from pathlib import Path
import json
import os
import subprocess
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]


class CLITests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.folder=Path(self.temp.name)
        self.env={**os.environ,"UNLLM_FACT_DB":str(self.folder/"facts.sqlite"),
                  "UNLLM_PACKS":str(self.folder/"empty-packs"),
                  "UNLLM_SOCKET":str(self.folder/"upgrade.sock"),"UNLLM_IDLE":"2"}
    def tearDown(self):self.temp.cleanup()
    def run_cli(self,args=(),text=None,timeout=30,**extra):
        env={**self.env,**extra}
        return subprocess.run([str(ROOT/"build/unllm"),"--json",*args],input=text,text=True,
                              capture_output=True,env=env,timeout=timeout)
    def test_interactive_facts_and_no_model(self):
        result=self.run_cli(text="Alice is in Paris.\nParis is in France.\nWhere is Alice?\nWhy?\n",
                            UNLLM_NO_SERVER="1",UNLLM_MODEL="this-model-must-never-be-called")
        self.assertEqual(0,result.returncode,result.stderr)
        answers=[json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(4,len(answers))
        self.assertEqual("supported",answers[2]["truth"])
        self.assertEqual(2,len(answers[2]["answers"]))
        self.assertEqual(str(ROOT/"build/unllm"),answers[2]["server"]["executable"])
    def test_resident_build_and_configuration_identity(self):
        self.env["UNLLM_IDLE"]="60"
        server=subprocess.Popen([str(ROOT/"build/unllm"),"--serve"],env=self.env,
                                stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
        try:
            for _ in range(60):
                if (self.folder/"upgrade.sock").exists():break
                if server.poll() is not None:self.fail(server.stderr.read())
                time.sleep(0.02)
            first=self.run_cli(["Alice is in Paris."],timeout=8)
            reply=json.loads(first.stdout)
            self.assertEqual(server.pid,reply["server"]["pid"])
            second=self.run_cli(["Alice is in Paris."],UNLLM_FACT_DB=str(self.folder/"other.sqlite"))
            reply2=json.loads(second.stdout)
            self.assertNotEqual(server.pid,reply2["server"]["pid"])
        finally:
            server.terminate()
            try:server.wait(timeout=3)
            except subprocess.TimeoutExpired:server.kill();server.wait()
            server.stderr.close()
    def test_structured_spec_and_separate_synthesis_deadline(self):
        spec=self.folder/"spec.json"
        spec.write_text(json.dumps({"input":"numbers","steps":[{"op":"filter","expr":"x>1"}],"output":"sum"}))
        result=self.run_cli(["--code","cpp","--spec",str(spec)],UNLLM_NO_SERVER="1",
                            UNLLM_ARTIFACTS=str(self.folder/"jobs"))
        self.assertEqual(0,result.returncode,result.stderr)
        reply=json.loads(result.stdout)
        self.assertEqual("validated",reply["status"],reply)
        self.assertEqual("cpp",reply["language"])
        self.assertTrue(Path(reply["source"]).exists())
    def test_calculator_retained(self):
        reply=json.loads(self.run_cli(["12 times 7"],UNLLM_NO_SERVER="1").stdout)
        self.assertEqual("calc",reply["branch"])
        self.assertIn("84",reply["text"])
    def test_calculator_application_from_english(self):
        result=self.run_cli(["--code","python","Make a standard calculator app."],UNLLM_NO_SERVER="1",
                            UNLLM_ARTIFACTS=str(self.folder/"jobs"))
        self.assertEqual(0,result.returncode,result.stderr)
        reply=json.loads(result.stdout)
        self.assertEqual("validated",reply["status"],reply)
        self.assertEqual("application",reply["kind"])
        self.assertEqual("calculator",reply["specification"]["program"])
        self.assertTrue(Path(reply["preview"]).exists())
    def test_behaviours_compose_without_an_application_category(self):
        result=self.run_cli(["--code","python",
                             "Build an app with a text area, a counter, and uppercase and save buttons."],
                            UNLLM_NO_SERVER="1",UNLLM_MODEL="this-model-must-never-be-called",
                            UNLLM_ARTIFACTS=str(self.folder/"jobs"))
        self.assertEqual(0,result.returncode,result.stderr)
        reply=json.loads(result.stdout)
        self.assertEqual("validated",reply["status"],reply)
        self.assertEqual("behaviour_program",reply["kind"])
        self.assertFalse(reply["construction"]["application_name_used"])
        self.assertIn("effect.save",reply["construction"]["providers"])
        self.assertEqual({"body","value"},set(reply["specification"]["state"]))
        self.assertTrue(Path(reply["preview"]).exists())
    def test_nested_load_save_plan_survives_the_cli_transport(self):
        result=self.run_cli(["--code","python",
                             "Build a shopping list app with add, delete, open, and save buttons."],
                            UNLLM_NO_SERVER="1",UNLLM_MODEL="this-model-must-never-be-called",
                            UNLLM_ARTIFACTS=str(self.folder/"jobs"))
        self.assertEqual(0,result.returncode,result.stderr)
        reply=json.loads(result.stdout)
        self.assertEqual("validated",reply["status"],reply)
        self.assertEqual("records",reply["specification"]["state"]["records"]["type"])
        plans=reply["construction"]["action_plans"]
        self.assertEqual("loaded_clean",plans["load"]["goal"])
        self.assertIn("write_atomic_utf8",plans["save"]["steps"])
        self.assertTrue(reply["construction"]["derivation"]["valid"])
    def test_interactive_codegen_revision_and_backend_switch(self):
        spec={"input":"numbers","steps":[{"op":"filter","expr":"x>0"}],"output":"sum"}
        text=json.dumps(spec)+'\n:revise {"output":"count"}\n:code python\n'
        result=self.run_cli(["--code","cpp"],text=text,UNLLM_NO_SERVER="1",
                            UNLLM_ARTIFACTS=str(self.folder/"jobs"))
        self.assertEqual(0,result.returncode,result.stderr)
        replies=[json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(["validated"]*3,[r["status"] for r in replies],replies)
        self.assertEqual(["cpp","cpp","python"],[r["language"] for r in replies])
        self.assertEqual("count",replies[1]["specification"]["output"])


if __name__=="__main__":unittest.main()
