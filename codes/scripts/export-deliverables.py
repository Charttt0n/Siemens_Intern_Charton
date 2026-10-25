from pathlib import Path
import re,ast,json,html,textwrap,zipfile,hashlib
from PIL import Image,ImageDraw,ImageFont
from docx import Document
from docx.shared import Inches,Pt,RGBColor
from docx.enum.section import WD_SECTION_START, WD_ORIENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
R=Path(__file__).resolve().parents[2]; C=R/'codes'; OUT=R/'output/pdf/2026-10-24-反馈修订';OUT.mkdir(parents=True,exist_ok=True)
F='/System/Library/Fonts/STHeiti Light.ttc';font=lambda n:ImageFont.truetype(F,n)
imgs=C/'docs/images';imgs.mkdir(exist_ok=True)
def arrow(d,a,b,color='#334b60'):
 d.line([a,b],fill=color,width=3)
 import math
 t=math.atan2(b[1]-a[1],b[0]-a[0]);d.polygon([b,(b[0]-14*math.cos(t-.4),b[1]-14*math.sin(t-.4)),(b[0]-14*math.cos(t+.4),b[1]-14*math.sin(t+.4))],fill=color)
im=Image.new('RGB',(1400,1190),'white');d=ImageDraw.Draw(im)
d.text((40,25),'UART 命令完成 → 检查点 → QMP cont',font=font(34),fill='black')
xs=[150,480,850,1240]
for x,label in zip(xs,['固件 / UART','宿主 Session','QEMU / QMP','磁盘镜像']):
 d.rounded_rectangle((x-120,95,x+120,155),10,fill='#e8eef3',outline='#b5c4ce');d.text((x-105,110),label,font=font(24),fill='black');d.line((x,155,x,1100),fill='#bcc8d1',width=2)
steps=[(0,1,'完整应答及 > 提示符'),(1,1,'quiescent = True'),(1,2,'stop；等待成功响应'),(1,2,'pmemsave 0x20300000 / 1 MiB'),(2,1,'导出文件完成；检查长度'),(1,3,'写临时文件并 fsync'),(1,3,'os.replace；fsync目录'),(1,2,'finally: cont'),(2,1,'成功响应；可继续命令')]
for i,(a,b,t) in enumerate(steps):
 y=220+i*92
 if a==b:d.text((xs[a]+20,y-20),t,font=font(23),fill='#243b50')
 else:
  arrow(d,(xs[a],y),(xs[b],y));d.text((min(xs[a],xs[b])+12,y-36),t,font=font(23),fill='#243b50')
d.text((45,1110),'replace 前失败保留旧镜像；finally 尝试 cont。QMP 断开等错误向上报告。',font=font(24),fill='#8a3d22');im.save(imgs/'uart-checkpoint-sequence.png')
im=Image.new('RGB',(1400,1510),'white');d=ImageDraw.Draw(im);d.text((50,25),'Bootloader 校验与应用启动状态',font=font(36),fill='black')
labels=['初始化 C 环境 / 板级 / UART','头格式与头 CRC','长度与加载地址','payload CRC / 向量范围','复制 payload 到运行区','复制后 CRC / 向量复核','排空 UART / 清中断 / VTOR / MSP','应用初始化与交接自检','APP: RUNNING']
for i,t in enumerate(labels):
 y=100+i*145;d.rounded_rectangle((100,y,850,y+85),12,fill='#e8eef3',outline='#567085',width=2);d.text((125,y+26),t,font=font(29),fill='black')
 if i<8:arrow(d,(470,y+85),(470,y+145))
 if 1<=i<=5:arrow(d,(850,y+42),(1010,y+42), '#a64d37')
