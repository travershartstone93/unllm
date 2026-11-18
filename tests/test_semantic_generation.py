"""Semantic grounding, real numeric input, records and clock execution."""
from pathlib import Path
import copy
import json
import math
import os
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from upgrade.engine import Engine
from upgrade.model import Unsupported,Ambiguous
from synthesis.language import interpret
from synthesis.behaviour.spec import BehaviourSpec
from synthesis.behaviour.lexicon import normalize
from synthesis.behaviour.planner import plan
from synthesis.behaviour.solver import synthesize
from synthesis.behaviour.expressions import evaluate
from synthesis.behaviour.validate import run_cases


class SemanticGenerationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.previous=os.environ.get("UNLLM_ARTIFACTS")
        os.environ["UNLLM_ARTIFACTS"]=str(Path(cls.temp.name)/"jobs")
        cls.engine=Engine(Path(cls.temp.name)/"facts.sqlite")
    @classmethod
    def tearDownClass(cls):
        cls.engine.parser.close();cls.engine.store.db.close()
        if cls.previous is None:os.environ.pop("UNLLM_ARTIFACTS",None)
        else:os.environ["UNLLM_ARTIFACTS"]=cls.previous
        cls.temp.cleanup()

    def meaning(self,question):
        return self.engine.interpret(question,self.engine.context("semantics"),"semantics",programming=True)

    def test_original_requests_resolve_without_rewording(self):
        for request in json.loads((ROOT/"tests/upgrade_requests.json").read_text()):
            spec=interpret(self.meaning(request["question"]))
            self.assertIsInstance(spec,BehaviourSpec,request["question"])
            self.assertIn("bindings",spec.value["interpretation"])
            if request["id"]=="todo":self.assertEqual("records",spec.state["records"]["type"])
            if request["id"]=="stopwatch":self.assertTrue(spec.value["events"])
            if request["id"]=="tip":self.assertIn("prev.amount * prev.rate",spec.actions[0]["ensure"])
            if request["id"]=="text_counter":self.assertIn("chars_view",{v["id"] for v in spec.views})

    def test_lexical_labels_keep_source_meaning_and_spans(self):
        question="Make a to-do list app with add, remove, and save buttons."
        normalized,terms=normalize(question)
        self.assertIn("task list",normalized)
        self.assertEqual({"append","remove","save"},{t["concept"] for t in terms if t["kind"]=="action"})
        for term in terms:
            a,b=term["source_span"];self.assertEqual(question[a:b],term["source"])
        meaning=self.meaning(question)
        actions={ep["lexical_concept"] for ep in meaning.semantics if ep.get("lexical_kind")=="action"}
        self.assertEqual({"append","remove","save"},actions)

    def test_units_bind_direction_and_check_dimensions(self):
        reverse=interpret(self.meaning("Create an app that converts Fahrenheit to Celsius."))
        binding=reverse.value["interpretation"]["bindings"][0]
        self.assertEqual(("fahrenheit","celsius"),(binding["source_unit"],binding["target_unit"]))
        updates,report=synthesize(reverse.actions[0],reverse.state)
        self.assertAlmostEqual(100.0,evaluate(updates["output_value"],{"input_value":212.0,"output_value":0.0}))
        with self.assertRaises((Unsupported,Ambiguous)):
            interpret(self.meaning("Create an app that converts Celsius to a mile."))

    def test_new_percentage_relation_is_synthesized(self):
        fields={n:{"type":"number","initial":0.0} for n in ("base","rate","part","total")}
        updates,report=synthesize({"ensure":"100 * next.part == prev.base * prev.rate and next.total == prev.base - next.part"},fields)
        values={"base":231.2,"rate":7.5,"part":999.0,"total":999.0}
        self.assertAlmostEqual(17.34,evaluate(updates["part"],values))
        self.assertAlmostEqual(213.86,evaluate(updates["total"],values))
        self.assertTrue(report["simultaneous"])
        guarded,proof=synthesize({"ensure":"next.base == 1 and prev.base > 0","requires":"base > 0"},fields)
        self.assertTrue(proof["precondition_encoded"]);self.assertEqual(1.0,evaluate(guarded["base"],values))

    def test_missing_roles_are_not_silently_discarded(self):
        for question in ("Build an app with two counters.",
                         "Build an app with a text area and a list and save buttons.",
                         "Build a calculator with a task list."):
            with self.assertRaises((Unsupported,Ambiguous),msg=question):interpret(self.meaning(question))

    def test_long_compound_request_keeps_numeric_and_clock_roles_separate(self):
        question="Build an app with a text area, a counter that increases by 3, and a clock with start and stop buttons."
        spec=interpret(self.meaning(question))
        self.assertEqual({"elapsed","anchor","running","body","value"},set(spec.state))
        actions={a["id"]:a for a in spec.actions}
        self.assertEqual("next.value == prev.value + 3.0",actions["increment"]["ensure"])
        self.assertEqual("elapsed",actions["start"]["state"])
        self.assertTrue(spec.value["events"])

    def test_record_and_event_types_reject_invalid_bindings(self):
        base={"schema":2,"state":{"items":{"type":"records","fields":{"text":"text","done":"bool"},"initial":[]},
                                  "other":{"type":"records","fields":{"name":"text"},"initial":[]},
                                  "selected":{"type":"number","initial":-1}},"views":[],"actions":[]}
        wrong=copy.deepcopy(base);wrong["actions"]=[{"id":"assign","assign":{"items":"other"}}]
        with self.assertRaises(Unsupported):BehaviourSpec.from_dict(wrong)
        wrong=copy.deepcopy(base);wrong["state"]["items"]["initial"]=[{"text":"item","done":1}]
        with self.assertRaises(Unsupported):BehaviourSpec.from_dict(wrong)
        wrong=copy.deepcopy(base);wrong["events"]=[{"kind":"periodic","interval_ms":0,"action":"missing"}]
        with self.assertRaises(Unsupported):BehaviourSpec.from_dict(wrong)

    def test_plans_achieve_effects_and_require_clock_capabilities(self):
        spec=interpret(self.meaning("Create a stopwatch app with start, stop, and reset buttons."))
        construction=plan(spec,self.engine.native,"python")
        self.assertEqual("running",construction["action_plans"]["start"]["goal"])
        self.assertIn("running",construction["action_plans"]["start"]["achieved"])
        with self.assertRaises(Unsupported):plan(spec,self.engine.native,"cpp",disabled=("behaviour:clock.monotonic",))

    def test_records_persist_load_and_reject_wrong_schema_in_both_languages(self):
        parsed=interpret(self.meaning("Make a to-do list app with add, remove, and save buttons."))
        raw=parsed.data();raw["actions"].append({"id":"load","effect":"load","state":"records"})
        for language in ("python","cpp"):
            result=self.engine.ask({"spec":raw,"language":language,"context":"records-"+language})
            self.assertEqual("validated",result["status"],result["attempts"])
            job=Path(result["job"]);(job/"wrong-schema.json").write_text('[{"unknown":"field"}]')
            cases=[{"events":[{"input":"entry_text_input","text":"  fresh café 🐦  "},{"button":"add"},
                              {"check":"records_view","row":0,"value":True},{"button":"save"},
                              {"select":"records_view","row":0},{"button":"remove"},{"button":"load"}],
                    "dialogs":{"save":["/output/roundtrip.json"],"open":["/output/roundtrip.json"],"confirm":["discard"]},
                    "expect":{"state":{"records":[{"text":"fresh café 🐦","done":True}]},"dirty":False}},
                   {"dialogs":{"open":["/job/wrong-schema.json"]},"events":[{"button":"load"}],
                    "expect":{"state":{"records":[]},"has_error":True}}]
            checked=run_cases(BehaviourSpec.from_dict(raw),job,language,cases,time.monotonic()+35)
            self.assertTrue(checked["valid"],checked)

    def test_clock_real_time_pause_resume_and_reset_in_both_languages(self):
        question="Create a stopwatch app with start, stop, and reset buttons."
        for language in ("python","cpp"):
            result=self.engine.ask({"question":question,"language":language,"context":"clock-"+language})
            self.assertEqual("validated",result["status"],result.get("attempts",result))
            spec=BehaviourSpec.from_dict(result["specification"])
            cases=[{"trace":True,"events":[{"button":"start"},{"wait_ms":120},{"button":"stop"},
                                         {"wait_ms":80},{"button":"start"},{"wait_ms":100},{"button":"stop"},
                                         {"button":"reset"},{"wait_ms":80}],
                    "expect":{"state":{"elapsed":0.0,"running":False},
                              "trace_ranges":[{"at":1,"state":"elapsed","minimum":.03,"maximum":5.0}],
                              "unchanged_between":[[2,3,"elapsed"]]}}]
            checked=run_cases(spec,Path(result["job"]),language,cases,time.monotonic()+35)
            self.assertTrue(checked["valid"],checked)
            trace=checked["observations"][0][0]["trace"]
            self.assertGreater(trace[6]["elapsed"],trace[3]["elapsed"])


if __name__=="__main__":unittest.main()
