"""Construction must depend on contracts, never an application category."""
from pathlib import Path
import copy
import json
import os
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from upgrade.engine import Engine
from upgrade.model import Unsupported,Ambiguous
from synthesis.behaviour.spec import BehaviourSpec
from synthesis.behaviour.planner import plan
from synthesis.behaviour.construct import construct,data
from synthesis.behaviour.solver import synthesize
from synthesis.behaviour.validate import run_cases
from synthesis.language import interpret


def mixed_spec():
    return {"schema":2,"program":"never_registered_as_an_app_type","title":"Workbench",
        "state":{"first":{"type":"number","initial":3},"second":{"type":"number","initial":7},
                 "draft":{"type":"text","initial":""},"archive":{"type":"text","initial":""},
                 "flag":{"type":"bool","initial":False}},
        "views":[{"id":"first_input","kind":"number_input","state":"first"},
                 {"id":"second_input","kind":"number_input","state":"second"},
                 {"id":"draft_input","kind":"text_edit","state":"draft"},
                 {"id":"archive_view","kind":"label","value":"archive"},
                 {"id":"word_count","kind":"label","value":"words(draft)"},
                 {"id":"flag_view","kind":"label","value":"flag"}],
        "actions":[{"id":"exchange","ensure":"next.first == prev.second and next.second == prev.first"},
                   {"id":"solve_relation","ensure":"2 * next.first == prev.first + 6 and next.second == prev.second + 1"},
                   {"id":"capture","assign":{"archive":"upper(strip(draft))","flag":"not flag"}},
                   {"id":"store","effect":"save","state":"archive"}]}


class BehaviourTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        cls.previous=os.environ.get("UNLLM_ARTIFACTS")
        os.environ["UNLLM_ARTIFACTS"]=str(Path(cls.temp.name)/"jobs")
        cls.engine=Engine(Path(cls.temp.name)/"facts.sqlite")

    @classmethod
    def tearDownClass(cls):
        cls.engine.parser.close();cls.engine.store.db.close()
        if cls.previous is None:os.environ.pop("UNLLM_ARTIFACTS",None)
        else:os.environ["UNLLM_ARTIFACTS"]=cls.previous
        cls.temp.cleanup()

    def test_application_label_does_not_select_the_program(self):
        raw=mixed_spec();one=BehaviourSpec.from_dict(raw)
        raw["program"]="a_completely_different_unknown_label";two=BehaviourSpec.from_dict(raw)
        first=plan(one,self.engine.native,"python");second=plan(two,self.engine.native,"python")
        self.assertEqual(first,second)
        ir_one,_=construct(one,first);ir_two,_=construct(two,second)
        self.assertEqual(data(ir_one),data(ir_two))
        self.assertFalse(first["application_name_used"])

    def test_provider_dependencies_are_required(self):
        spec=BehaviourSpec.from_dict(mixed_spec())
        with self.assertRaises(Unsupported):
            plan(spec,self.engine.native,"python",disabled=("behaviour:write_atomic_utf8",))
        self.assertTrue(plan(spec,self.engine.native,"cpp")["wire_derivation"]["valid"])

    def test_relational_synthesis_and_impossible_contract(self):
        spec=BehaviourSpec.from_dict(mixed_spec())
        updates,report=synthesize(spec.actions[1],spec.state)
        self.assertEqual({"first","second"},set(updates))
        self.assertTrue(report["simultaneous"])
        self.assertIn("(/ 1 2)",report["solutions"]["first"])
        with self.assertRaises(Unsupported):
            synthesize({"ensure":"next.first > prev.first and next.first < prev.first"},spec.state,1000)

    def test_unknown_effects_and_unbound_or_unsafe_expressions_reject(self):
        for action in ({"id":"bad","effect":"send_email","state":"draft"},
                       {"id":"bad","assign":{"draft":"open('/etc/passwd')"}},
                       {"id":"bad","assign":{"draft":"unknown_state"}},
                       {"id":"bad","ensure":"next.first == prev.first * prev.second * prev.first"}):
            raw=mixed_spec();raw["actions"]=[action]
            with self.assertRaises(Unsupported):BehaviourSpec.from_dict(raw)
        raw=mixed_spec();raw["network"]=True
        with self.assertRaises(Unsupported):BehaviourSpec.from_dict(raw)

    def test_english_features_compose_without_selecting_an_app_family(self):
        questions=[
            ("Build an app with an editable text area and a word count.",{"words_view"},set()),
            ("Build an app with a counter.",{"number_view"},{"increment","decrement","reset"}),
            ("Build an app with a text area, a counter, and uppercase and save buttons.",
             {"body_view","number_view"},{"increment","upper","save"}),
        ]
        for question,views,actions in questions:
            meaning=self.engine.interpret(question,self.engine.context("english"),"english",programming=True)
            spec=interpret(meaning)
            self.assertIsInstance(spec,BehaviourSpec)
            self.assertTrue(views<={v["id"] for v in spec.views})
            self.assertTrue(actions<={a["id"] for a in spec.actions})
        meaning=self.engine.interpret("Build an app that increases a counter by 7.",
                                      self.engine.context("numeric-english"),"numeric-english",programming=True)
        numeric=interpret(meaning)
        self.assertEqual("next.value == prev.value + 7.0",numeric.actions[0]["ensure"])
        for question in ("Build an app with a blue text area.","Build an app that sends files online."):
            with self.assertRaises((Unsupported,Ambiguous)):
                interpret(self.engine.interpret(question,self.engine.context("bad"),"bad",programming=True))

    def test_unseen_mixed_contract_on_both_backends_and_fresh_inputs(self):
        for language in ("python","cpp"):
            result=self.engine.ask({"spec":mixed_spec(),"language":language,"context":"mixed-"+language})
            self.assertEqual("validated",result["status"],result["attempts"])
            self.assertEqual("behaviour_program",result["kind"])
            self.assertGreaterEqual(len(result["solver"]),2)
            self.assertTrue(result["construction"]["derivation"]["valid"])
            fresh=[
                {"initial":{"first":37,"second":-9},"events":[{"button":"exchange"}],
                 "expect":{"state":{"first":-9,"second":37}}},
                {"initial":{"first":18,"second":100},"events":[{"button":"solve_relation"}],
                 "expect":{"state":{"first":12,"second":101}}},
                {"events":[{"input":"draft_input","text":"  a fresh café note  "},{"button":"capture"},{"button":"store"}],
                 "dialogs":{"save":["/output/fresh-capture.txt"]},
                 "expect":{"state":{"archive":"A FRESH CAFÉ NOTE","flag":True},
                           "files":{"/output/fresh-capture.txt":"A FRESH CAFÉ NOTE"},"dirty":False}},
            ]
            checked=run_cases(BehaviourSpec.from_dict(mixed_spec()),Path(result["job"]),language,fresh,time.monotonic()+40)
            self.assertTrue(checked["valid"],checked)

    def test_editor_is_assembled_and_survives_roundtrip_history_and_cancel(self):
        for language in ("python","cpp"):
            result=self.engine.ask({"question":"Make a basic text editor.","language":language,"context":"editor-"+language})
            self.assertEqual("validated",result["status"],result.get("attempts",result))
            spec=BehaviourSpec.from_dict(result["specification"])
            fresh=[
                {"events":[{"type":"body_view","text":"abc"},{"button":"undo"}],"expect":{"state":{"body":""}}},
                {"events":[{"type":"body_view","text":"abc"},{"button":"undo"},{"button":"redo"}],"expect":{"state":{"body":"abc"}}},
                {"events":[{"input":"body_view","text":"fresh café 🐦\nsecond line"},{"button":"save"},
                           {"input":"body_view","text":"temporary"},{"button":"load"}],
                 "dialogs":{"save":["/output/fresh-document.txt"],"open":["/output/fresh-document.txt"],"confirm":["discard"]},
                 "expect":{"state":{"body":"fresh café 🐦\nsecond line"},"dirty":False,
                           "files":{"/output/fresh-document.txt":"fresh café 🐦\nsecond line"}}},
                {"events":[{"input":"body_view","text":"keep me"},{"button":"new"}],"dialogs":{"confirm":["cancel"]},
                 "expect":{"state":{"body":"keep me"},"dirty":True}},
                {"events":[{"type":"body_view","text":"copy this"},{"button":"select_all"},{"button":"cut"},{"button":"paste"}],
                 "expect":{"state":{"body":"copy this"}}},
            ]
            checked=run_cases(spec,Path(result["job"]),language,fresh,time.monotonic()+40)
            self.assertTrue(checked["valid"],checked)

    def test_revision_uses_the_same_general_construction_path(self):
        raw={"schema":2,"program":"arbitrary_label","state":{"quantity":{"type":"number","initial":2}},
             "views":[{"id":"quantity_view","kind":"label","value":"quantity"}],
             "actions":[{"id":"change","ensure":"next.quantity == prev.quantity + 2"}]}
        first=self.engine.ask({"spec":raw,"language":"python","context":"revise"})
        revised=self.engine.ask({"question":':revise {"actions":[{"id":"change","ensure":"3 * next.quantity == prev.quantity + 4"}]}',"context":"revise"})
        self.assertEqual("validated",first["status"]);self.assertEqual("validated",revised["status"],revised["attempts"])
        self.assertEqual("behaviour_program",revised["kind"])
        checked=run_cases(BehaviourSpec.from_dict(revised["specification"]),Path(revised["job"]),"python",
                          [{"events":[{"button":"change"}],"expect":{"state":{"quantity":2}}}],time.monotonic()+15)
        self.assertTrue(checked["valid"],checked)


if __name__=="__main__":unittest.main()
