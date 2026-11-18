"""Logical, semantic and state invariants, independent of answer wording."""
from pathlib import Path
import copy
import json
import os
import random
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from upgrade.engine import Engine
from upgrade.model import Claim,Goal,Relation,RELATIONS,Unsupported,Ambiguous
from upgrade.native import NativeEngine
from upgrade.proof import ProofChecker
from upgrade.store import FactStore


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.store=FactStore(Path(self.temp.name)/"facts.sqlite")
    def tearDown(self):
        self.store.db.close();self.temp.cleanup()
    def document(self,**extra):
        return {"source":{"id":"test","title":"Independent test facts"},
                "entities":[{"id":"a","name":"Alpha"},{"id":"b","name":"Beta"}],**extra}
    def test_atomic_source_replacement(self):
        self.store.import_document(self.document(claims=[{"subject":"a","predicate":"located_in","object":"b"}]))
        revision=self.store.revision
        with self.assertRaises(ValueError):
            self.store.import_document(self.document(claims=[{"subject":"a","predicate":"unregistered","object":"b"}]))
        self.assertEqual(revision,self.store.revision)
        self.assertEqual(1,sum(c.source=="test" and c.origin=="source" for c in self.store.claims(100)))
        self.store.import_document(self.document(claims=[]))
        self.assertEqual(0,sum(c.source=="test" and c.origin=="source" for c in self.store.claims(100)))
    def test_half_open_time(self):
        self.store.import_document(self.document(claims=[{"subject":"a","predicate":"located_in","object":"b",
                                                        "valid_from":10,"valid_to":20}]))
        for now,expected in [(9,0),(10,1),(19,1),(20,0)]:
            self.assertEqual(expected,sum(c.source=="test" and c.origin=="source" for c in self.store.claims(now)))
    def test_ambiguous_aliases_are_preserved(self):
        doc=self.document();doc["entities"][0]["aliases"]=["Same"];doc["entities"][1]["aliases"]=["Same"]
        self.store.import_document(doc)
        self.assertEqual(2,len(self.store.resolve("same")))
    def test_source_entities_cannot_overwrite_type_identities(self):
        with self.assertRaises(ValueError):
            self.store.import_document({"source":{"id":"collision"},
                                        "entities":[{"id":"city","name":"Westhaven","type":"city"}]})
        self.assertEqual("type",self.store.resolve("city")[0]["type"])
    def test_unknown_qualifiers_not_discarded(self):
        with self.assertRaises(ValueError):
            Claim("c","located_in","a","b",qualifiers={"only_during":"a dream"})
        with self.assertRaises(ValueError):Claim("c","located_in","?a","b")
        with self.assertRaises(ValueError):Claim("c","located_in","a","b",negative="false")
        with self.assertRaises(ValueError):Relation("connected",transitive="false")
    def test_prepared_cache_requires_intact_content(self):
        snapshot={"status":"complete","closure":[["located_in","a","b"]]}
        self.store.save_prepared("one",snapshot)
        self.assertEqual(snapshot,self.store.prepared("one"))
        self.store.db.execute("UPDATE prepared SET data='{}' WHERE id='one'")
        self.assertIsNone(self.store.prepared("one"))
        self.store.db.execute("UPDATE prepared SET data='invalid json' WHERE id='one'")
        self.assertIsNone(self.store.prepared("one"))
        with self.assertRaises(ValueError):self.store.save_prepared("two",{"status":"resource_limit"})


class LogicTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.native=NativeEngine()
    def query(self,claims,goals,complete=()):
        data=[c.data() for c in claims];relations=[r.data() for r in RELATIONS]
        answer=self.native.query(data,relations,list(complete),[g.data() for g in goals],limit=1000)
        for row in answer["solutions"]:
            self.assertTrue(ProofChecker(claims,relations,list(complete)).check(row["proofs"])["valid"])
        return answer
    def test_random_cyclic_graphs_against_bfs(self):
        rng=random.Random(14923)
        for graph_index in range(12):
            nodes=["n"+str(graph_index)+"_"+str(i) for i in range(8)]
            edges={(a,b) for a in nodes for b in nodes if rng.random()<0.13}
            claims=[Claim("e"+str(i),"depends_on",a,b) for i,(a,b) in enumerate(sorted(edges))]
            source=nodes[0];expected=set();todo=[b for a,b in edges if a==source]
            while todo:
                node=todo.pop()
                if node not in expected:
                    expected.add(node);todo.extend(b for a,b in edges if a==node)
            answer=self.query(claims,[Goal("depends_on",source,"?value")])
            self.assertEqual(expected,{r["bindings"]["?value"] for r in answer["solutions"]})
    def test_inverse_and_type_evidence(self):
        claims=[Claim("a","author","book","writer")]
        answer=self.query(claims,[Goal("authored","writer","book"),Goal("type","writer","agent")])
        self.assertTrue(answer["solutions"])
    def test_three_relation_join(self):
        claims=[Claim("a","author","book","writer"),Claim("b","birthplace","writer","city"),
                Claim("c","country","city","nation")]
        answer=self.query(claims,[Goal("author","book","?who"),Goal("birthplace","?who","?where"),
                                  Goal("country","?where","?country")])
        self.assertEqual("nation",answer["solutions"][0]["bindings"]["?country"])
    def test_explicit_negative_and_completeness(self):
        claims=[Claim("a","depends_on","app","runtime"),Claim("b","available","runtime","true",True)]
        self.assertTrue(self.query(claims,[Goal("blocked","app","true")])["solutions"])
        changed=[claims[0],Claim("b2","available","runtime","true")]
        self.assertFalse(self.query(changed,[Goal("blocked","app","true",True)])["solutions"])
        answer=self.query(changed,[Goal("blocked","app","true",True)],
                          [{"predicate":"depends_on","subject":"app"}])
        self.assertTrue(answer["solutions"])
        proof=ProofChecker(changed,[r.data() for r in RELATIONS],[{"predicate":"depends_on","subject":"app"}]).check(
            answer["solutions"][0]["proofs"])
        self.assertEqual({"a","b2"},set(proof["claim_ids"]))
    def test_numeric_comparison_has_checked_premises(self):
        claims=[Claim("a","height","a","1.8"),Claim("b","height","b","1.7")]
        self.assertTrue(self.query(claims,[Goal("taller_than","a","b")])["solutions"])
        self.assertTrue(self.query(claims,[Goal("taller_than","b","a",True)])["solutions"])
        self.assertFalse(self.query(claims[:1],[Goal("taller_than","a","b")])["solutions"])
        result=self.query(claims,[Goal("taller_than","a","b")])
        proof=copy.deepcopy(result["solutions"][0]["proofs"])
        proof[0]["args"][0]="neg"
        with self.assertRaises(ValueError):ProofChecker(claims,[r.data() for r in RELATIONS],[]).check(proof)
    def test_two_python_adapters_cannot_reuse_another_world(self):
        other=NativeEngine()
        first=[Claim("a","located_in","a","b")]
        self.query(first,[Goal("located_in","a","b")])
        other.query([Claim("b","located_in","x","y").data()],[r.data() for r in RELATIONS],[],
                    [Goal("located_in","x","y").data()])
        self.assertTrue(self.query(first,[Goal("located_in","a","b")])["solutions"])
        self.assertFalse(self.query(first,[Goal("located_in","x","y")])["solutions"])
    def test_tampered_proof_rejected(self):
        claims=[Claim("a","located_in","a","b"),Claim("b","located_in","b","c")]
        answer=self.query(claims,[Goal("located_in","a","c")])
        proof=copy.deepcopy(answer["solutions"][0]["proofs"])
        proof[0]["args"][3]="different"
        with self.assertRaises(ValueError):
            ProofChecker(claims,[r.data() for r in RELATIONS],[]).check(proof)
        with self.assertRaises(ValueError):
            ProofChecker(claims,[r.data() for r in RELATIONS],[]).check(
                answer["solutions"][0]["proofs"],[Goal("located_in","a","?value")],{"?value":"wrong"})
    def test_persisted_closure_and_hypothetical_rebuild(self):
        claims=[Claim("a","located_in","a","b"),Claim("b","located_in","b","c")]
        facts=[c.data() for c in claims];relations=[r.data() for r in RELATIONS]
        snapshot=self.native.snapshot(facts,relations,[])
        # Force a different world before loading the persisted one.
        self.native.query([],relations,[],[])
        answer=self.native.query(facts,relations,[],[Goal("located_in","a","c").data()],prepared=snapshot)
        self.assertTrue(answer["used_prepared"])
        self.assertTrue(ProofChecker(claims,relations,[]).check(answer["solutions"][0]["proofs"])["valid"])
        changed=[claims[0],Claim("new","located_in","b","d")]
        self.assertFalse(self.query(changed,[Goal("located_in","a","c")])["solutions"])
        self.assertTrue(self.query(changed,[Goal("located_in","a","d")])["solutions"])


class ConversationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.engine=Engine(Path(self.temp.name)/"facts.sqlite")
    def tearDown(self):
        self.engine.parser.close();self.engine.store.db.close();self.temp.cleanup()
    def ask(self,q,context="one"):
        return self.engine.ask({"question":q,"context":context})
    def test_chains_explanation_and_session_isolation(self):
        self.ask("Alice is in Paris.");self.ask("Paris is in France.")
        answer=self.ask("Where is Alice?")
        self.assertEqual(2,len(answer["answers"]))
        explanation=self.ask("Why?")
        self.assertGreaterEqual(len(explanation["sources"]),2)
        with self.assertRaises(Unsupported):self.ask("Where is Alice?","other")
    def test_correction_and_hypothesis_do_not_rewrite_base(self):
        for q in ("Alice depends on Bob.","Bob is not available.",':complete depends_on Alice'):
            self.ask(q)
        self.assertEqual("supported",self.ask("Is Alice blocked?")["truth"])
        hypothetical=self.ask("What if Bob is available?")
        self.assertTrue(hypothetical["hypothetical"])
        self.assertEqual("refuted",hypothetical["truth"])
        self.assertEqual("supported",self.ask("Is Alice blocked?")["truth"])
        self.ask("No, Bob is available.")
        self.assertEqual("refuted",self.ask("Is Alice blocked?")["truth"])
    def test_conflicting_assertions(self):
        self.ask("Alice is in Paris.");self.ask("Alice is not in Paris.")
        self.assertEqual("both",self.ask("Is Alice in Paris?")["truth"])
    def test_count_and_type_questions_have_the_right_answers(self):
        self.ask("Alice is in Paris.");self.ask("Paris is in France.")
        self.ask("France is a country.")
        answer=self.ask("How many countries contain Alice?")
        self.assertEqual(1,answer["known_count"])
        self.assertFalse(answer["complete_set"])
        self.assertEqual("supported",self.ask("Is France a place?")["truth"])
    def test_new_multiword_proper_names_are_compositional(self):
        self.ask("Clara Morgan is in Paris.")
        self.assertEqual("supported",self.ask("Is Clara Morgan in Paris?")["truth"])
    def test_source_publication_cannot_mix_positive_and_negative_snapshots(self):
        def document(negative,version):
            return {"source":{"id":"changing","title":"Versioned source","version":version},
                    "entities":[{"id":"versioned:lucia","name":"Lucia"}],
                    "claims":[{"predicate":"available","subject":"versioned:lucia",
                               "object":"true","negative":negative}]}
        self.engine.store.import_document(document(False,"1"))
        writer=FactStore(self.engine.store.path)
        original=self.engine.native.query
        published=False
        def interleave(*args,**kwargs):
            nonlocal published
            result=original(*args,**kwargs)
            if not published:
                writer.import_document(document(True,"2"))
                published=True
            return result
        self.engine.native.query=interleave
        try:
            first=self.ask("Is Lucia available?")
            self.assertEqual("supported",first["truth"])
            self.assertEqual({"1"},{s["source"]["version"] for s in first["sources"].values()})
            second=self.ask("Is Lucia available?")
            self.assertEqual("refuted",second["truth"])
            self.assertEqual({"2"},{s["source"]["version"] for s in second["sources"].values()})
        finally:
            self.engine.native.query=original
            writer.db.close()
    def test_unsupported_meaning_is_not_discarded(self):
        self.ask("Alice is in Paris.")
        with self.assertRaises(Unsupported):self.ask("Alice is very available.")
        with self.assertRaises(Unsupported):self.ask("Alice might be available.")
        with self.assertRaises(Unsupported):self.ask("Every person is in Paris.")
        with self.assertRaises(Unsupported):self.ask("Where is Alice?\nBob is available.")


if __name__=="__main__":unittest.main()
