/** @file
 * @brief Application-side trial confirmation and watchdog release.
 */
#ifndef WEEK6_APP_CONFIRM_H
#define WEEK6_APP_CONFIRM_H
/** Application-side trial confirmation. Writes the durable "confirmed"
 * record and stops the CMSDK watchdog. Test keys and this helper are
 * lab-only; they are not a production attestation API. */
int week6_app_confirm(void);
#endif
