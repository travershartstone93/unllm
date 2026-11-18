"""Load the real C++/SWI-Prolog/Souffle bridge; no Python inference fallback."""
from __future__ import annotations
import ctypes
import json
import os
from pathlib import Path
from collections import defaultdict
from .model import digest, Unsupported

ROOT = Path(__file__).resolve().parents[1]


class NativeEngine:
    def __init__(self, root: Path = ROOT):
        os.environ["SWI_HOME_DIR"] = str(root / ".deps/native/usr/lib/swipl")
        runtime = root / ".deps/native/usr/lib/swipl/lib/x86_64-linux/libswipl.so"
        self.runtime = ctypes.CDLL(str(runtime), mode=ctypes.RTLD_GLOBAL)
        self.library_path=Path(os.environ.get("UNLLM_LOGIC_LIBRARY",root/"build/libunllm_logic.so")).resolve()
        self.lib = ctypes.CDLL(str(self.library_path))
        self.lib.unllm_logic_request.argtypes = [ctypes.c_char_p]
        self.lib.unllm_logic_request.restype = ctypes.c_void_p
        self.lib.unllm_logic_free.argtypes = [ctypes.c_void_p]
        self.loaded_key = None
        self.rule_version = digest([(root / p).read_text() for p in
                                    ("reasoning/prolog/rules.pl", "reasoning/souffle/background.dl")])

    @staticmethod
    def check_budget(facts, relations):
        if len(facts) > 100000:
            raise Unsupported("The input fact budget is 100000 per knowledge module.")
        transitive = {r["name"] for r in relations if r.get("transitive")}
        graphs = defaultdict(lambda: defaultdict(set))
        for f in facts:
            if not f.get("negative") and f["predicate"] in transitive:
                a, b = f["subject"], f["object"]
                graphs[f["predicate"]][a].add(b)
                graphs[f["predicate"]][b].add(a)
        bound = len(facts) * 2
        for graph in graphs.values():
            remaining = set(graph)
            while remaining:
                todo = [next(iter(remaining))]
                component = set()
                while todo:
                    node = todo.pop()
                    if node in component:
                        continue
                    component.add(node)
                    todo.extend(graph[node] - component)
                remaining -= component
                bound += len(component) ** 2
                if bound > 500000:
                    raise Unsupported("The background closure may exceed its 500000-tuple budget; split the knowledge module.")

    def input_key(self,facts,relations):
        return digest([self.rule_version,facts,relations])

    def snapshot(self,facts,relations,complete):
        self.check_budget(facts,relations)
        key=digest([self.rule_version,facts,relations,complete])
        result=self._request({"operation":"snapshot","world_key":key,"facts":facts,
                              "relations":relations,"complete":complete})
        if result.get("status")!="complete":
            self.loaded_key=None
            raise RuntimeError(result.get("error","Background preparation failed"))
        self.loaded_key=key
        return result

    def _request(self,request):
        raw = self.lib.unllm_logic_request(json.dumps(request, ensure_ascii=False).encode())
        if not raw:raise RuntimeError("Native reasoning engine returned no result")
        try:return json.loads(ctypes.string_at(raw))
        finally:self.lib.unllm_logic_free(raw)

    def application_plan(self,facts,relations,requirements,language):
        self.check_budget(facts,relations)
        key=digest([self.rule_version,facts,relations,[]])
        result=self._request({"operation":"application_plan","world_key":key,
                              "facts":facts,"relations":relations,"complete":[],
                              "requirements":requirements,"language":language})
        if result["status"]=="error":
            self.loaded_key=None
            raise RuntimeError(result.get("error","Application planning failed"))
        self.loaded_key=key
        return result

    def query(self, facts, relations, complete, goals, limit=100,prepared=None):
        world_key = digest([self.rule_version, facts, relations, complete])
        request = {"world_key": world_key, "goals": goals, "limit": limit}
        if world_key != self.loaded_key:
            self.check_budget(facts, relations)
            request.update(facts=facts, relations=relations, complete=complete)
            if prepared is not None:request["prepared"]=prepared
        result=self._request(request)
        if result["status"] == "error" and result.get("error") == "world snapshot is not loaded":
            self.loaded_key = None
            return self.query(facts,relations,complete,goals,limit,prepared)
        if result["status"] == "error":
            self.loaded_key = None
            raise RuntimeError(result.get("error", "Native inference failed"))
        self.loaded_key = world_key
        return result
