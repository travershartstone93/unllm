"""Transactional source claims. Materialized conclusions are never source facts."""
from __future__ import annotations

from contextlib import contextmanager
from pathlib import Path
import json
import sqlite3
import math
from .model import Claim, Relation, RELATIONS, key, digest

SCHEMA = """
PRAGMA foreign_keys=ON;
PRAGMA journal_mode=WAL;
CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS relations(name TEXT PRIMARY KEY,data TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS sources(id TEXT PRIMARY KEY,data TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS entities(id TEXT PRIMARY KEY,name TEXT NOT NULL,type TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS aliases(alias TEXT NOT NULL,entity TEXT NOT NULL REFERENCES entities(id),
                                   PRIMARY KEY(alias,entity));
CREATE TABLE IF NOT EXISTS claims(id TEXT PRIMARY KEY,predicate TEXT NOT NULL,
 subject TEXT NOT NULL,object TEXT NOT NULL,negative INTEGER NOT NULL,
 valid_from INTEGER,valid_to INTEGER,source TEXT NOT NULL REFERENCES sources(id),
 origin TEXT NOT NULL,qualifiers TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS claim_lookup ON claims(predicate,subject,object);
CREATE INDEX IF NOT EXISTS claim_source ON claims(source);
CREATE TABLE IF NOT EXISTS completeness(predicate TEXT NOT NULL,subject TEXT NOT NULL,
 source TEXT NOT NULL REFERENCES sources(id),PRIMARY KEY(predicate,subject,source));
CREATE TABLE IF NOT EXISTS prepared(id TEXT PRIMARY KEY,data TEXT NOT NULL,
 checksum TEXT NOT NULL,created REAL NOT NULL);
"""


