#!/usr/bin/env python3
"""Independently compile each catalog registration through the actual planner and exercise its behavior."""
import gzip
from concurrent.futures import ThreadPoolExecutor
import io
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tarfile
import tempfile
import zipfile

here = Path(__file__).resolve().parent
repo = here.parents[3]
bundle = Path(sys.argv[1]).resolve()
schema = json.loads((bundle/'providers/contracts.json').read_text())
cards = json.loads((bundle/'components.json').read_text())
planner = os.getenv('BRICK_PROVIDER_PLANNER',repo/'build/native/brick-provider-plan')
worker = os.getenv('BRICK_LOGIC_WORKER',repo/'build/native/unllm_logic_worker')
cases = {}

def case(name, source, expected, params=None, output='application/json'):
    cases[name] = (source,expected,params or {},output)

def dbbackup(path):
    with sqlite3.connect(path) as db: return db.execute('SELECT label FROM song ORDER BY rowid').fetchall()==[('b',),('a',)]

def dbrows(path):
    with sqlite3.connect(path) as db: return db.execute('SELECT value FROM lines ORDER BY rowid').fetchall()==[(x,) for x in words]

def tarbytes(path):
    with tarfile.open(path) as tar: return tar.getnames()==['binary'] and tar.extractfile('binary').read()==binary

def zipbytes(path):
    with zipfile.ZipFile(path) as archive: return archive.namelist()==['binary'] and archive.read('binary')==binary

words=[' b ','','frog','frog','á.c','frog']
binary=b'a\0\n\xff'
named={'é':['x','x','z'],'empty':[]}
groups=[['four','Å','four'],[],['one']]
xml=b'<root><a x="1" y="2">first</a><a x="3">next</a></root>'
for name,value in [('named_lists',named),('named_sets',{'é':['x','z'],'empty':[]}),('object_keys',['empty','é'])]: case(name,'object',value)
case('nested_lists','nested',groups)
for name,expected in [('xml_names',['a','a']),('xml_text',['first','next']),('xml_paths',['/root/a','/root/a']),('xml_attributes',['x','y','x'])]: case(name,'xml',expected,{'xpath':'/root/a'})
for name,exp,param in [('sort_lines',sorted(words),{}),('reverse_order',words[::-1],{}),('unique_lines',list(dict.fromkeys(words)),{}),
    ('adjacent_unique',[' b ','','frog','á.c','frog'],{}),('trim_lines',['b','','frog','frog','á.c','frog'],{}),
    ('nonempty_lines',[s for s in words if s],{}),('matching_lines',['frog']*3,{'needle':'ro'}),('prefixed_lines',['á.c'],{'prefix':'á'}),
    ('first_items',words[:2],{'count':2}),('last_items',words[-2:],{'count':2}),('byte_lengths',[len(s.encode()) for s in words],{}),
    ('item_count',[len(words)],{})]: case(name,'lines',exp,param)
for name,exp in [('file_names',['a.txt']),('directory_names',['sub']),('recursive_files',['a.txt','sub/b.c']),('symbolic_links',['alias'])]: case(name,'directory',exp)
paths=['dir/../log/a.tar.gz','f','dir/b.log']
for name,exp in [('filter_extension',['dir/b.log']),('basenames',['a.tar.gz','f','b.log']),('filename_stems',['a.tar','f','b']),('extensions',['.gz','','.log']),('parent_paths',['dir/../log','','dir']),('normalize_paths',['log/a.tar.gz','f','dir/b.log'])]:
    case(name,'paths',exp,{'extension':'.log'} if name=='filter_extension' else {})
for name,exp in [('database_tables',['album','song']),('database_views',['titles']),('integrity_check',['ok'])]: case(name,'database',exp)
for name,exp in [('table_columns',['n','label','album']),('primary_keys',['n']),('foreign_tables',['album']),('count_rows',[2]),('text_column',['b','a'])]:
    case(name,'database',exp,{'table':'song',**({'column':'label'} if name=='text_column' else {})})
case('database_backup','database',dbbackup,output='application/vnd.sqlite3')
case('store_text_rows','lines',dbrows,output='application/vnd.sqlite3')
for name,exp in [('archive_members',['dir/','dir/a','link']),('archive_files',['dir/a']),('archive_directories',['dir/']),('archive_symlinks',['link']),('archive_sizes',[len(binary)]),('archive_format',lambda p:'tar' in json.loads(p.read_text())[0].lower())]: case(name,'archive',exp)
case('archive_member_bytes','archive',lambda p:p.read_bytes()==binary,{'member':'dir/a'},'application/octet-stream')
case('pack_tar','binary',tarbytes,output='application/x-tar')
case('pack_zip','binary',zipbytes,output='application/zip')
case('gzip_compress','binary',lambda p:gzip.decompress(p.read_bytes())==binary,output='application/gzip')
mime={'object':'application/json','nested':'application/json','xml':'application/xml','lines':'text/plain','paths':'text/plain',
      'directory':'inode/directory','database':'application/vnd.sqlite3','archive':'application/x-tar','binary':'application/octet-stream'}

def write(path, value): path.write_text(json.dumps(value,ensure_ascii=True))

