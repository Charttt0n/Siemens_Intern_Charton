#!/usr/bin/env python3
"""Export weekly Markdown to Word. Uses python-docx; render Word with LibreOffice.
From codes/: python3 scripts/export_weekly_reports.py [--root PROJECT] [--week 3 4]
Existing figures and literal evidence paths are preserved. Markdown remains source.
"""
from pathlib import Path
import argparse,re
from docx import Document
from docx.shared import Inches,Pt,RGBColor,Mm
from docx.enum.section import WD_SECTION_START,WD_ORIENT
from docx.enum.table import WD_TABLE_ALIGNMENT,WD_CELL_VERTICAL_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from PIL import Image

def clean(t):
    t=re.sub(r'\[([^]]*)\]\(([^)]+)\)',r'\1',t)
    return t.replace('**','').replace('`','')

def word(md,out):
    md=Path(md);doc=Document();sec=doc.sections[0]
    def setup(sec,wide=False):
        sec.orientation=WD_ORIENT.LANDSCAPE if wide else WD_ORIENT.PORTRAIT
        sec.page_width=Inches(11.69 if wide else 8.27);sec.page_height=Inches(8.27 if wide else 11.69)
        sec.top_margin=Inches(.6);sec.bottom_margin=Inches(.6);sec.left_margin=Inches(.65);sec.right_margin=Inches(.65)
    setup(sec);wide=False
    for name,size in [('Normal',10.5),('Title',21),('Heading 1',15),('Heading 2',12),('Heading 3',11)]:
        st=doc.styles[name];st.font.name='Arial Unicode MS';st.font.size=Pt(size);st.font.color.rgb=RGBColor(0,0,0)
        st.element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'),'Arial Unicode MS')
        st.paragraph_format.space_after=Pt(6)
    doc.styles['Normal'].paragraph_format.line_spacing=1.15
    # Evidence paths/identifiers may wrap at any character rather than overflow.
    compat=OxmlElement('w:doNotUseHTMLParagraphAutoSpacing');doc.settings.element.append(compat)
    footer=sec.footer.paragraphs[0];footer.alignment=2
    fld=OxmlElement('w:fldSimple');fld.set(qn('w:instr'),'PAGE');footer._p.append(fld)
    lines=md.read_text().splitlines();i=0;fence=False
    while i<len(lines):
        s=lines[i].strip();i+=1
        if not s:continue
        if s.startswith('<!--'):
            if 'landscape' in s or 'portrait' in s:
                want='landscape' in s
                if want!=wide:
                    sec=doc.add_section(WD_SECTION_START.NEW_PAGE);setup(sec,want);wide=want
            elif 'pagebreak' in s:doc.add_page_break()
            continue
        if s.startswith('```'):fence=not fence;continue
        pic=re.fullmatch(r'!\[([^]]*)\]\(([^)]+)\)',s)
        if pic:
            p=(md.parent/pic[2]).resolve()
            if not p.is_file():raise FileNotFoundError(p)
            iw,ih=Image.open(p).size;maxw=(sec.page_width-sec.left_margin-sec.right_margin)/914400
            maxh=(sec.page_height-sec.top_margin-sec.bottom_margin)/914400-1.7
            w=min(maxw,maxh*iw/ih)
            para=doc.add_paragraph();para.alignment=1;para.add_run().add_picture(str(p),width=Inches(w))
            para.paragraph_format.keep_with_next=True
            cap=doc.add_paragraph(pic[1]);cap.alignment=1
            for run in cap.runs:run.font.size=Pt(9)
            continue
        if s.startswith('|'):
            block=[s]
            while i<len(lines) and lines[i].strip().startswith('|'):block.append(lines[i].strip());i+=1
            rows=[[clean(c.strip()) for c in x.strip('|').split('|')] for x in block if not re.fullmatch(r'[| :\-]+',x)]
            n=max(map(len,rows));available=sec.page_width-sec.left_margin-sec.right_margin
            # Wide steps/output columns; IDs remain readable on landscape pages.
            h=rows[0][0]
            weights={3:[.23,.33,.44],4:[.16,.31,.25,.28],5:[.12,.23,.25,.07,.33],7:[.15,.13,.20,.11,.08,.15,.18]}.get(n,[1/n]*n)
            if n==4 and h in ['固定编号','用例编号']:weights=[.17,.41,.22,.20]
            if n==4 and rows[0][1]=='测试方式':weights=[.12,.16,.49,.23]
            if n==4 and rows[0][1]=='实际命令':weights=[.18,.21,.40,.21]
            if n==4 and h=='断言名':weights=[.27,.37,.18,.18]
            if n==4 and h=='检查对象':weights=[.17,.33,.22,.28]
            if n==4 and h=='boot_status值':weights=[.11,.39,.24,.26]
            table=doc.add_table(rows=0,cols=n);table.autofit=False
            for col,f in zip(table.columns,weights):col.width=int(available*f)
            for j,row in enumerate(rows):
                tr=table.add_row();pr=tr._tr.get_or_add_trPr();no=OxmlElement('w:cantSplit');pr.append(no)
                if j==0:
                    rep=OxmlElement('w:tblHeader');pr.append(rep)
                for k,cell in enumerate(tr.cells):
                    cell.width=int(available*weights[k]);cell.text=row[k] if k<len(row) else ''
                    if j==0:
                        sh=OxmlElement('w:shd');sh.set(qn('w:fill'),'E8EEF3');cell._tc.get_or_add_tcPr().append(sh)
                    for p in cell.paragraphs:
                        p.paragraph_format.space_after=Pt(3);p.paragraph_format.space_before=Pt(3);p.paragraph_format.line_spacing=1.08
                        p.paragraph_format.keep_with_next=False
                        for run in p.runs:
                            run.font.size=Pt(8.5 if n>=5 else 9);run.bold=j==0
                    margins=OxmlElement('w:tcMar')
                    for side in ['left','right']:
                        m=OxmlElement('w:'+side);m.set(qn('w:w'),'75');m.set(qn('w:type'),'dxa');margins.append(m)
                    cell._tc.get_or_add_tcPr().append(margins)
            borders=OxmlElement('w:tblBorders')
            for side in ['top','left','bottom','right','insideH','insideV']:
                x=OxmlElement('w:'+side);x.set(qn('w:val'),'single');x.set(qn('w:sz'),'4');x.set(qn('w:color'),'CBD5E1');borders.append(x)
            table._tbl.tblPr.append(borders);doc.add_paragraph().paragraph_format.space_after=Pt(2)
            continue
        h=re.match(r'^(#{1,4})\s+(.+)',s)
        if h:
            p=doc.add_paragraph(clean(h[2]),style='Title' if len(h[1])==1 else 'Heading '+str(len(h[1])-1))
        else:
            p=doc.add_paragraph(clean(s))
            if fence:
                p.paragraph_format.line_spacing=1;p.paragraph_format.space_after=Pt(1)
                p.paragraph_format.keep_with_next=(i<len(lines) and not lines[i].startswith('```'))
                for run in p.runs:run.font.size=Pt(8.5)
        for r in p.runs:r.font.color.rgb=RGBColor(0,0,0)
    for root in [doc.element,doc.styles.element]:
        for b in list(root.iter(qn('w:pBdr'))):b.getparent().remove(b)
    if md.parent.name=='week3':format_week3_portrait(doc)
    out=Path(out);out.parent.mkdir(parents=True,exist_ok=True);doc.save(out)

