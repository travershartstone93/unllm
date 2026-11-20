#!/usr/bin/env python3
"""Install provenance and typed registrations for the packaged-operation corpus (offline)."""
import json
from pathlib import Path
import shutil
import sys

here = Path(__file__).resolve().parent
bundle, count = Path(sys.argv[1]), int(sys.argv[2])
groups = [(8, 'structure', ['Qt6Core','pugixml'], 'Qt QJsonDocument/QSaveFile and pugixml XPath'),
          (20,'text_ops',['Qt6Core'],'libstdc++ algorithms, string and iostream; Qt QSaveFile'),
          (30,'path_ops',[],'libstdc++ std::filesystem'),
          (40,'sql_ops',['sqlite3','Qt6Core'],'SQLite prepared statements, backup API and Qt QTemporaryFile'),
          (50,'archive_ops',['libarchive','Qt6Core'],'libarchive read/write APIs and Qt QSaveFile')]

def read(name): return json.loads((bundle/name).read_text())
def write(name,data): (bundle/name).write_text(json.dumps(data, indent=2)+'\n')
cards, components = read('catalog.json'), read('components.json')
schema = read('providers/contracts.json')
schema['base_outcomes']=list(schema['outcomes'])
schema['types']['named_lists']='std::map<std::string, std::vector<std::string>>'
write('trained-catalog.json',cards)
rows = [line.split('|') for line in (here/'operations.tsv').read_text().splitlines() if line and not line.startswith('#')]
if len(rows)!=50 or count not in (25,50): raise ValueError('Expected 25 or 50 additions')
registrations = {}
for index,(effect,source,sink,call,params,description) in enumerate(rows[:count]):
    _,family,packages,upstream = next(g for g in groups if index<g[0])
    header = family+'.hpp'
    shutil.copyfile(here/header,bundle/'headers'/header)
    brick = f'x{index+1:02}'
    fields = dict(p.split(':') for p in params.split(',') if p)
    record = {'id':brick,'description':description,'outputs':[description],'tags':[]}
    cards.append(record)
    components.append(dict(record,operation=effect,header=header,source='expanded/'+header,packages=packages,
                           provenance={'implementation':upstream,'binding':call}))
    schema['outcomes'][effect]={'meaning':description,'parameters':fields}
    provider={'id':effect,'brick':brick,'effect':effect,'from':source,'to':sink,'call':call,'header':header,'args':list(fields)}
    schema['providers'].append(provider)
    registrations[effect]=provider
    if source=='in:application/x-tar':
        schema['providers'].append(dict(provider,id=effect+'_zip',**{'from':'in:application/zip'}))

def adapter(base,id,**changes):
    p=dict(registrations[base],id=id,**changes);p.pop('effect',None)
    schema['providers'].append(p)
for effect in ('named_lists','named_sets','nested_lists'):
    adapter(effect,'read_'+effect)
    # Existing JSON writer owns the additional output shapes.
    port=registrations[effect]['to']
    adapter(effect,'write_'+effect,brick='j85',call='bricks::save_groups' if port=='layers' else 'bricks::save_named',
            **{'from':port,'to':'out:application/json'})
for c in components:
    if c['id']=='j85': c['packages']+=['Qt6Core','pugixml']
# Always stage this common adapter header when a j85 shape uses it without a JSON reader.
shutil.copyfile(here/'structure.hpp',bundle/'providers/structure.hpp')
schema['providers'].append(dict(registrations['reverse_order'],id='reverse_groups',**{'from':'layers','to':'layers'}))
adapter('sort_lines','read_lines',call='bricks::lines',**{'from':'in:text/plain','to':'strings'})
adapter('sort_lines','write_lines',call='bricks::save_lines',**{'from':'strings','to':'out:text/plain'})
schema['types']['decoded_bytes']='brick_data::ByteFile'
shutil.copyfile(here/'expand_temp.hpp',bundle/'providers/expand_temp.hpp')
schema['providers'] += [
    {'id':'gzip_intermediate','brick':'g83','from':'in:application/gzip','to':'decoded_bytes','effect':'original_bytes','header':'expand_temp.hpp','call':'bricks::expand_bytes'},
    {'id':'decoded_text','brick':'x09','from':'decoded_bytes','to':'strings','header':'text_ops.hpp','call':'bricks::decoded_lines'}
]
next(c for c in components if c['id']=='g83')['packages'].append('Qt6Core')
write('catalog.json',cards);write('components.json',components);write('providers/contracts.json',schema)
reviews=read('reviews.json');reviews['catalog']=cards;write('reviews.json',reviews)
