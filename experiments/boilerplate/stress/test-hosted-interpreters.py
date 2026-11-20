#!/usr/bin/env python3
"""Submit frozen B32 prompts and schemas to the proposed hosted interpreters."""
import argparse
from copy import deepcopy
import json
import os
import re
import time
from pathlib import Path

import requests
from jsonschema import Draft202012Validator

CASES = [f"release-live-{name}" for name in
         ("groups", "build", "finance", "gzip", "inventory", "lines", "queue", "sensors")]
CASES += ["final-create", "final-transfer"]
MODELS = {"groq": "openai/gpt-oss-120b", "gemini": "gemini-3.5-flash-lite",
          "cloudflare": "@cf/meta/llama-3.3-70b-instruct-fp8-fast"}
KEYS = {"groq": ["GROQ_API_KEY"], "gemini": ["GEMINI_API_KEY"],
        "cloudflare": ["CLOUDFLARE_ACCOUNT_ID", "CLOUDFLARE_API_TOKEN"]}


def groq_schema(schema):
    # Strict mode requires a root object and closed property sets. Transfer the
    # dictionary of user-defined method names as entries, restoring it on receipt.
    value = deepcopy(schema)
    definitions = value.pop("$defs", {})
    for branch in value["anyOf"]:
        artifact = branch["properties"].get("artifact", {}).get("properties", {})
        if "methods" in artifact:
            operation = artifact["methods"]["additionalProperties"]
            artifact["methods"] = {"type": "array", "items": {"type": "object",
                "properties": {"name": {"type": "string"}, "operation": operation},
                "required": ["name", "operation"], "additionalProperties": False}}
    result = {"type": "object", "properties": {"interpretation": value},
              "required": ["interpretation"], "additionalProperties": False}
    if definitions:
        result["$defs"] = definitions
    return result


def probe(provider, credentials, prompt, schema):
    model = MODELS[provider]
    messages = [{"role": "user", "content": prompt}]
    if provider == "groq":
        url = "https://api.groq.com/openai/v1/chat/completions"
        headers = {"Authorization": "Bearer " + credentials["GROQ_API_KEY"]}
        messages.insert(0, {"role": "system", "content": "Put the requested JSON in interpretation. "
                         "For header methods send name/operation entries, preserving each method name."})
        body = {"model": model, "messages": messages, "response_format": {"type": "json_schema",
                "json_schema": {"name": "interpretation", "schema": groq_schema(schema), "strict": True}}}
    elif provider == "gemini":
        url = f"https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent"
        headers = {"x-goog-api-key": credentials["GEMINI_API_KEY"]}
        body = {"contents": [{"parts": [{"text": prompt}]}],
                "generationConfig": {"responseMimeType": "application/json", "responseJsonSchema": schema}}
    else:
        account = credentials["CLOUDFLARE_ACCOUNT_ID"]
        if not re.fullmatch("[a-fA-F0-9]{32}", account):
            return {"status": "invalid_account_id"}
        url = f"https://api.cloudflare.com/client/v4/accounts/{account}/ai/run/{model}"
        headers = {"Authorization": "Bearer " + credentials["CLOUDFLARE_API_TOKEN"]}
        body = {"messages": messages, "response_format": {"type": "json_schema", "json_schema": schema}}

    try:
        reply = requests.post(url, json=body, headers=headers, timeout=(10, 120), allow_redirects=False)
    except requests.RequestException as exc:
        return {"status": "transport_error", "error": type(exc).__name__}
    if reply.status_code != 200:
        detail = reply.text[:1800]
        for key in KEYS[provider]:
            detail = detail.replace(credentials[key], "[redacted]")
        return {"status": "http_error", "http": reply.status_code, "message": detail,
                "retry_after": reply.headers.get("Retry-After")}
    try:
        data = reply.json()
        if not isinstance(data, dict):
            raise ValueError("Expected an API response object")
        if provider == "groq":
            value = data["choices"][0]["message"]["content"]
        elif provider == "gemini":
            value = "".join(part.get("text", "") for part in data["candidates"][0]["content"]["parts"]
                            if not part.get("thought"))
        else:
            if data.get("success") is not True:
                return {"status": "provider_error", "errors": data.get("errors")}
            value = data["result"]["response"]
        interpretation = json.loads(value) if isinstance(value, str) else value
        if provider == "groq":
            interpretation = interpretation["interpretation"]
            if not isinstance(interpretation, dict):
                raise ValueError("Expected an interpretation object")
            artifact = interpretation.get("artifact", {})
            if isinstance(artifact, dict) and artifact.get("kind") == "header":
                entries = artifact["methods"]
                methods = {entry["name"]: entry["operation"] for entry in entries}
                if len(methods) != len(entries):
                    raise ValueError("Duplicate method names")
                artifact["methods"] = methods
    except (ValueError, KeyError, IndexError, TypeError) as exc:
        return {"status": "invalid_response", "error": type(exc).__name__}
    error = next(Draft202012Validator(schema).iter_errors(interpretation), None)
    return {"status": "schema_rejected" if error else "needs_semantic_review",
            "schema_error": error.message if error else None, "interpretation": interpretation,
            "usage": data.get("usage") or data.get("usageMetadata") or data.get("result", {}).get("usage")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, help="B32 USB run directory")
    parser.add_argument("output", type=Path, help="new result directory")
    parser.add_argument("--provider", choices=MODELS, action="append")
    parser.add_argument("--case", choices=CASES, action="append")
    parser.add_argument("--repeats", type=int, choices=(1, 2, 3), default=3)
    args = parser.parse_args()
    args.output.mkdir()
    credentials = dict(os.environ)
    if not credentials.get("GEMINI_API_KEY"):
        credentials["GEMINI_API_KEY"] = credentials.get("GOOGLE_API_KEY", "")
    rows = []
    for provider in args.provider or MODELS:
        missing = [name for name in KEYS[provider] if not credentials.get(name)]
        if missing:
            rows.append({"provider": provider, "status": "missing_credentials", "missing": missing})
            continue
        for case in args.case or CASES:
            folder = args.baseline / case / "calls/interpret"
            prompt = (folder / "prompt.txt").read_text()
            schema = json.loads((folder / "schema.json").read_text())
            for repeat in range(1, args.repeats + 1):
                start = time.monotonic()
                result = probe(provider, credentials, prompt, schema)
                row = {"provider": provider, "model": MODELS[provider], "case": case, "repeat": repeat,
                       "seconds": round(time.monotonic() - start, 2), **result}
                rows.append(row)
                (args.output / f"{provider}-{case}-{repeat}.json").write_text(json.dumps(row, indent=2) + "\n")
                if result["status"] != "needs_semantic_review":
                    break
            if result.get("http") in (401, 403, 404, 429) or result["status"] == "invalid_account_id":
                break
    (args.output / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
    for row in rows:
        print(row["provider"], row.get("case", ""), row["status"])
    return int(any(row["status"] != "needs_semantic_review" for row in rows))


if __name__ == "__main__":
    raise SystemExit(main())
