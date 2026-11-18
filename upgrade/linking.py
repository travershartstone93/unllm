"""Dictionary entity linking before grammar analysis.

The dictionary is source data. Matching only replaces a registered name with
one opaque proper-name token; it does not recognize question patterns.
"""
from .model import TYPE_WORDS,key


def words(text):
    out=[]
    index=0
    while index<len(text):
        if not text[index].isalnum():
            index+=1;continue
        end=index+1
        while end<len(text) and (text[end].isalnum() or text[end] in ("'","-")):
            end+=1
        out.append((key(text[index:end]),index,end))
        index=end
    return out


def link(text,aliases):
    trie={}
    for alias in aliases:
        tokens=words(alias)
        if not tokens or (len(tokens)==1 and tokens[0][0] in TYPE_WORDS):continue
        node=trie
        for word,_,_ in tokens:node=node.setdefault(word,{})
        node[""]=True
    tokens=words(text)
    result=[];names={};cursor=0;index=0
    while index<len(tokens):
        node=trie;end=index;best=None
        while end<len(tokens) and tokens[end][0] in node:
            if end>index and text[tokens[end-1][2]:tokens[end][1]].strip():
                break
            node=node[tokens[end][0]];end+=1
            if "" in node:best=end
        if best is None:index+=1;continue
        start,finish=tokens[index][1],tokens[best-1][2]
        # All letters keep the placeholder a single ERG proper-name token.
        n=len(names);suffix=""
        while True:
            suffix=chr(97+n%26)+suffix;n=n//26-1
            if n<0:break
        placeholder="Zzunllmname"+suffix
        names[key(placeholder)]=text[start:finish]
        result.extend([text[cursor:start],placeholder])
        cursor=finish;index=best
    result.append(text[cursor:])
    return "".join(result),names
