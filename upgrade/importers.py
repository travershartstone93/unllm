"""Read-only adapters for existing packs and bounded Wikidata JSON imports."""
from __future__ import annotations
from pathlib import Path
from collections import deque
from datetime import datetime, timezone
import json
import sqlite3
import urllib.request
from .model import digest,key

PROPERTY = {"P17":"country","P19":"birthplace","P50":"author","P36":"capital",
            "P131":"located_in","P31":"type","P279":"subclass_of"}
CLASS = {"Q5":"person","Q515":"city","Q6256":"country","Q43229":"organization",
         "Q571":"work","Q8261":"work","Q11424":"work"}
FIELD = {"country":"country","constituent country":"country","sovereign state":"country",
         "author":"author","authors":"author","birth place":"birthplace",
         "birthplace":"birthplace","capital":"capital","location":"located_in"}


def title_key(text):
    value="".join(c.casefold() if c.isalnum() else " " for c in text)
    parts=value.split()
    if parts and parts[0] in ("the","a","an"):parts=parts[1:]
    return " ".join(parts)


def find_documents(packs: Path,titles:list[str],pack_names=()):
    wanted={title_key(t) for t in titles}
    found=[]
    for directory in sorted(packs.iterdir() if packs.exists() else []):
        manifest=directory/"manifest.json"
        if not manifest.exists() or (pack_names and directory.name not in pack_names):continue
        info=json.loads(manifest.read_text())
        if info.get("kind")!="encyclopedia":continue
        for shard in sorted(directory.glob("shard-*.sqlite")):
            db=sqlite3.connect("file:"+str(shard.resolve())+"?mode=ro",uri=True)
            db.row_factory=sqlite3.Row
            try:
                placeholders=",".join("?" for _ in wanted)
                rows=db.execute(f"SELECT * FROM docs WHERE title_key IN ({placeholders})",sorted(wanted))
                for row in rows:
                    fields=[dict(r) for r in db.execute("SELECT box,key,value FROM infobox WHERE doc_id=?",(row["doc_id"],))]
                    extra=json.loads(row["extra"] or "{}")
                    qid=extra.get("wikidata","")
                    identity="wd:"+qid if qid else "pack:"+digest([directory.name,row["title_key"]])[:24]
                    found.append({"id":identity,"qid":qid,"name":row["title"],"row":dict(row),
                                  "fields":fields,"pack":directory.name,"version":info.get("version",""),
                                  "license":info.get("license",""),"shard":shard.name})
            finally:db.close()
    unique={}
    for doc in found:unique.setdefault((doc["pack"],doc["id"]),doc)
    return list(unique.values())


def import_packs(store,packs,titles,pack_names=()):
    documents=find_documents(Path(packs),titles,pack_names)
    aliases={key(d["name"]):d["id"] for d in documents}
    skipped=[]
    catalog=[{"id":d["id"],"name":d["name"],"type":"entity"} for d in documents]
    if catalog:
        store.import_document({"source":{"id":"pack-catalog:"+digest(catalog),
                              "title":"Entity identities from installed reference packs"},
                              "entities":catalog})
    results=[]
    for document in documents:
        claims=[]
        for field in document["fields"]:
            normalized=key(field["key"].replace("_"," "))
            predicate=FIELD.get(normalized)
            if not predicate:continue
            value=field["value"].strip()
            # Existing packs store display text, not a reliable relation graph.
            # Accept an exact unique entity name; qualifiers or lists stay text.
            identity=aliases.get(key(value))
            if not identity:
                matches=store.resolve(value)
                identity=matches[0]["id"] if len(matches)==1 else None
            if not identity:
                skipped.append({"title":document["name"],"field":field["key"],
                                "reason":"not one resolved structured entity"})
                continue
            claims.append({"predicate":predicate,"subject":document["id"],"object":identity,
                "qualifiers":{"source_location":{"shard":document["shard"],
                    "document":document["row"]["doc_id"],"box":field["box"],"field":field["key"]}}})
        source={"id":"pack:"+document["pack"]+":"+document["id"],"title":document["name"],
                "url":document["row"]["url"],"version":document["version"],
                "license":document["license"],"adapter":"exact-infobox-entity-v1"}
        results.append(store.import_document({"source":source,"claims":claims}))
    return {"documents":len(documents),"claims":sum(r["claims"] for r in results),
            "sources":results,"skipped":skipped,"qids":sorted({d["qid"] for d in documents if d["qid"]})}