d.rounded_rectangle((1010,240,1360,1000),12,fill='#fff2ed',outline='#a64d37',width=2)
for y,t in [(275,'校验失败'),(355,'设置 boot_status'),(435,'REJECT: 0x........'),(515,'提示符 >'),(595,'boot_idle'),(710,'不执行应用'),(790,'不尝试备份')]:d.text((1030,y),t,font=font(24),fill='#742c1c')
d.text((70,1435),'UART失败 → 0x101；CPU异常 → 0x100并记录exception_number；均进入boot_idle。',font=font(25),fill='#742c1c');im.save(imgs/'boot-state.png')
# Reuse the inspected PDF layout functions without executing the old batch export.
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import SimpleDocTemplate,Paragraph,Spacer,Table,TableStyle,PageBreak,Image as RLImage,KeepTogether
from reportlab.lib.utils import ImageReader
from reportlab.platypus import BaseDocTemplate, Frame, PageTemplate, NextPageTemplate
pdfmetrics.registerFont(TTFont('CN',F,subfontIndex=0));pdfmetrics.registerFont(TTFont('CNBold','/System/Library/Fonts/STHeiti Medium.ttc',subfontIndex=0))
styles={'body':ParagraphStyle('body',fontName='CN',fontSize=10,leading=16,spaceAfter=7,wordWrap='CJK'),'small':ParagraphStyle('small',fontName='CN',fontSize=8,leading=12,spaceAfter=2,wordWrap='CJK'),'code':ParagraphStyle('code',fontName='CN',fontSize=8,leading=12,spaceAfter=1,wordWrap='CJK')}
for i,size in [(1,19),(2,14),(3,11.5),(4,10.5)]:styles[f'h{i}']=ParagraphStyle(f'h{i}',fontName='CNBold',fontSize=size,leading=size*1.45,spaceBefore=10,spaceAfter=8,keepWithNext=True,wordWrap='CJK')
WIDTH=481.89
srcs=[R/'reports/week1-2/week1-2-report.md',R/'reports/week3/week3-report.md',R/'reports/week4/week4-report.md',C/'docs/teacher-feedback-technical.md',*[C/'docs'/f'week{i}-test-report.md' for i in range(1,5)],C/'docs/feedback-test-report.md',C/'docs/cli-week34-test-report.md',C/'docs/uart-baud-validation.md',C/'docs/irq-contract-validation.md',C/'docs/tx-full-validation.md',C/'docs/week34-review-closure.md',R/'planning/teacher-feedback-2026-10-24.md']
TARGETS={p.resolve():p.stem+'.pdf' for p in srcs}
# Imported functions expect Image to mean reportlab Image.
Image=RLImage
def footer(c, d):
    c.setFont('CN', 8)
    c.setFillColor(colors.HexColor('#607080'))
    c.drawRightString(c._pagesize[0] - 56.7, 28, str(d.page))

def build(path, story, title):
    doc = BaseDocTemplate(str(path), pagesize=(595.28, 841.89), title=title, author='Charton')
    doc.addPageTemplates([
        PageTemplate(id='portrait', frames=[Frame(56.7,46,481.88,747.89,id='p')], pagesize=(595.28,841.89), onPage=footer),
        PageTemplate(id='landscape', frames=[Frame(36,36,769.89,523.28,id='l')], pagesize=(841.89,595.28), onPage=footer)])
    doc.build(story)