class FactStore:
    def __init__(self, path: str | Path):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.db = sqlite3.connect(self.path, timeout=10)
        self.db.row_factory = sqlite3.Row
        self.db.executescript(SCHEMA)
        version = self.db.execute("SELECT value FROM metadata WHERE key='schema'").fetchone()
        if version and version[0] != "1":
            raise ValueError("Unsupported fact-store schema")
        with self.db:
            self.db.execute("INSERT OR IGNORE INTO metadata VALUES('schema','1')")
            self.db.execute("INSERT OR IGNORE INTO metadata VALUES('revision','0')")
            for relation in RELATIONS:
                self.db.execute("INSERT INTO relations VALUES(?,?) ON CONFLICT(name) DO UPDATE SET data=excluded.data",
                                (relation.name, json.dumps(relation.data())))
        if not self.db.execute("SELECT 1 FROM sources WHERE id='unllm:ontology:v1'").fetchone():
            ontology=Path(__file__).resolve().parents[1]/"data/reasoning_ontology.json"
            self.import_document(json.loads(ontology.read_text()))

    @property
    def revision(self):
        return int(self.db.execute("SELECT value FROM metadata WHERE key='revision'").fetchone()[0])

    def relations(self) -> list[dict]:
        return [json.loads(r[0]) for r in self.db.execute("SELECT data FROM relations ORDER BY name")]

    def entities(self) -> dict[str, dict]:
        return {r["id"]: dict(r) for r in self.db.execute("SELECT * FROM entities")}

    def resolve(self, label: str) -> list[dict]:
        direct = self.db.execute("SELECT * FROM entities WHERE id=?", (label,)).fetchall()
        if direct:
            return [dict(r) for r in direct]
        return [dict(r) for r in self.db.execute(
            "SELECT e.* FROM entities e JOIN aliases a ON a.entity=e.id WHERE a.alias=? ORDER BY e.id",
            (key(label),))]

    def source(self, identifier: str) -> dict:
        row = self.db.execute("SELECT data FROM sources WHERE id=?", (identifier,)).fetchone()
        return json.loads(row[0]) if row else {"id": identifier, "title": identifier}

    def claims(self, now: int) -> list[Claim]:
        rows = self.db.execute(
            "SELECT * FROM claims WHERE (valid_from IS NULL OR valid_from<=?) "
            "AND (valid_to IS NULL OR ?<valid_to) ORDER BY id", (now, now))
        return [Claim(**{**dict(r), "negative": bool(r["negative"]),
                         "qualifiers": json.loads(r["qualifiers"])}) for r in rows]

    def completeness(self) -> list[dict]:
        return [dict(r) for r in self.db.execute(
            "SELECT predicate,subject,source FROM completeness ORDER BY predicate,subject,source")]

    def prepared(self,identifier):
        row=self.db.execute("SELECT data,checksum FROM prepared WHERE id=?",(identifier,)).fetchone()
        if not row:return None
        try:data=json.loads(row[0])
        except (TypeError,ValueError):return None
        return data if digest(data)==row[1] else None

    def save_prepared(self,identifier,snapshot):
        import time
        if snapshot.get("status")!="complete":raise ValueError("Cannot publish incomplete materialization")
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO prepared VALUES(?,?,?,?)",
                            (identifier,json.dumps(snapshot,ensure_ascii=False),digest(snapshot),time.time()))
            self.db.execute("DELETE FROM prepared WHERE id NOT IN (SELECT id FROM prepared ORDER BY created DESC LIMIT 3)")

    def import_document(self, document: dict) -> dict:
        if document.get("schema", 1) != 1:
            raise ValueError("Unsupported import schema")
        source = dict(document["source"])
        source_id = source.get("id")
        if not isinstance(source_id, str) or not source_id:
            raise ValueError("A source ID is required")
        entities = list(document.get("entities", []))
        relations = self.relations()
        by_name = {r["name"]: r for r in relations}
        for item in document.get("relations", []):
            relation = Relation(**item).data()
            if relation["name"] in by_name and relation != by_name[relation["name"]]:
                # JSON round trips tuple words as lists.
                if digest(relation) != digest(by_name[relation["name"]]):
                    raise ValueError("Changing a relation definition requires a schema migration")
            by_name[relation["name"]] = relation
        records: list[Claim] = []
        existing = self.entities()
        for entity in entities:
            previous=existing.get(entity.get("id"))
            if previous and previous["type"]=="type" and entity.get("type","entity")!="type":
                raise ValueError("Entity ID collides with a declared type; use a namespaced entity ID: "+entity["id"])
        existing.update({e["id"]: e for e in entities})
        for entity in entities:
            if not all(isinstance(entity.get(k), str) and entity[k] for k in ("id", "name")):
                raise ValueError("Entities require an ID and name")
            if not isinstance(entity.get("type", "entity"), str):
                raise ValueError("Entity type must be a string")
            records.append(Claim(id="type:" + digest([source_id, entity["id"], entity.get("type","entity")]),
                                 predicate="type", subject=entity["id"], object=entity.get("type","entity"),
                                 source=source_id, origin="metadata"))
        for raw in document.get("claims", []):
            item = dict(raw)
            item["source"] = source_id
            item.setdefault("origin", "source")
            item.setdefault("id", "claim:" + digest([source_id, item]))
            claim = Claim(**item)
            relation = by_name.get(claim.predicate)
            if relation is None:
                raise ValueError("Undeclared relation: " + claim.predicate)
            if claim.subject not in existing:
                raise ValueError("Unknown subject: " + claim.subject)
            if relation["range"] == "bool" and claim.object != "true":
                raise ValueError("Boolean predicates use object=true and explicit polarity")
            if relation["range"] in ("number","quantity"):
                try:value=float(claim.object)
                except ValueError:raise ValueError("Numeric facts require a finite decimal; heights are in metres") from None
                if not math.isfinite(value) or abs(value)>1e15:
                    raise ValueError("Numeric facts must be finite and within +/-1e15")
            if relation["range"] not in ("bool", "type", "number", "quantity") and claim.object not in existing:
                raise ValueError("Unknown object: " + claim.object)
            records.append(claim)
        complete = document.get("complete", [])
        for item in complete:
            if item["predicate"] not in by_name or item["subject"] not in existing:
                raise ValueError("Invalid completeness boundary")
        # One transaction replaces a source snapshot. Readers see either version.
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO sources VALUES(?,?)",
                            (source_id, json.dumps(source, ensure_ascii=False, sort_keys=True)))
            for relation in by_name.values():
                self.db.execute("INSERT OR IGNORE INTO relations VALUES(?,?)",
                                (relation["name"], json.dumps(relation, sort_keys=True)))
            for entity in entities:
                self.db.execute("INSERT INTO entities VALUES(?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,type=excluded.type",
                                (entity["id"], entity["name"], entity.get("type", "entity")))
                for alias in [entity["name"], *entity.get("aliases", [])]:
                    self.db.execute("INSERT OR IGNORE INTO aliases VALUES(?,?)", (key(alias), entity["id"]))
            self.db.execute("DELETE FROM claims WHERE source=?", (source_id,))
            self.db.execute("DELETE FROM completeness WHERE source=?", (source_id,))
            for claim in records:
                self.db.execute("INSERT INTO claims VALUES(?,?,?,?,?,?,?,?,?,?)",
                                (claim.id, claim.predicate, claim.subject, claim.object, int(claim.negative),
                                 claim.valid_from, claim.valid_to, claim.source, claim.origin,
                                 json.dumps(claim.qualifiers, sort_keys=True)))
            for item in complete:
                self.db.execute("INSERT INTO completeness VALUES(?,?,?)",
                                (item["predicate"], item["subject"], source_id))
            self.db.execute("UPDATE metadata SET value=CAST(value AS INTEGER)+1 WHERE key='revision'")
        return {"source": source_id, "claims": len(records), "entities": len(entities),
                "revision": self.revision, "snapshot": digest([r.data() for r in records])}