def format_week3_portrait(doc):
    for sec in doc.sections:
     sec.orientation=WD_ORIENT.PORTRAIT;sec.page_width=Mm(210);sec.page_height=Mm(297)
     sec.top_margin=Mm(16);sec.bottom_margin=Mm(16);sec.left_margin=Mm(17);sec.right_margin=Mm(17)
     sec.header_distance=Mm(7);sec.footer_distance=Mm(8)
    for st in ['Normal','Title','Heading 1','Heading 2','Heading 3']:
     style=doc.styles[st];style.font.name='Arial Unicode MS';style.element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'),'Arial Unicode MS')
    doc.styles['Normal'].paragraph_format.line_spacing=1.15;doc.styles['Normal'].paragraph_format.space_after=Pt(5)
    # Keep code examples on a page as a block; preserve the current Word's content.
    for p in doc.paragraphs:
     if p.style.name.startswith('Heading'):
      p.paragraph_format.keep_with_next=True;p.paragraph_format.keep_together=True
    for idx,table in enumerate(doc.tables):
     table.autofit=False;table.alignment=WD_TABLE_ALIGNMENT.CENTER;n=len(table.columns);header=[c.text for c in table.rows[0].cells]
     if idx==0:widths=[40,58,78]
     elif idx==1:widths=[46,28,102]
     elif idx==2:widths=[18,25,88,45]
     elif idx==3:widths=[18,38,48,12,60]
     elif idx==4:widths=[34,42,62,38]
     elif idx==5:widths=[29,35,49,12,51]
     elif idx in [6,7,10]:widths=[27,65,41,43]
     elif idx in [8,9]:widths=[49,65,31,31]
     else:widths=[176/n]*n
     for col,w in zip(table.columns,widths):col.width=Mm(w)
     pr=table._tbl.tblPr
     for name in ['tblW','tblInd']:
      old=pr.find(qn('w:'+name))
      if old is not None:pr.remove(old)
     tw=OxmlElement('w:tblW');tw.set(qn('w:type'),'dxa');tw.set(qn('w:w'),str(round(176/25.4*1440)));pr.append(tw)
     for j,row in enumerate(table.rows):
      row.height=None;trpr=row._tr.get_or_add_trPr()
      if trpr.find(qn('w:cantSplit')) is None:trpr.append(OxmlElement('w:cantSplit'))
      if j==0 and trpr.find(qn('w:tblHeader')) is None:trpr.append(OxmlElement('w:tblHeader'))
      for k,cell in enumerate(row.cells):
       cell.width=Mm(widths[k]);cell.vertical_alignment=WD_CELL_VERTICAL_ALIGNMENT.TOP;tcpr=cell._tc.get_or_add_tcPr()
       fit=tcpr.find(qn('w:tcFitText'))
       if fit is not None:tcpr.remove(fit)
       old=tcpr.find(qn('w:tcMar'))
       if old is not None:tcpr.remove(old)
       margins=OxmlElement('w:tcMar')
       for name,value in [('top',65),('bottom',65),('left',65),('right',65)]:
        e=OxmlElement('w:'+name);e.set(qn('w:w'),str(value));e.set(qn('w:type'),'dxa');margins.append(e)
       tcpr.append(margins)
       for p in cell.paragraphs:
        p.paragraph_format.space_before=Pt(0);p.paragraph_format.space_after=Pt(1.5);p.paragraph_format.line_spacing=1.1
        p.paragraph_format.keep_with_next=False;p.paragraph_format.keep_together=True;p.paragraph_format.widow_control=True
        for run in p.runs:
         run.font.size=Pt(9 if n<=4 else 8.5);run.font.name='Arial Unicode MS';run._element.get_or_add_rPr().rFonts.set(qn('w:eastAsia'),'Arial Unicode MS')
         if j==0:run.bold=True
    # Retain figure proportions and fit inside the portrait text width.
    for shape in doc.inline_shapes:
     limit=Mm(165)
     if shape.width>limit:
      ratio=limit/shape.width;shape.width=int(limit);shape.height=int(shape.height*ratio)

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]);ap.add_argument('--week',nargs='+',choices=['3','4'],default=['3','4']);a=ap.parse_args()
    for n in a.week:
        p=a.root/f'reports/week{n}'
        word(p/f'week{n}-report.md',p/f'Week{n}实习周报.docx')
        print('Exported',p/f'Week{n}实习周报.docx')
