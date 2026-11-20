#include "update.h"
#include "secure_image.h"
#include "watchdog.h"

/* Week-6 signed upload path. Linked instead of week5/update.c so the
 * Week-5 source and behaviour stay unchanged. File length is
 * 32 + payload + SIG_TRAILER_SIZE; metadata progress stays payload-only. */
#define SLOT_MIN_FILE (IMAGE_HEADER_SIZE + APP_VECTOR_SIZE + SIG_TRAILER_SIZE)
#define SLOT_MAX_FILE APP_EXEC_SIZE

static int flash_read_op(void *context, flash_partition_id slot, uint32_t offset,
                         void *output, uint32_t size) {
    return flash_read(context, slot, offset, output, size);
}
static int flash_program_op(void *context, flash_partition_id slot,
                            uint32_t offset, const void *input, uint32_t size) {
    return flash_program(context, slot, offset, input, size);
}
static int flash_erase_op(void *context, flash_partition_id slot,
                          uint32_t offset, uint32_t size) {
    return flash_erase(context, slot, offset, size);
}

void update_flash_bind(update_flash_ops *ops, flash_device *dev) {
    if (!ops) return;
    ops->context = dev;
    ops->read = flash_read_op;
    ops->program = flash_program_op;
    ops->erase = flash_erase_op;
}

static flash_partition_id other_slot(flash_partition_id slot) {
    return slot == FLASH_APP ? FLASH_BACKUP : FLASH_APP;
}

static uint32_t payload_progress(const update_context *ctx) {
    uint32_t done = ctx->programmed > IMAGE_HEADER_SIZE
                        ? ctx->programmed - IMAGE_HEADER_SIZE : 0u;
    if (done > ctx->payload_expected) done = ctx->payload_expected;
    return done;
}

static int program_word(update_context *ctx) {
    uint8_t check[FLASH_PROGRAM_SIZE];
    if (ctx->ops.program(ctx->ops.context, ctx->target, ctx->programmed,
                         ctx->stage, FLASH_PROGRAM_SIZE) ||
        ctx->ops.read(ctx->ops.context, ctx->target, ctx->programmed,
                      check, sizeof(check))) return UPDATE_WRITE;
    for (uint32_t i = 0; i < sizeof(check); ++i)
        if (check[i] != ctx->stage[i]) return UPDATE_WRITE;
    ctx->programmed += FLASH_PROGRAM_SIZE;
    ctx->staged = 0;
    watchdog_kick();
    return UPDATE_OK;
}

static void persist_failure(update_context *ctx, int error) {
    if (!ctx || !ctx->begun || ctx->failed) return;
    meta_record record = ctx->record;
    record.active_slot = ctx->active;
    record.target_slot = ctx->target;
    record.state = META_STATE_FAILED;
    record.expected_length = ctx->payload_expected;
    record.progress = payload_progress(ctx);
    record.candidate_version = 0;
    record.error = (uint32_t)error;
    if (meta_commit(&ctx->meta_ops, &record) == META_OK)
        ctx->record = record;
    ctx->failed = 1;
}

int update_prepare(update_context *ctx, flash_device *dev,
                   const update_flash_ops *ops, const meta_flash_ops *meta_ops) {
    if (!ctx || !dev) return UPDATE_ARGUMENT;
    ctx->dev = dev;
    if (ops) ctx->ops = *ops;
    else update_flash_bind(&ctx->ops, dev);
    if (!ctx->ops.read || !ctx->ops.program || !ctx->ops.erase)
        return UPDATE_ARGUMENT;
    if (meta_ops) ctx->meta_ops = *meta_ops;
    else meta_flash_bind(&ctx->meta_ops, dev);
    ctx->expected = 0;
    ctx->payload_expected = 0;
    ctx->received = 0;
    ctx->programmed = 0;
    ctx->staged = 0;
    ctx->begun = 0;
    ctx->finished = 0;
    ctx->failed = 0;
    int status = meta_init(&ctx->meta_ops, &ctx->record);
    if (status != META_OK) return UPDATE_META;
    uint32_t active = ctx->record.active_slot;
    if (active != FLASH_APP && active != FLASH_BACKUP) return UPDATE_STATE;
    ctx->active = (flash_partition_id)active;
    ctx->target = other_slot(ctx->active);
    return UPDATE_OK;
}

static int sink_begin(void *context, const char *name, uint32_t size) {
    (void)name;
    update_context *ctx = context;
    if (!ctx || ctx->begun) return 1;
    if (size < SLOT_MIN_FILE || size > SLOT_MAX_FILE) return 1;
    ctx->expected = size;
    ctx->payload_expected = size - IMAGE_HEADER_SIZE - SIG_TRAILER_SIZE;
    ctx->received = 0;
    ctx->programmed = 0;
    ctx->staged = 0;
    ctx->finished = 0;
    ctx->failed = 0;
    meta_record record = ctx->record;
    record.active_slot = ctx->active;
    record.target_slot = ctx->target;
    record.state = META_STATE_RECEIVING;
    record.expected_length = ctx->payload_expected;
    record.progress = 0;
    record.candidate_version = 0;
    record.error = 0;
    if (meta_commit(&ctx->meta_ops, &record) != META_OK) {
        ctx->failed = 1;
        return 1;
    }
    ctx->record = record;
    ctx->begun = 1;
    const flash_partition *partition = flash_partition_get(ctx->target);
    if (!partition ||
        ctx->ops.erase(ctx->ops.context, ctx->target, 0, partition->size)) {
        persist_failure(ctx, UPDATE_ERASE);
        return 1;
    }
    watchdog_kick();
    return 0;
}

