"""Span-preserving lexical linking before ERG, not prompt rewriting.

Registered domain nouns use grammatical surface nouns. Coordinated button
labels are quoted lexical mentions, not commands: ERG receives a common noun
and the MRS node retains the original action concept and source span.
"""
from pathlib import Path
import json

ROOT=Path(__file__).resolve().parents[2]
DATA=json.loads((ROOT/"data/program_semantics.json").read_text())


def tokens(text):
    result=[];index=0
    while index<len(text):
        if text[index].isspace():index+=1;continue
        start=index
        if text[index].isalnum() or text[index]=="_":
            while index<len(text) and (text[index].isalnum() or text[index] in ("_","'")):index+=1
        else:index+=1
        result.append((text[start:index].casefold(),start,index))
    return result


def normalize(text):
    words=tokens(text);edits={}
    for entry in DATA["aliases"]:
        pattern=[w for w,_,_ in tokens(entry["term"])]
        for index in range(len(words)-len(pattern)+1):
            if [w for w,_,_ in words[index:index+len(pattern)]]==pattern:
                edits[words[index][1]]=(words[index+len(pattern)-1][2],entry["surface"],entry["concept"],"noun")
    for index,(word,_,_) in enumerate(words):
        if word not in ("button","buttons","control","controls"):continue
        cursor=index-1;labels=[]
        while cursor>=0:
            label,start,end=words[cursor]
            if label in (",","and","or"):cursor-=1;continue
            if label not in DATA["action_labels"]:break
            labels.append((start,end,DATA["action_labels"][label]));cursor-=1
        for start,end,concept in labels:edits[start]=(end,"operation",concept,"action")
    output=[];annotations=[];position=0;length=0
    for start,(end,replacement,concept,kind) in sorted(edits.items()):
        if start<position:continue
        prefix=text[position:start];output.append(prefix);length+=len(prefix)
        annotations.append({"start":length,"end":length+len(replacement),"source_span":[start,end],
                            "source":text[start:end],"concept":concept,"kind":kind})
        output.append(replacement);length+=len(replacement);position=end
    output.append(text[position:])
    return "".join(output),annotations


def attach(semantics,annotations):
    for ep in semantics:
        span=ep.get("span")
        if not span or not ep["predicate"].startswith("_"):continue
        matching=[a for a in annotations if span[0]>=a["start"] and span[1]<=a["end"]]
        if matching:
            term=min(matching,key=lambda a:a["end"]-a["start"])
            ep["lexical_concept"]=term["concept"];ep["lexical_kind"]=term["kind"]
            ep["source_term"]=term["source"];ep["source_span"]=term["source_span"]