def markdown(src, target):
    txt = src.read_text()
    refs = dict(re.findall('^\\[([^\\]]+)\\]:\\s*(\\S+)', txt, re.M))
    lines = txt.splitlines()

    def fmt(t):
        t = html.escape(t)

        def anchor(label, url):
            url = html.unescape(url)
            if not url.startswith(('https://', 'http://', '#')):
                resolved = (src.parent / url.split('#')[0]).resolve()
                if resolved in TARGETS:
                    url = TARGETS[resolved]
                elif resolved.is_file():
                    url = resolved.as_uri()
                else:
                    return label
            return '<a href="' + html.escape(url, quote=True) + '" color="#225d8c">' + label + '</a>'
        t = re.sub('\\[([^\\]]+)\\]\\(([^)]+)\\)', lambda m: anchor(m[1], m[2]), t)
        t = re.sub('\\[([^\\]]+)\\]\\[([^\\]]+)\\]', lambda m: anchor(m[1], refs[m[2]]) if m[2] in refs else m[0], t)
        t = re.sub('\\*\\*(.+?)\\*\\*', '<b>\\1</b>', t)
        t = re.sub('`([^`]+)`', '\\1', t)
        return t

    def para(t, style='body'):
        return Paragraph(fmt(t), styles[style])
    story = []
    page_width = WIDTH
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if not line or re.match('^\\[[^\\]]+\\]:', line):
            i += 1
            continue
        if line.startswith('<!--'):
            if 'landscape' in line or 'portrait' in line:
                target_template = 'landscape' if 'landscape' in line else 'portrait'
                if any(x.strip() for x in lines[i+1:]):
                    story.extend([NextPageTemplate(target_template), PageBreak()])
                page_width = 769.89 if target_template == 'landscape' else WIDTH
            elif 'pagebreak' in line:
                story.append(PageBreak())
            i += 1
            continue
        if line.startswith('```'):
            i += 1
            code = []
            while i < len(lines) and (not lines[i].startswith('```')):
                code.append(lines[i])
                i += 1
            for ln in code:
                for part in textwrap.wrap(ln, 94, replace_whitespace=False, drop_whitespace=False) or [' ']:
                    story.append(Paragraph(html.escape(part).replace(' ', '&#160;'), styles['code']))
            story.append(Spacer(1, 9))
            i += 1
            continue
        if line.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].strip().startswith('|'):
                cells = lines[i].strip().strip('|').split('|')
                if not all((re.fullmatch('\\s*:?-+:?\\s*', c) for c in cells)):
                    rows.append(cells)
                i += 1
            n = max(map(len, rows))
            rows = [r + [''] * (n - len(r)) for r in rows]
            weights = [max(5, min(36, max((len(re.sub('\\[([^]]+)\\]\\([^)]+\\)', '\\1', r[j])) for r in rows)))) for j in range(n)]
            widths = [page_width * w for w in [.14,.13,.20,.10,.07,.13,.23]] if n == 7 else ([page_width * w for w in ([.11,.20,.25,.10,.34] if n == 5 else [.12,.16,.44,.28])] if n in (4,5) else [page_width * w / sum(weights) for w in weights])
            if n == 4 and rows[0][0].strip() == '检查对象':
                widths = [page_width*w for w in [.16,.34,.24,.26]]
            elif n == 4 and rows[0][0].strip() == 'boot_status值':
                widths = [page_width*w for w in [.12,.40,.22,.26]]
            elif n == 7 and rows[0][0].strip() == '编号与反馈':
                widths = [page_width*w for w in [.11,.25,.20,.22,.08,.06,.08]]
            table = Table([[para(c.strip(), 'small') for c in row] for row in rows], colWidths=widths, repeatRows=1, hAlign='LEFT')
            table.setStyle(TableStyle([('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#e6edf4')), ('GRID', (0, 0), (-1, -1), 0.4, colors.HexColor('#c5cfd9')), ('VALIGN', (0, 0), (-1, -1), 'TOP'), ('LEFTPADDING', (0, 0), (-1, -1), 6), ('RIGHTPADDING', (0, 0), (-1, -1), 6), ('TOPPADDING', (0, 0), (-1, -1), 6), ('BOTTOMPADDING', (0, 0), (-1, -1), 6)]))
            if n == 7 and rows[0][0].strip() == '编号与反馈':
                table.setStyle(TableStyle([('TOPPADDING',(0,0),(-1,-1),4),('BOTTOMPADDING',(0,0),(-1,-1),4)]))
            story.extend([table, Spacer(1, 10)])
            continue
        image = re.fullmatch('!\\[([^]]*)\\]\\(([^)]+)\\)', line)
        if image:
            p = src.parent / image[2]
            if p.is_file():
                w, h = ImageReader(str(p)).getSize()
                scale = min(page_width / w, 460 / h)
                story.extend([Image(str(p), width=w * scale, height=h * scale), para(image[1], 'small'), Spacer(1, 8)])
            i += 1
            continue
        h = re.match('^(#{1,4})\\s+(.+)', line)
        if h:
            story.append(para(h[2], f'h{len(h[1])}'))
            i += 1
            continue
        if re.fullmatch('[-*_]{3,}', line):
            i += 1
            continue
        story.append(para(line))
        i += 1
    build(target, story, lines[0].lstrip('# '))

def clean(s):
 s=re.sub(r'!?\[([^]]*)\]\([^)]*\)',r'\1',s);return s.replace('**','').replace('`','')