def build(work, enabled):
    write(work/'picks',[]); write(work/'route',{'rows':[{'role':'r','bound':False}]})
    units={}
    flags_by_card={c["id"]:c["packages"] for c in cards}
    for name in enabled:
        source,_,params,out=cases[name]
        write(work/'spec',{'input_type':mime[source],'roles':[{'id':'r','output_type':out,'content':'whole','multiple':False}]})
        write(work/'req',{'r':{'outcome':name,'parameters':params,'uncovered':[]}})
        p=json.loads(subprocess.check_output([planner,repo,worker,bundle/'providers/contracts.json',bundle/'components.json',
                    work/'picks',work/'spec',work/'route',work/'req']))
        assert not p['gaps'] and len(p['providers'])==1,(name,p['gaps'])
        provision=p['providers'][0]
        assert provision['status']=='complete' and not provision['typed_gaps'],(name,provision['plan']['steps'])
        expected_id=next(c['id'] for c in cards if c.get('operation')==name)
        assert expected_id in provision['bricks'],(name,provision['bricks'])
        (work/(name+'.hpp')).write_text(provision['source'].replace('void component_0(',f'void run_{name}('))
        libraries=tuple(sorted({pkg for id in provision['bricks'] for pkg in flags_by_card[id]}))
        group=(tuple(sorted(provision['headers'])),libraries)
        units.setdefault(group,[]).append(name)
    clients={}
    def compile_group(job):
        index, ((headers,libraries),names)=job
        includes=[f'#include "{name}.hpp"' for name in names]
        dispatch=[f'if(op=="{name}") {{run_{name}(argv[2],argv[3]); return 0;}}' for name in names]
        main=work/f'main-{index}.cpp'
        main.write_text('\n'.join(includes+['int main(int argc,char** argv) try { if(argc!=4)return 2; std::string op=argv[1];',*dispatch,'return 3; } catch(const std::exception& e){ return 1; }']))
        flags=subprocess.check_output(['pkg-config','--cflags','--libs',*libraries],text=True).split() if libraries else []
        client=f'client-{index}'
        subprocess.run(['c++','-std=c++17','-fPIC','-O1','-I'+str(bundle/'headers'),'-I'+str(bundle/'providers'),main,'-o',work/client,*flags],check=True)
        return {name:client for name in names}
    with ThreadPoolExecutor(max_workers=3) as pool:
        for group in pool.map(compile_group,enumerate(units.items())): clients.update(group)
    return clients

def fixtures(w):
    (w/'object').write_text(r'{"\u00e9":["x","x","z"],"empty":[]}')
    write(w/'nested',groups)
    (w/'xml').write_bytes(xml)
    (w/'lines').write_text('\n'.join(words)+'\n')
    (w/'paths').write_text('\n'.join(paths)+'\n')
    (w/'binary').write_bytes(binary)
    d=w/'directory'; (d/'sub').mkdir(parents=True); (d/'a.txt').touch(); (d/'sub/b.c').touch(); (d/'alias').symlink_to('sub')
    with sqlite3.connect(w/'database') as db:
        db.executescript("CREATE TABLE album(id INTEGER PRIMARY KEY); CREATE TABLE song(n INTEGER PRIMARY KEY, label TEXT,album REFERENCES album); INSERT INTO song(n,label) VALUES(2,'a'),(1,'b'); CREATE VIEW titles AS SELECT label FROM song;")
    with tarfile.open(w/'archive','w') as tf:
        for name,kind in [('dir/',tarfile.DIRTYPE),('dir/a',tarfile.REGTYPE),('link',tarfile.SYMTYPE)]:
            t=tarfile.TarInfo(name); t.type=kind
            if kind==tarfile.SYMTYPE:t.linkname='dir/a'
            if kind==tarfile.REGTYPE:t.size=len(binary)
            tf.addfile(t,io.BytesIO(binary) if t.size else None)

with tempfile.TemporaryDirectory() as directory:
    work=Path(directory)
    enabled=[c['operation'] for c in cards if 'operation' in c]
    assert len(enabled)==len(set(enabled)) and set(enabled)<=cases.keys()
    clients=build(work,enabled); fixtures(work)
    def execute(name,source,out):
        args=['bwrap','--die-with-parent','--unshare-all','--ro-bind','/usr','/usr','--symlink','usr/lib','/lib','--symlink','usr/lib','/lib64',
              '--bind',str(work),'/work','--chdir','/work','/work/'+clients[name],name,source,out]
        return subprocess.run(args,timeout=15).returncode
    for name in enabled:
        source,expected,params,out=cases[name]
        dest=work/('out-'+name)
        assert execute(name,source,dest.name)==0,(name,'failed execution')
        assert expected(dest) if callable(expected) else json.loads(dest.read_text())==expected,(name,dest.read_bytes()[:160])
        assert execute(name,'absent','failure')==1,(name,'missing input accepted')
    if 'named_sets' in enabled:
        for bad in [b'{"x":"scalar"}', b'{"x":[1]}', b'{"x":[]} trailing', b'[]']:
            (work/'bad').write_bytes(bad)
            assert execute('named_sets','bad','bad-out')==1,bad
    if 'nested_lists' in enabled:
        (work/'bad').write_text('[["x"],42]')
        assert execute('nested_lists','bad','bad-out')==1
    print(f'{len(enabled)} emitted operations passed independent behavior and missing-input checks; applicable malformed JSON checks passed.')