def import_wikidata(store,qids,cache:Path,depth=1,max_entities=50,fetch=True):
    cache.mkdir(parents=True,exist_ok=True)
    todo=deque((q,0) for q in qids)
    seen=set()
    imported=[]
    skipped=[]
    entities={}
    raw_documents=[]
    while todo and len(seen)<max_entities:
        qid,level=todo.popleft()
        if qid in seen:continue
        if not qid.startswith("Q") or not qid[1:].isdigit():raise ValueError("Invalid Wikidata ID")
        seen.add(qid)
        path=cache/(qid+".json")
        if not path.exists():
            if not fetch:
                skipped.append({"entity":qid,"reason":"not in offline cache"})
                continue
            request=urllib.request.Request("https://www.wikidata.org/wiki/Special:EntityData/"+qid+".json",
                headers={"User-Agent":"unllm-upgrade/0.1 (local symbolic knowledge importer)"})
            with urllib.request.urlopen(request,timeout=30) as response:
                data=response.read(8*1024*1024+1)
            if len(data)>8*1024*1024:raise ValueError("Wikidata entity exceeds input budget")
            parsed=json.loads(data)
            if qid not in parsed.get("entities",{}):raise ValueError("Missing requested entity")
            temporary=path.with_suffix(".part");temporary.write_bytes(data);temporary.replace(path)
        raw=json.loads(path.read_text())["entities"][qid]
        name=raw.get("labels",{}).get("en",{}).get("value",qid)
        aliases=[a["value"] for a in raw.get("aliases",{}).get("en",[])]
        entities["wd:"+qid]={"id":"wd:"+qid,"name":name,"type":"entity","aliases":aliases}
        claims=[]
        for property_id,predicate in PROPERTY.items():
            for record in raw.get("claims",{}).get(property_id,[]):
                if record.get("rank")=="deprecated":continue
                snak=record.get("mainsnak",{})
                if snak.get("snaktype")!="value" or snak.get("datatype")!="wikibase-item":continue
                target=snak.get("datavalue",{}).get("value",{}).get("id")
                if not target:continue
                if record.get("qualifiers"):
                    skipped.append({"entity":qid,"property":property_id,"claim":record.get("id"),
                                    "reason":"qualified assertion needs a supported scope adapter"})
                    continue
                target_id="wd:"+target
                entities.setdefault(target_id,{"id":target_id,"name":target,"type":"entity"})
                claims.append({"id":"wikidata:"+record["id"],"predicate":predicate,
                               "subject":"wd:"+qid,"object":target_id,
                               "qualifiers":{"source_location":{"property":property_id,
                                "statement":record["id"],"rank":record.get("rank"),
                                "references":record.get("references",[])}}})
                if level<depth and property_id not in ("P31","P279"):
                    todo.append((target,level+1))
        raw_documents.append((qid,raw,claims,path))
    # Load all identities before relations so references never become guessed
    # entities. Known class IDs are mapped by explicit ontology declarations.
    for qid,typ in CLASS.items():
        entities.setdefault("wd:"+qid,{"id":"wd:"+qid,"name":qid,"type":"type"})
    existing=store.entities()
    for identifier,entity in entities.items():
        if entity["name"].startswith("Q") and entity["name"][1:].isdigit() and identifier in existing:
            entity["name"]=existing[identifier]["name"]
    catalog=list(entities.values())
    store.import_document({"source":{"id":"wikidata:catalog:"+digest(sorted(seen)),
                          "title":"Wikidata entity labels and aliases","version":"cached"},
                          "entities":catalog})
    mappings=[{"predicate":"subclass_of","subject":"wd:"+q,"object":typ} for q,typ in CLASS.items()]
    store.import_document({"source":{"id":"unllm:wikidata-type-mapping:v1",
                          "title":"Declared mapping of Wikidata classes to unllm types"},
                          "claims":mappings})
    for qid,raw,claims,path in raw_documents:
        source={"id":"wikidata:"+qid,"title":entities["wd:"+qid]["name"],
                "url":"https://www.wikidata.org/wiki/"+qid,
                "version":str(raw.get("lastrevid","")),"license":"CC0",
                "cache_sha256":digest(json.loads(path.read_text())),"adapter":"wikidata-unqualified-items-v1"}
        imported.append(store.import_document({"source":source,"claims":claims}))
    return {"documents":len(imported),"claims":sum(r["claims"] for r in imported),
            "sources":imported,"skipped":skipped,"queued_beyond_budget":len(todo)}
