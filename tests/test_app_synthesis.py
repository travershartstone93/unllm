"""Application requirements, recursive planning, generated GUIs and revisions."""
from pathlib import Path
import copy
import hashlib
import json
import os
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from upgrade.engine import Engine
from upgrade.model import Unsupported,Ambiguous
from synthesis.apps.spec import ApplicationSpec
from synthesis.apps.planner import construct_plan
from synthesis.apps.construct import construct
from synthesis.apps.validate import sandbox
from synthesis.apps.ir import set_state,literal
from synthesis.language import interpret


class AppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        cls.engine=Engine(Path(cls.temp.name)/"facts.sqlite")
        cls.previous=os.environ.get("UNLLM_ARTIFACTS")
        os.environ["UNLLM_ARTIFACTS"]=str(Path(cls.temp.name)/"jobs")
    @classmethod
    def tearDownClass(cls):
        cls.engine.parser.close();cls.engine.store.db.close()
        if cls.previous is None:os.environ.pop("UNLLM_ARTIFACTS",None)
        else:os.environ["UNLLM_ARTIFACTS"]=cls.previous
        cls.temp.cleanup()

    def test_requirements_do_not_drop_unknown_fields(self):
        for spec in ({"program":"calculator","operations":[]},
                     {"program":"calculator","operations":["eval"]},
                     {"program":"calculator","memory":"false"},
                     {"program":"calculator","precision":2},
                     {"program":"calculator","title":"bad\nwindow"},
                     {"program":"calculator","network":True}):
            with self.assertRaises(Unsupported):ApplicationSpec.from_dict(spec)

    def test_english_requirements_and_scoped_exclusions(self):
        cases=[
            ("Write a calculator program.",{}),
            ("Make a standard calculator app.",{}),
            ("Create a calculator with memory buttons.",{"memory":True}),
            ("Write a calculator program without division.",{"operations":("add","subtract","multiply")}),
            ("Make a calculator that supports only addition and subtraction.",{"operations":("add","subtract")}),
            ("Create a calculator without memory buttons.",{"memory":False}),
            ("Create a calculator with no memory buttons.",{"memory":False}),
            ("Create a calculator with memory but without multiplication.",{"memory":True,"operations":("add","subtract","divide")}),
        ]
        for question,changes in cases:
            meaning=self.engine.interpret(question,self.engine.context("english"),"english")
            actual=interpret(meaning)
            self.assertIsInstance(actual,ApplicationSpec)
            self.assertEqual(ApplicationSpec(**changes),actual,question)
        for question in ("Make a calculator with a blue display.","Make a scientific calculator.",
                         "Make a calculator that sends results online."):
            with self.assertRaises((Unsupported,Ambiguous)):
                interpret(self.engine.interpret(question,self.engine.context("rejection"),"rejection"))

    def test_planner_decomposes_dependencies_and_checks_wires(self):
        spec=ApplicationSpec(memory=True)
        plan=construct_plan(spec,self.engine.native,"python")
        self.assertIn("state:numeric_memory",plan["providers"])
        self.assertIn("expression:parse",plan["providers"])
        self.assertTrue(plan["derivation"]["valid"])
        self.assertTrue(plan["wire_derivation"]["valid"])
        self.assertTrue(plan["wire_derivation"]["steps"])
        with self.assertRaises(Unsupported):
            construct_plan(spec,self.engine.native,"python",disabled=("app-component:qt_window_python",))
        # A missing Python provider must not prevent the C++ graph from using
        # its separate provider, nor silently select it for Python.
        self.assertTrue(construct_plan(spec,self.engine.native,"cpp",
                        disabled=("app-component:qt_window_python",))["derivation"]["valid"])

    def test_app_ir_rejects_a_state_type_error(self):
        spec=ApplicationSpec()
        app,_=construct(spec,construct_plan(spec,self.engine.native,"python"))
        app.functions[0]=copy.copy(app.functions[0])
        from dataclasses import replace
        app.functions[0]=replace(app.functions[0],body=(set_state("entry",literal(1.0)),))
        with self.assertRaises(Unsupported):app.validate()

    def test_both_generated_apps_on_fresh_qt_interactions(self):
        for language in ("python","cpp"):
            result=self.engine.ask({"question":"Make a standard calculator app.","language":language,"context":"app-"+language})
            self.assertEqual("validated",result["status"],result["attempts"])
            self.assertEqual("application",result["kind"])
            self.assertGreaterEqual(result["attempts"][0]["gui_scenarios"],10)
            self.assertEqual(language=="cpp",result["attempts"][0]["sanitizers"])
            self.assertTrue(Path(result["preview"]).exists())
            for name,digest in result["source_sha256"].items():
                self.assertEqual(digest,hashlib.sha256((Path(result["job"])/name).read_bytes()).hexdigest())
            command=["/runtime/python/bin/python3.12","/job/gui_driver.py"] if language=="python" else ["/job/gui_driver"]
            fresh={"expressions":["81/9-4","(9+5)*3"],"gui":[
                {"events":[{"text":"37/2"},{"key":"Return"}]},
                {"events":[{"text":"7/0"},{"key":"Return"},{"key":"Escape"},{"text":"9-2"},{"key":"Return"}]}]}
            process=sandbox(Path(result["job"]),command,fresh,Path(result["job"])/"test-output",python=language=="python")
            self.assertEqual(0,process.returncode,process.stderr)
            actual=json.loads(process.stdout)
            self.assertEqual([5,42],[r["value"] for r in actual["expressions"]])
            self.assertEqual(["18.5","7"],[r["display"] for r in actual["gui"]])

    def test_revision_changes_app_graph_and_behavior(self):
        first=self.engine.ask({"spec":{"program":"calculator"},"language":"python","context":"app-revision"})
        revised=self.engine.ask({"question":':revise {"memory":true,"operations":["add","subtract"],"precision":6}',
                                 "context":"app-revision"})
        self.assertEqual("validated",first["status"]);self.assertEqual("validated",revised["status"],revised["attempts"])
        self.assertNotEqual(first["job"],revised["job"])
        self.assertNotIn("control:memory",first["construction"]["requirements"])
        self.assertIn("control:memory",revised["construction"]["requirements"])
        observed=revised["attempts"][0]["observations"][0]["gui"][0]["buttons"]
        self.assertIn("memory_add",observed)
        self.assertNotIn("multiply",observed);self.assertNotIn("divide",observed)

    def test_mouse_only_variant_has_no_keyboard_handlers(self):
        result=self.engine.ask({"spec":{"program":"calculator","operations":["multiply"],"keyboard":False,
                                         "parentheses":False,"backspace":False},"language":"python"})
        self.assertEqual("validated",result["status"],result["attempts"])
        self.assertEqual(0,result["structure"]["key_bindings"])
        ir=json.loads((Path(result["job"])/"application.ir.json").read_text())
        ids={w["identifier"] for w in ir["widgets"]}
        self.assertFalse({"lparen","rparen","backspace","add","divide"} & ids)

    def test_factual_names_do_not_hide_programming_concepts(self):
        with tempfile.TemporaryDirectory() as folder:
            engine=Engine(Path(folder)/"facts.sqlite")
            try:
                engine.store.import_document({"source":{"id":"name-collision"},"entities":[
                    {"id":"example:calculator","name":"Calculator"}]})
                result=engine.ask({"question":"Write a calculator program.","language":"python"})
                self.assertEqual("validated",result["status"],result["attempts"])
            finally:engine.parser.close();engine.store.db.close()


if __name__=="__main__":unittest.main()