def word(md,out):
 doc=Document();sec=doc.sections[0];sec.top_margin=Inches(.65);sec.bottom_margin=Inches(.65);sec.left_margin=Inches(.65);sec.right_margin=Inches(.65)
 for name in ['Normal','Title','Heading 1','Heading 2','Heading 3']:
  st=doc.styles[name];st.font.name='Arial Unicode MS';st.font.size=Pt(10 if name=='Normal' else (20 if name=='Title' else 13));st.font.color.rgb=RGBColor(0,0,0);st.element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'),'Arial Unicode MS')
 doc.styles['Normal'].paragraph_format.space_after=Pt(5)
 lines=md.read_text().splitlines();i=0;fence=False
 while i<len(lines):
  s=lines[i].strip();i+=1
  if not s:continue
  if s.startswith('<!--'):
   if 'landscape' in s or 'portrait' in s:
    if any(x.strip() for x in lines[i:]):
     sec=doc.add_section(WD_SECTION_START.NEW_PAGE)
     wide='landscape' in s;sec.orientation=WD_ORIENT.LANDSCAPE if wide else WD_ORIENT.PORTRAIT
     sec.page_width=Inches(11.7 if wide else 8.3);sec.page_height=Inches(8.3 if wide else 11.7)
   elif 'pagebreak' in s:doc.add_page_break()
   continue
  if s.startswith('```'):fence=not fence;continue
  if s.startswith('|'):
   block=[s]
   while i<len(lines) and lines[i].strip().startswith('|'):block.append(lines[i].strip());i+=1
   rows=[[clean(c.strip()) for c in ln.strip('|').split('|')] for ln in block if not re.fullmatch(r'[| :\-]+',ln)]
   table=doc.add_table(rows=0,cols=max(map(len,rows)));table.autofit=False
   widecols=[.14,.13,.20,.10,.07,.13,.23] if len(rows[0])==7 else None
   if widecols:
    available=sec.page_width-sec.left_margin-sec.right_margin
    for col,fraction in zip(table.columns,widecols):col.width=int(available*fraction)
   for j,row in enumerate(rows):
    cells=table.add_row().cells
    if widecols:
     for cell,fraction in zip(cells,widecols):cell.width=int(available*fraction)
    for c,t in zip(cells,row):
     c.text=t
     for p in c.paragraphs:
      p.paragraph_format.space_after=Pt(3)
      for run in p.runs:run.font.size=Pt(8)
    if j==0:
     repeat=OxmlElement('w:tblHeader');table.rows[-1]._tr.get_or_add_trPr().append(repeat)
     for c in cells:
      sh=OxmlElement('w:shd');sh.set(qn('w:fill'),'E8EEF3');c._tc.get_or_add_tcPr().append(sh)
   borders=OxmlElement('w:tblBorders')
   for side in ['top','left','bottom','right','insideH','insideV']:
    e=OxmlElement('w:'+side);e.set(qn('w:val'),'single');e.set(qn('w:sz'),'4');e.set(qn('w:color'),'D9D9D9');borders.append(e)
   table._tbl.tblPr.append(borders);continue
  pic=re.fullmatch(r'!\[([^]]*)\]\(([^)]+)\)',s)
  if pic:
   p=md.parent/pic[2]
   if p.exists():doc.add_picture(str(p),width=sec.page_width-sec.left_margin-sec.right_margin)
   continue
  h=re.match(r'^(#{1,4}) (.*)',s)
  if h:doc.add_paragraph(clean(h[2]),style='Title' if len(h[1])==1 else 'Heading '+str(min(3,len(h[1])-1)))
  else:
   para=doc.add_paragraph(clean(s))
   if fence:
    para.paragraph_format.space_after=Pt(0);para.paragraph_format.line_spacing=1
    para.paragraph_format.keep_with_next=(i<len(lines) and not lines[i].startswith('```'))
    for run in para.runs:run.font.size=Pt(8.5)
 for root in [doc.element,doc.styles.element]:
  for border in list(root.iter(qn('w:pBdr'))): border.getparent().remove(border)
 doc.save(out)
for p in srcs:
 markdown(p,OUT/(p.stem+'.pdf'))
 if 'reports' in p.parts: word(p,p.parent/('Week'+p.parent.name.removeprefix('week')+'实习周报.docx'))
 elif p.stem=='teacher-feedback-technical':word(p,p.with_suffix('.docx'))
print('Created',len(srcs),'PDFs and synchronized 3 Word files in',OUT)
