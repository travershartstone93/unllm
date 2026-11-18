#!/usr/bin/env python3
from pathlib import Path
import argparse
import json
import os
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from upgrade.store import FactStore
from upgrade.importers import import_packs,import_wikidata


def main():
    parser=argparse.ArgumentParser(description="Import source claims into the upgrade's isolated fact store.")
    parser.add_argument("--database",default=os.environ.get("UNLLM_FACT_DB",str(ROOT/".state/facts.sqlite")))
    commands=parser.add_subparsers(dest="command",required=True)
    load=commands.add_parser("import-json");load.add_argument("files",nargs="+")
    packs=commands.add_parser("import-packs")
    packs.add_argument("--packs",default=str(Path.home()/".local/share/unllm/packs"))
    packs.add_argument("--pack",action="append",default=[])
    packs.add_argument("--titles",nargs="+",required=True)
    wiki=commands.add_parser("import-wikidata")
    wiki.add_argument("qids",nargs="+");wiki.add_argument("--depth",type=int,default=1)
    wiki.add_argument("--max-entities",type=int,default=50)
    wiki.add_argument("--offline",action="store_true")
    wiki.add_argument("--cache",default=str(ROOT/".state/wikidata"))
    commands.add_parser("status")
    commands.add_parser("prepare")
    args=parser.parse_args()
    store=FactStore(args.database)
    if args.command=="import-json":
        result=[store.import_document(json.loads(Path(p).read_text())) for p in args.files]
    elif args.command=="import-packs":
        result=import_packs(store,args.packs,args.titles,args.pack)
    elif args.command=="import-wikidata":
        if not 0<=args.depth<=3 or not 1<=args.max_entities<=200:parser.error("Import budget exceeded")
        result=import_wikidata(store,args.qids,Path(args.cache),args.depth,args.max_entities,not args.offline)
    elif args.command=="prepare":
        import time
        from upgrade.native import NativeEngine
        engine=NativeEngine()
        facts=[c.data() for c in store.claims(int(time.time()))]
        relations=store.relations()
        snapshot=engine.snapshot(facts,relations,store.completeness())
        identifier=engine.input_key(facts,relations)
        store.save_prepared(identifier,snapshot)
        result={"snapshot":identifier,"tuples":len(snapshot["closure"]),"status":"complete"}
    else:
        result={"database":str(store.path),"revision":store.revision,
                "sources":store.db.execute("SELECT COUNT(*) FROM sources").fetchone()[0],
                "claims":store.db.execute("SELECT COUNT(*) FROM claims").fetchone()[0],
                "entities":len(store.entities())}
    print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__=="__main__":main()
