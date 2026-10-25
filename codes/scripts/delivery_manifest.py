"""Generate/validate a portable delivery inventory. No network/dependencies.
Validator supports precisely the keywords used by the bundled schema.
"""
import argparse, hashlib, json, re
from pathlib import Path, PurePosixPath
from datetime import datetime
from zoneinfo import ZoneInfo
SCHEMA=Path(__file__).resolve().parents[1]/'schemas/delivery-manifest.schema.json'
def check(value, rule, path='$'):
    if 'const' in rule and (type(value)!=type(rule['const']) or value!=rule['const']): raise ValueError(path+': const')
    if 'enum' in rule and value not in rule['enum']: raise ValueError(path+': enum')
    kind=rule.get('type')
    types={'object':dict,'array':list,'string':str,'integer':int}
    if kind and type(value) is not types[kind]: raise ValueError(path+': type')
    if kind=='object':
        if set(rule.get('required',[]))-value.keys(): raise ValueError(path+': missing field')
        if rule.get('additionalProperties') is False and value.keys()-rule['properties'].keys(): raise ValueError(path+': unknown field')
        for k,v in value.items(): check(v,rule['properties'][k],path+'.'+k)
    if kind=='array':
        if len(value)<rule.get('minItems',0): raise ValueError(path+': empty')
        for i,v in enumerate(value): check(v,rule['items'],path+f'[{i}]')
    if kind=='string' and 'pattern' in rule and not re.search(rule['pattern'],value): raise ValueError(path+': pattern')
    if kind=='string' and rule.get('format')=='date-time':
        try: datetime.fromisoformat(value)
        except ValueError as exc: raise ValueError(path+': date-time') from exc
    if kind=='integer' and value<rule.get('minimum',value): raise ValueError(path+': range')
def validate(data,root=None):
    check(data,json.loads(SCHEMA.read_text()))
    paths=[x['path'] for x in data['files']]
    if any(PurePosixPath(p).as_posix()!=p for p in paths): raise ValueError('noncanonical path')
    if len(set(paths))!=len(paths): raise ValueError('duplicate file')
    if root is not None:
        root=Path(root).resolve()
        for item in data['files']:
            p=(root/item['path']).resolve()
            if not p.is_relative_to(root): raise ValueError('path escape')
            b=p.read_bytes()
            if len(b)!=item['size'] or hashlib.sha256(b).hexdigest()!=item['sha256']: raise ValueError('file mismatch: '+item['path'])
    return data
def generate(root,group):
    root=Path(root)
    files=[{'path':p.relative_to(root).as_posix(),'size':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(root.rglob('*')) if p.is_file() and p.name!='delivery-manifest.json']
    return validate({'schema_version':1,'plan_group':group,'generated_at':datetime.now(ZoneInfo('Asia/Shanghai')).isoformat(),'files':files,'image_format':'M7IM-v1','approval_status':'pending-teacher-review'},root)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--group',choices=['week1-2','week3','week4','week3-4']);a=p.parse_args()
    if a.group: (a.root/'delivery-manifest.json').write_text(json.dumps(generate(a.root,a.group),ensure_ascii=False,indent=2)+'\n')
    else: validate(json.loads((a.root/'delivery-manifest.json').read_text()),a.root)
    print('MANIFEST PASS')
