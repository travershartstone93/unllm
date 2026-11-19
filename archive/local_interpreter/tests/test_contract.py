"""Protocol and trust-boundary tests; no local model required for these checks."""
from pathlib import Path
import copy
import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from archive.local_interpreter.interpreter import ModelConfig, extract, ground
from upgrade.model import Unsupported


def requirements():
    return {"text": "none", "metrics": [], "counter": True, "initial_number": 8.5,
            "records": "none", "clock": False, "percentage": "none", "source_unit": "none", "target_unit": "none",
            "actions": [{"operation": "subtract", "target": "value", "amount": 2.25}],
            "unsupported": [], "ambiguities": [], "evidence": ["counter"]}


class InterpretationContractTests(unittest.TestCase):
    def test_model_name_is_required_and_arbitrary(self):
        with self.assertRaises(ValueError):
            ModelConfig("")
        self.assertEqual("any-installed-name:tag", ModelConfig("any-installed-name:tag").model)

    def test_extracted_operands_and_initial_values_reach_typed_state(self):
        spec = ground(requirements(), "A counter starts at 8.5 and subtracts 2.25.")
        self.assertEqual(8.5, spec.state["value"]["initial"])
        self.assertEqual("next.value == prev.value - 2.25", spec.actions[0]["ensure"])

    def test_unsupported_clause_is_not_dropped_when_other_parts_fit(self):
        data = requirements()
        data["unsupported"] = ["synchronize with a server"]
        with self.assertRaises(Unsupported):
            ground(data, "A counter can synchronize with a server.")

    def test_absent_target_and_false_evidence_reject(self):
        data = requirements()
        data["actions"] = [{"operation": "save", "target": "body", "amount": 0}]
        with self.assertRaises(Unsupported):
            ground(data, "A counter.")
        data = requirements()
        data["evidence"] = ["a quote that was never in the request"]
        with self.assertRaises(Unsupported):
            ground(data, "A counter.")

    def test_conflicting_operands_are_not_silently_merged(self):
        data = requirements()
        data["actions"].append({"operation": "subtract", "target": "value", "amount": 99})
        with self.assertRaises(Unsupported):
            ground(data, "A counter.")

    def test_units_still_need_symbolic_dimension_compatibility(self):
        data = requirements()
        data.update(source_unit="celsius", target_unit="mile")
        with self.assertRaises(Unsupported):
            ground(data, "A counter and a conversion.")

    def test_transport_uses_configured_model_and_rejects_truncation(self):
        requests = []
        responses = [
            {"done": True, "done_reason": "stop", "message": {"content": json.dumps(requirements())}},
            {"done": True, "done_reason": "length", "message": {"content": json.dumps(requirements())}},
        ]
        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                requests.append((self.path, json.loads(self.rfile.read(int(self.headers["Content-Length"])))))
                body = json.dumps(responses.pop(0)).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            def log_message(self, *args):
                pass
        server = HTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            config = ModelConfig("a-test-model-that-is-not-a-default", f"http://127.0.0.1:{server.server_port}")
            valid = extract("A counter starts at 8.5 and subtracts 2.25.", config)
            truncated = extract("A counter starts at 8.5 and subtracts 2.25.", config)
        finally:
            server.shutdown()
            thread.join(timeout=5)
            server.server_close()
        self.assertEqual("accepted", valid["status"])
        self.assertEqual("error", truncated["status"])
        self.assertNotIn("specification", truncated)
        path, request = requests[0]
        self.assertEqual("/api/chat", path)
        self.assertEqual(config.model, request["model"])
        self.assertFalse(request["think"])
        self.assertFalse(request["format"]["additionalProperties"])
        self.assertEqual(2, len(request["messages"]))
        self.assertNotIn("expected", request)


if __name__ == "__main__":
    unittest.main()
