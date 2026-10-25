from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
R=Path(__file__).resolve().parents[1]; O=R/'docs/images';O.mkdir(exist_ok=True)
F='/System/Library/Fonts/STHeiti Light.ttc';font=lambda n:ImageFont.truetype(F,n)
im=Image.new('RGB',(2100,1220),'white');d=ImageDraw.Draw(im)
def arrow(a,b,color='#42566b',w=4):
 import math
 d.line([a,b],fill=color,width=w);t=math.atan2(b[1]-a[1],b[0]-a[0]);d.polygon([b,(b[0]-16*math.cos(t-.45),b[1]-16*math.sin(t-.45)),(b[0]-16*math.cos(t+.45),b[1]-16*math.sin(t+.45))],fill=color)
def text(x,y,lines,size=27,color='#172735'):
 for i,line in enumerate(lines):d.text((x,y+i*(size+10)),line,font=font(size),fill=color)
def box(x,y,w,h,lines,fill='#eaf0f6',outline='#758da7',size=28):
 d.rounded_rectangle((x,y,x+w,y+h),14,fill=fill,outline=outline,width=3);text(x+22,y+18,lines,size)
text(55,20,['Week 3–4 Bootloader 状态与失败去向'],42)
ys=[90,210,330,450,570,690,810,930]
main=[['初始化 C 环境 / board / UART','尚未写入状态时 boot_status = 0'],['绑定存储；校验头 / 长度 / 地址 / CRC / 向量','全部通过才开始加载'],['复制 payload 到 0x00010000','仅使用已校验的 size / load'],['复核运行区 CRC / 向量','拒绝复制损坏与非法向量'],['排空 UART；屏蔽并清理中断 / SysTick','设置 VTOR / MSP 后跳转'],['应用初始化 .data / .bss；检查交接状态','应用自行初始化 UART'],['应用启用中断；验证 SVC','app_svc_count 必须为 1'],['APP: RUNNING；app_status = 1','最终在 app_idle 中循环等待']]
for y,lines in zip(ys,main):box(55,y,810,94,lines,size=27)
for y in ys[:-1]:arrow((460,y+94),(460,y+120));text(475,y+94,['通过'],19)
errors=[(90,['UART 初始化或输出失败','boot_status = 0x101；不保证可输出错误行']),
(210,['存储绑定 / image_validate 失败 → REJECT(1..7)','先写 boot_status；成功输出后等待']),
(330,['image_load 失败 → REJECT(1 / 4)','读取失败 / 重复边界检查失败']),
(450,['boot_verify_loaded 失败 → REJECT(4 / 7 / 8)','长度 / 向量 / 复制 CRC']),
(570,['最终 uart_flush 失败','boot_status = 0x101；不执行应用']),
(690,['应用 CRT / 交接检查失败 → app_status = 2','应用 UART 失败 → 4；进入 app_idle']),
(810,['SVC 检查失败 → app_status = 2；app_idle','应用异常 → 3，记录 app_exception；app_idle'])]
for y,lines in errors:
 box(1100,y,920,94,lines,fill='#fff0eb',outline='#c8836d',size=25);arrow((865,y+47),(1100,y+47),'#a9533b');text(900,y+15,['失败'],24,'#8c3f2c')
# Shared Bootloader idle destination and fault entry are separate from app idle.
box(1100,930,920,94,['Bootloader 失败 → boot_idle 循环','无自动重试、备份切换或应用跳转'],fill='#fff0eb',outline='#c8836d',size=28)
for y in ys[:5]:
 arrow((2020,y+47),(2070,y+47),'#a9533b',3)
d.line((2070,137,2070,977),fill='#a9533b',width=3);arrow((2070,977),(2020,977),'#a9533b',3)
text(60,1080,['Bootloader 异常：记录 exception_number，boot_status = 0x100 → boot_idle；通常没有 REJECT 行。',
'boot_status = 0 在跳转前再次写入，单看 0 不能区分“尚未初始化状态”和“已交接”。',
'UART 在 REJECT 输出途中失败：可能仅输出部分文本，最终 boot_status 被改写为 0x101。'],28)
im.save(O/'boot-state-complete.png')
(O/'boot-state-complete.mmd').write_text('''flowchart TD
 INIT[初始化 C / board / UART] --> BIND[绑定存储]
 BIND --> VALID[头 / 长度 / 地址 / CRC / 向量校验]
 BIND -->|1| REJECT
 VALID --> LOAD[加载 payload]
 LOAD --> VERIFY[运行区 CRC / 向量复核]
 VERIFY --> FLUSH[排空 UART]
 FLUSH --> HANDOFF[清理中断 / VTOR / MSP / boot_status=0]
 HANDOFF --> APPINIT[应用 CRT / 交接检查 / UART]
 APPINIT --> SVC[启用中断并检查 SVC]
 SVC --> RUN[APP RUNNING / app_status=1 / app_idle]
 INIT -->|UART失败| TX[boot_status=0x101]
 VALID -->|1..7| REJECT[写 boot_status / REJECT 十六进制输出]
 LOAD -->|1或4| REJECT
 VERIFY -->|4或7或8| REJECT
 FLUSH -->|失败| TX
 REJECT -->|输出失败| TX
 REJECT --> IDLE[boot_idle]
 TX --> IDLE
 EXC[Bootloader异常 / exception_number / status=0x100] --> IDLE
 APPINIT -->|CRT或交接失败| A2[app_status=2 / app_idle]
 APPINIT -->|UART失败| A4[app_status=4 / app_idle]
 SVC -->|计数失败| A2
 AEXC[应用异常 / app_exception] --> A3[app_status=3 / app_idle]
''')
print(O/'boot-state-complete.png')
