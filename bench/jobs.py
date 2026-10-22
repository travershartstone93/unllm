"""The Round 1 jobs: one instruction per job, shared by Haiku (the reference)
and every small model, and the answer format each must keep to.

A job either picks from a menu ("choices", per job or per item) or writes
a short text ("written"). Items live in DATA/sets/items-<job>.jsonl
(bench/sets.py).
"""

import os

DATA = os.path.expanduser("~/Downloads/unllm-corpus-sources/bench")

JOBS = {
    "turnkind": {
        "choices": ["new question", "follow-up", "correction", "small talk", "not a question"],
        "instruction": (
            "Classify the latest user message in a conversation with an assistant. 'new question': a question "
            "that makes sense on its own, without the conversation. 'follow-up': a question that depends on "
            "the conversation (uses it, that, they, why?, what about..., and yesterday?, tell me more). "
            "'correction': the user corrects or changes what they asked before (no I meant..., actually...). "
            "'small talk': greetings, thanks, chit-chat. 'not a question': anything else."),
    },
    "rewrite": {
        "written": True,
        "instruction": (
            "Rewrite the latest user message as one standalone question that means exactly the same thing "
            "but makes sense without the conversation: replace it/that/they/he/she with what they refer to, "
            "and fill in what a short follow-up leaves out. Do not answer it. Reply with the question only."),
    },
    "article": {
        "instruction": (
            "A search found these articles for the question. Pick the one article whose subject the "
            "question is about (the article most likely to hold the answer). Pick 'none of these' if none "
            "of them is about it."),
    },
    "answertype": {
        "choices": ["person", "date or year", "place", "number or amount", "organization", "title of a work",
                    "thing or concept", "explanation or description", "yes or no"],
        "instruction": "What kind of answer does the question ask for? Pick one.",
    },
    "reword": {
        "written": True,
        "instruction": (
            "A program can answer questions about one Linux laptop from its own records: temperatures, "
            "fans, CPU, memory, disk, GPU, battery, which programs ran and when, crashes, software updates, "
            "and what a running program is. Rewrite the user's message as one short plain question that this "
            "program could answer, keeping its meaning. If the message cannot be answered from such records "
            "(for example a general how-to, coding, or a question about the assistant itself), reply none. "
            "Reply with the question or none, nothing else."),
    },
    "passage": {
        "instruction": (
            "Which numbered passage contains the answer to the question? Pick its number, or 'none' if no "
            "passage contains it."),
    },
    "checker": {
        "instruction": (
            "Does the passage show that the claimed answer is the correct answer to the question? Answer "
            "yes only if the passage supports it; otherwise no."),
    },
}


def schema(job, choices):
    """The JSON shape a reply must have (Ollama 'format')."""
    if JOBS[job].get("written"):
        return {"type": "object", "properties": {"answer": {"type": "string"}}, "required": ["answer"]}
    return {"type": "object", "properties": {"answer": {"type": "string", "enum": choices}}, "required": ["answer"]}


def prompt(item):
    """The user message for one item: the input, then the menu when there is one."""
    p = item["input"]
    if item.get("choices"):
        p += "\n\nChoices:\n" + "\n".join(f"- {c}" for c in item["choices"])
    return p + '\n\nReply as JSON: {"answer": "..."}'
