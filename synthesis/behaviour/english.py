"""ERG analyses are grounded through typed semantic frames and role bindings."""
from upgrade.model import Unsupported,Ambiguous,digest
from .frames import Graph,ground,DATA


def accepts(meaning):
    for eps in meaning.analyses or [meaning.semantics]:
        graph=Graph(eps)
        for ep in eps:
            word=graph.word(ep)
            if word in ('app','application','window','interface','editable'):return True
            if DATA['concepts'].get(word,{}).get('type') in ('collection','elapsed_time','editable_document','interactive_expression','numeric_state'):
                return True
            if word in ('clock','collection'):return True
    return False


def from_analysis(eps):return ground(eps)


def interpret(meaning):
    choices={};errors=[]
    for eps in meaning.analyses or [meaning.semantics]:
        try:
            spec=ground(eps);body=spec.data();body.pop('interpretation',None)
            if 'actions' in body:body['actions']=sorted(body['actions'],key=lambda a:a['id'])
            signature=digest(body)
            choices.setdefault(signature,spec)
        except (Unsupported,Ambiguous) as error:errors.append(error)
    if not choices:raise errors[0] if errors else Unsupported('No complete semantic interpretation.')
    if len(choices)>1:raise Ambiguous('The grammatical readings bind different requirements; specify the intended roles.')
    return next(iter(choices.values()))
