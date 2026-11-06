"""Render the Week4 control-flow figure used by the weekly report."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import math

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/images'
FONT = '/System/Library/Fonts/STHeiti Light.ttc'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    im = Image.new('RGB', (1500, 1510), 'white')
    draw = ImageDraw.Draw(im)
    def text(x, y, lines, size=31, color='#172735'):
        f = ImageFont.truetype(FONT, size)
        for n, line in enumerate(lines):
            draw.text((x, y+n*(size+10)), line, font=f, fill=color)
    def box(x, y, w, h, lines, error=False):
        draw.rounded_rectangle((x, y, x+w, y+h), 10,
            fill='#fff3ee' if error else '#edf2f6',
            outline='#b47b66' if error else '#8193a3', width=2)
        text(x+20, y+16, lines, 29 if error else 32)
    def arrow(a,b,color='#506b7c'):
        draw.line([a,b],fill=color,width=4)
        t=math.atan2(b[1]-a[1],b[0]-a[0])
        draw.polygon([b,(b[0]-14*math.cos(t-.45),b[1]-14*math.sin(t-.45)),
                        (b[0]-14*math.cos(t+.45),b[1]-14*math.sin(t+.45))],fill=color)
    text(30, 12, ['Week4 启动流程与失败状态'], 44)
    main_rows=[
        ['C 初始化 / board / UART', 'boot_status 初值为 0'],
        ['绑定存储 / 头及内容校验', '长度 / 地址 / CRC / 全部向量'],
        ['复制 payload 至 0x10000', '复核运行区 CRC 与向量'],
        ['输出 LOAD / JUMP 并 flush', '确保 UART 发送检查通过'],
        ['写 boot_status=0 并交接', '清中断 / VTOR / MSP / BX'],
        ['应用 CRT / 交接检查', '重新初始化应用 UART'],
        ['输出 CRT / HANDOFF', '开启中断并检查 SVC 计数'],
        ['输出 SVC PASS / RUNNING', 'app_status=1 → app_idle'],
    ]
    errors={
        0:['UART 初始化失败', '0x101 → boot_idle'],
        1:['拒绝 1 至 7', 'REJECT → boot_idle'],
        2:['拒绝 1 / 4 / 7 / 8', 'REJECT → boot_idle'],
        3:['flush 失败', '0x101 → boot_idle'],
        5:['CRT / 交接失败：2', 'UART 失败：4', '→ app_idle'],
        6:['SVC 计数失败：2', 'UART 失败：4', '→ app_idle'],
        7:['最终日志失败：4', '→ app_idle'],
    }
    for n, lines in enumerate(main_rows):
        y=88+n*145
        box(30,y,825,112,lines)
        if n<7:
            arrow((440,y+112),(440,y+145))
        if n in errors:
            box(1035,y,435,126 if len(errors[n])==3 else 112,errors[n],True)
            arrow((855,y+55),(1035,y+55),'#9e614b')
            text(885,y+12,['失败'],25,'#8e503e')
    text(30,1265,[
        '任意 Bootloader 日志输出失败：boot_status=0x101 → boot_idle。',
        'Bootloader 异常：0x100，并记录 exception_number → boot_idle。',
        '应用异常：app_status=3，并记录 app_exception → app_idle。',
        '应用错误提示若发送失败，app_status 可由 2 改为 4。',
        'idle 为循环等待；当前没有自动重试或备份切换。',
    ],30)
    im.save(OUT/'week4-boot-flow-reviewed.png')
    (OUT/'week4-boot-flow-reviewed.mmd').write_text('''flowchart TD
  INIT[C初始化 board UART] --> VALID[绑定存储及镜像校验]
  VALID --> LOAD[复制payload并复核运行区]
  LOAD --> FLUSH[输出LOAD JUMP并flush]
  FLUSH --> HANDOFF[boot_status=0 清中断 设置VTOR MSP并跳转]
  HANDOFF --> APP[应用CRT与交接检查 初始化UART]
  APP --> SVC[输出CRT HANDOFF 开中断并检查SVC]
  SVC --> LOG[输出SVC PASS RUNNING]
  LOG --> RUN[app_status=1 app_idle]
  INIT -->|UART失败| BTX[boot_status=0x101 boot_idle]
  VALID -->|1至7| REJECT[写boot_status 输出REJECT boot_idle]
  LOAD -->|1或4或7或8| REJECT
  FLUSH -->|失败| BTX
  BPRINT[任意Bootloader日志失败 包括REJECT] --> BTX
  BFAULT[Bootloader异常 记录exception_number] --> BEX[boot_status=0x100 boot_idle]
  APP -->|CRT或交接失败| A2[app_status=2 输出错误 app_idle]
  APP -->|UART失败| A4[app_status=4 app_idle]
  SVC -->|计数失败| A2
  SVC -->|日志失败| A4
  LOG -->|日志失败| A4
  A2 -->|错误提示发送失败| A4
  AFAULT[应用异常 记录app_exception] --> A3[app_status=3 app_idle]
''')
    print(OUT/'week4-boot-flow-reviewed.png')

if __name__ == '__main__':
    main()
