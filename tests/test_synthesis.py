"""Check synthesis against behavior, grammar meaning and isolation boundaries."""
from pathlib import Path
import os
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from upgrade.engine import Engine
from upgrade.model import Unsupported,Ambiguous
from synthesis.spec import Program,expression
from synthesis.solver import synthesize_expression
from synthesis.language import interpret
from synthesis.validate import run_isolated


class SynthesisTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        cls.engine=Engine(Path(cls.temp.name)/"facts.sqlite")
        cls.old=os.environ.get("UNLLM_ARTIFACTS")
        os.environ["UNLLM_ARTIFACTS"]=str(Path(cls.temp.name)/"jobs")
    @classmethod
    def tearDownClass(cls):
        cls.engine.parser.close();cls.engine.store.db.close()
        if cls.old is None:os.environ.pop("UNLLM_ARTIFACTS",None)
        else:os.environ["UNLLM_ARTIFACTS"]=cls.old
        cls.temp.cleanup()

    def test_expression_parser_has_no_code_execution(self):
        for value in ('__import__("os").system("id")','x.__class__','open("/tmp/test")','[x for x in []]'):
            with self.assertRaises(Unsupported):expression(value)
        with self.assertRaises(Unsupported):
            Program.from_dict({"input":"lines","steps":[{"op":"map","expr":"x*2"}]})
    def test_solver_constructs_equivalent_scalar(self):
        target=expression("3*x+2")
        result,report=synthesize_expression(target)
        for x in (-101.25,-1,0,0.125,5,1923.5):
            self.assertAlmostEqual(target.evaluate(x),result.evaluate(x))
        self.assertEqual("cvc5",report["engine"])
    def test_specification_rejects_lost_fields_and_wrong_types(self):
        for spec in (
            {"input":{"kind":"records","format":"csv"}},
            {"input":"numbers","steps":[{"op":"sort","descending":"false"}]},
            {"input":"numbers","steps":[{"op":"map","expr":"x>0"}]},
            {"input":"numbers","steps":[{"op":"filter","expr":"x+1"}]},
            {"input":"numbers","steps":[{"op":"sort","expr":"x+1"}]},
            {"input":"numbers","steps":[{"op":"map","expr":{"op":"add","args":[]}}]}):
            with self.assertRaises(Unsupported):Program.from_dict(spec)
    def test_english_preserves_repetition_and_rejects_unrepresented_numbers(self):
        q="Write a program that reads numbers, doubles them, and doubles them."
        meaning=self.engine.interpret(q,self.engine.context("repetition"),"repetition")
        try:program=interpret(meaning)
        except Ambiguous:pass  # An explicit clarification is safe; silently dropping a double is not.
        else:self.assertEqual(["map","map"],[s.op for s in program.steps])
        for q in ("Write a program that reads numbers and prints the sum of 2 and 3.",
                  "Write a program that reads numbers and keeps duplicates.",
                  "Write a program that reads lines and removes empty lines."):
            with self.assertRaises(Unsupported):
                interpret(self.engine.interpret(q,self.engine.context("rejected"),"rejected"))
    def test_grammar_to_specification(self):
        examples=[
            ("Write a program that reads numbers and prints their sum.","numbers",[],"sum"),
            ("Write a program that reads lines, removes duplicates, and sorts them.","lines",["unique","sort"],"values"),
            ("Write a program that counts words.","lines",["words"],"count"),
            ("Write a program that reads numbers, doubles them, and prints their average.","numbers",["map"],"mean"),
            ("Write a program that groups records by key and prints the sum for each group.","records",[],"sum")]
        for text,kind,steps,output in examples:
            meaning=self.engine.interpret(text,self.engine.context("language"),"language")
            program=interpret(meaning)
            self.assertEqual((kind,steps,output),(program.input,[s.op for s in program.steps],program.output))
    def test_both_languages_on_independent_input(self):
        spec={"input":"records","steps":[{"op":"filter","expr":"x>=0"},{"op":"map","expr":"x*2"}],"output":"stats"}
        for language in ("python","cpp"):
            result=self.engine.ask({"spec":spec,"language":language,"context":"code"})
            self.assertEqual("validated",result["status"],result["attempts"])
            self.assertGreaterEqual(result["meaningful_lines"],60)
            command=["/usr/bin/python3","/job/program.py"] if language=="python" else ["/job/program"]
            output=run_isolated(Path(result["job"]),command,"Gamma\t7\nGamma\t-9\nDelta\t2\nGamma\t3\n")
            self.assertEqual(0,output.returncode,output.stderr)
            self.assertEqual("key\tcount\tsum\tmin\tmax\tmean\nDelta\t1\t4\t4\t4\t4\nGamma\t2\t20\t6\t14\t10\n",output.stdout)
            over_budget=run_isolated(Path(result["job"]),command,"Gamma\t1\n"*100001)
            self.assertEqual(2,over_budget.returncode)
            self.assertEqual("",over_budget.stdout)
    def test_floating_point_counterexample_repairs_the_ir(self):
        result=self.engine.ask({"spec":{"input":"numbers","steps":[
            {"op":"map","expr":"(x+1000000000000)-1000000000000"}],"output":"values"},
            "language":"python","context":"repair"})
        self.assertEqual("validated",result["status"])
        self.assertEqual(2,len(result["attempts"]))
        self.assertIn("repair",result["attempts"][0])
        self.assertFalse(result["attempts"][0]["valid"])
        self.assertTrue(result["attempts"][1]["valid"])
    def test_requirement_change_regenerates_and_checks(self):
        context="revision"
        read_locks=[]
        first=self.engine.ask({"spec":{"input":"numbers","steps":[{"op":"sort"}],"output":"values"},
                               "language":"python","context":context,
                               "_progress":lambda _:read_locks.append(self.engine.store.db.in_transaction)})
        self.assertEqual([False],read_locks)
        second=self.engine.ask({"question":':revise {"steps":[{"op":"sort","descending":true}]}',"context":context})
        self.assertEqual("validated",first["status"]);self.assertEqual("validated",second["status"])
        self.assertNotEqual(first["job"],second["job"])
        result=run_isolated(Path(second["job"]),["/usr/bin/python3","/job/program.py"],"3\n1\n2\n")
        self.assertEqual("3\n2\n1\n",result.stdout)
    def test_candidate_cannot_write_checkout_or_use_host_network(self):
        job=Path(self.temp.name)/"isolation";job.mkdir(exist_ok=True)
        code='''import os, socket
assert not os.path.exists("/home/whisper/repo-work/unllm")
try:
    open("/job/forbidden","w").write("bad")
    raise AssertionError("job mount is writable")
except OSError:
    pass
sock=socket.socket()
sock.settimeout(0.1)
try:
    sock.connect(("1.1.1.1",80))
    raise AssertionError("host network is visible")
except OSError:
    pass
print("isolated")
'''
        (job/"probe.py").write_text(code)
        result=run_isolated(job,["/usr/bin/python3","/job/probe.py"],"")
        self.assertEqual(0,result.returncode,result.stderr)
        self.assertEqual("isolated\n",result.stdout)
        self.assertFalse((job/"forbidden").exists())


if __name__=="__main__":unittest.main()
