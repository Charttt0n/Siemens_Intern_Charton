#!/usr/bin/env python3
"""Validate bundled manifest examples, every field, and real file integrity."""
import copy,json,tempfile,hashlib,os
from datetime import datetime
from zoneinfo import ZoneInfo
from pathlib import Path
from delivery_manifest import generate,validate
ROOT=Path(__file__).resolve().parents[1]
def main():
 out=ROOT/'logs'/datetime.now(ZoneInfo('Asia/Shanghai')).strftime('manifest-%Y%m%d-%H%M%S');out.mkdir(parents=True)
 rows=[]
 with tempfile.TemporaryDirectory(prefix='manifest-') as tmp:
  root=Path(tmp);(root/'README.txt').write_bytes(b'Week3-4 delivery example\n');good=generate(root,'week3-4')
  examples=ROOT/'schemas/examples';examples.mkdir(exist_ok=True)
  # Checked-in examples carry the exact file bytes they inventory.
  (examples/'README.txt').write_bytes((root/'README.txt').read_bytes())
  (examples/'valid.json').write_text(json.dumps(good,ensure_ascii=False,indent=2)+'\n')
  validate(good,root);rows.append(dict(id='MF-VALID',input='valid.json + README.txt',expected='accepted',actual='accepted',status='PASS'))
  def reject(name,mut,expected='ValueError',alter=None):
   data=copy.deepcopy(good);mut(data)
   if alter:alter()
   try:validate(data,root)
   except (ValueError,OSError) as e:
    actual=type(e).__name__+': '+str(e);assert type(e).__name__==expected,(name,actual)
   else:raise AssertionError(name+' accepted')
   rows.append(dict(id='MF-'+name,input=data,expected=expected,actual=actual,status='PASS'))
   (out/(name+'.json')).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
   if name in ['MISSING','TRAVERSAL','HASH']: (examples/('invalid-'+name.lower()+'.json')).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
  for field in good:
   reject('MISSING-'+field.upper(),lambda d,f=field:d.pop(f))
  for name,mut in [
   ('MISSING',lambda d:d.pop('files')),('UNKNOWN',lambda d:d.update(extra=1)),
   ('VERSION',lambda d:d.update(schema_version=2)),('BOOLEAN-VERSION',lambda d:d.update(schema_version=True)),
   ('GROUP',lambda d:d.update(plan_group='week5')),('FORMAT',lambda d:d.update(image_format='M7IM-v2')),
   ('APPROVAL',lambda d:d.update(approval_status='approved')),('DATE',lambda d:d.update(generated_at='2026-02-30T12:00:00+08:00')),
   ('DATE-TEXT',lambda d:d.update(generated_at='2026-10-25Tgarbage+08:00')),('ZONE',lambda d:d.update(generated_at='2026-10-25T12:00:00Z')),
   ('FILES-TYPE',lambda d:d.update(files={})),('EMPTY',lambda d:d.update(files=[])),
   ('FILE-UNKNOWN',lambda d:d['files'][0].update(extra=1)),('HASH',lambda d:d['files'][0].update(sha256='xyz')),
   ('HASH-UPPER',lambda d:d['files'][0].update(sha256='A'*64)),('SIZE',lambda d:d['files'][0].update(size=-1)),
   ('BOOL-SIZE',lambda d:d['files'][0].update(size=True)),('FLOAT-SIZE',lambda d:d['files'][0].update(size=1.5)),
   ('TRAVERSAL',lambda d:d['files'][0].update(path='../escape')),('ABSOLUTE',lambda d:d['files'][0].update(path='/tmp/escape')),
   ('BACKSLASH',lambda d:d['files'][0].update(path='a\\b')),('DRIVE',lambda d:d['files'][0].update(path='C:/escape')),
   ('DOT',lambda d:d['files'][0].update(path='./README.txt')),('DOUBLE-SLASH',lambda d:d['files'][0].update(path='a//b')),
   ('TRAILING-SLASH',lambda d:d['files'][0].update(path='README.txt/')),('NEWLINE',lambda d:d['files'][0].update(path='a\nb')),
   ('DUPLICATE',lambda d:d['files'].append(copy.deepcopy(d['files'][0]))),
   ('TAMPERED',lambda d:d['files'][0].update(sha256='0'*64)),('BAD-LENGTH',lambda d:d['files'][0].update(size=0))]:reject(name,mut)
  for field in ['path','size','sha256']:reject('FILE-MISSING-'+field.upper(),lambda d,f=field:d['files'][0].pop(f))
  reject('MISSING-FILE',lambda d:d['files'][0].update(path='not-found.txt'),expected='FileNotFoundError')
  outside=root.parent/(root.name+'-outside');outside.write_bytes(b'outside')
  try:
   (root/'escape').symlink_to(outside)
   reject('SYMLINK-ESCAPE',lambda d:d['files'][0].update(path='escape'))
  finally:outside.unlink()
  (out/'valid.json').write_text(json.dumps(good,ensure_ascii=False,indent=2)+'\n')
 report=dict(status='PASS',checks=rows)
 (out/'summary.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n');(ROOT/'logs/manifest-latest.txt').write_text(str(out)+'\n');print('PASS',len(rows),'checks',out)
if __name__=='__main__':main()
