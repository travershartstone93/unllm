#!/usr/bin/env python3
"""Build source-format reachability and extension hints from registered ports and MIME data."""
from collections import defaultdict
import json
import mimetypes
from pathlib import Path
import sys

p=Path(sys.argv[1])
schema=json.loads(p.read_text())
providers=schema['providers']
entry={}
terms=defaultdict(set)
for source in sorted({r['from'] for r in providers if r['from'].startswith('in:')}):
    mime=source[3:]
    ports={source}
    while True:
        expanded=ports|{r['to'] for r in providers if not r.get('effect') and r['from'] in ports and not r['to'].startswith('out:')}
        if expanded==ports: break
        ports=expanded
    entry[mime]=sorted({r['effect'] for r in providers if r.get('effect') and r['from'] in ports})
    if mime=='application/octet-stream': continue
    names={e.lstrip('.') for e in mimetypes.guess_all_extensions(mime)}|{mime.split('/')[-1].removeprefix('x-')}
    for word in names:
        if word.isalnum() and len(word)>1: terms[word.lower()].add(mime)
schema['entry_outcomes']=entry
schema['format_terms']={w:next(iter(values)) for w,values in terms.items() if len(values)==1}
p.write_text(json.dumps(schema,indent=2)+'\n')