static int sink_write(void *context, const uint8_t *data, uint32_t size) {
    update_context *ctx = context;
    if (!ctx || !ctx->begun || ctx->failed || ctx->finished) return 1;
    if (size == 0) return 0;
    if (!data || size > ctx->expected - ctx->received) {
        persist_failure(ctx, UPDATE_LENGTH);
        return 1;
    }
    for (uint32_t i = 0; i < size; ++i) {
        ctx->stage[ctx->staged++] = data[i];
        if (ctx->staged == FLASH_PROGRAM_SIZE) {
            if (program_word(ctx) != UPDATE_OK) {
                persist_failure(ctx, UPDATE_WRITE);
                return 1;
            }
        }
    }
    ctx->received += size;
    meta_record record = ctx->record;
    record.state = META_STATE_RECEIVING;
    record.expected_length = ctx->payload_expected;
    record.progress = payload_progress(ctx);
    record.candidate_version = 0;
    record.error = 0;
    if (meta_commit(&ctx->meta_ops, &record) != META_OK) {
        ctx->failed = 1;
        return 1;
    }
    ctx->record = record;
    return 0;
}

static int sink_finish(void *context, uint32_t size) {
    update_context *ctx = context;
    image_info info;
    int valid;
    if (!ctx || !ctx->begun || ctx->failed || ctx->finished) return 1;
    if (size != ctx->expected || ctx->received != ctx->expected) {
        persist_failure(ctx, UPDATE_LENGTH);
        return 1;
    }
    if (ctx->staged) {
        for (uint32_t i = ctx->staged; i < FLASH_PROGRAM_SIZE; ++i)
            ctx->stage[i] = 0xFFu;
        if (program_word(ctx) != UPDATE_OK) {
            persist_failure(ctx, UPDATE_WRITE);
            return 1;
        }
    }
    meta_record record = ctx->record;
    record.state = META_STATE_VERIFYING;
    record.progress = ctx->payload_expected;
    if (meta_commit(&ctx->meta_ops, &record) != META_OK) {
        ctx->failed = 1;
        return 1;
    }
    ctx->record = record;
    valid = secure_validate_slot(ctx->dev, ctx->target, &info);
    if (valid != SECURE_OK ||
        info.size + IMAGE_HEADER_SIZE + SIG_TRAILER_SIZE != ctx->expected) {
        persist_failure(ctx, UPDATE_IMAGE);
        return 1;
    }
    record = ctx->record;
    record.active_slot = ctx->active;
    record.target_slot = ctx->target;
    record.state = META_STATE_READY;
    record.expected_length = ctx->payload_expected;
    record.progress = ctx->payload_expected;
    record.candidate_version = info.version;
    record.error = 0;
    if (meta_commit(&ctx->meta_ops, &record) != META_OK) {
        ctx->failed = 1;
        return 1;
    }
    ctx->record = record;
    ctx->finished = 1;
    return 0;
}

static void sink_abort(void *context) {
    update_context *ctx = context;
    if (!ctx || !ctx->begun || ctx->finished || ctx->failed) return;
    persist_failure(ctx, UPDATE_ABORTED);
}

void update_sink_init(update_context *ctx, ymodem_sink *sink) {
    if (!sink) return;
    sink->context = ctx;
    sink->begin = sink_begin;
    sink->write = sink_write;
    sink->finish = sink_finish;
    sink->abort = sink_abort;
}

int update_activate(update_context *ctx) {
    image_info info;
    meta_record current;
    if (!ctx || !ctx->finished || ctx->failed) return UPDATE_STATE;
    if (meta_read(&ctx->meta_ops, &current) != META_OK) return UPDATE_META;
    if (current.generation != ctx->record.generation ||
        current.state != META_STATE_READY ||
        current.active_slot != (uint32_t)ctx->active ||
        current.target_slot != (uint32_t)ctx->target ||
        current.expected_length != ctx->payload_expected ||
        current.progress != ctx->payload_expected) return UPDATE_STATE;
    if (secure_validate_slot(ctx->dev, ctx->target, &info) != SECURE_OK ||
        info.size != current.expected_length ||
        info.version != current.candidate_version) return UPDATE_IMAGE;
    current.active_slot = current.target_slot;
    current.target_slot = META_SLOT_NONE;
    current.state = META_STATE_IDLE;
    current.progress = current.expected_length = 0;
    current.candidate_version = current.error = 0;
    if (meta_commit(&ctx->meta_ops, &current) != META_OK) return UPDATE_META;
    ctx->record = current;
    return UPDATE_OK;
}
