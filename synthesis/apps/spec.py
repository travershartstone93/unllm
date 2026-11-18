"""Requirements describe an application, not preselected source code."""
from dataclasses import dataclass,asdict
from upgrade.model import Unsupported

OPERATIONS=("add","subtract","multiply","divide")


@dataclass(frozen=True)
class ApplicationSpec:
    program: str="calculator"
    operations: tuple[str,...]=OPERATIONS
    memory: bool=False
    parentheses: bool=True
    backspace: bool=True
    keyboard: bool=True
    precision: int=12
    title: str="Calculator"
    schema: int=1

    @classmethod
    def from_dict(cls,data):
        allowed=set(cls.__dataclass_fields__)
        if not isinstance(data,dict) or set(data)-allowed:
            raise Unsupported("Unknown application specification fields.")
        if data.get("program")!="calculator" or data.get("schema",1)!=1:
            raise Unsupported("The initial interactive application family is calculator, schema 1.")
        operations=data.get("operations",OPERATIONS)
        if not isinstance(operations,(list,tuple)) or not operations or any(op not in OPERATIONS for op in operations):
            raise Unsupported("Calculator operations must be a nonempty selection of add, subtract, multiply, divide.")
        if len(set(operations))!=len(operations):raise Unsupported("Repeated calculator operations.")
        for field in ("memory","parentheses","backspace","keyboard"):
            if field in data and type(data[field]) is not bool:raise Unsupported(field+" must be boolean.")
        precision=data.get("precision",12)
        if type(precision) is not int or not 3<=precision<=15:
            raise Unsupported("Display precision must be 3 to 15 significant digits.")
        title=data.get("title","Calculator")
        if not isinstance(title,str) or not title or len(title)>80 or any(ord(ch)<32 for ch in title):
            raise Unsupported("The window title must be 1 to 80 printable characters.")
        return cls(**{**data,"operations":tuple(op for op in OPERATIONS if op in operations)})

    def data(self):
        result=asdict(self);result["operations"]=list(self.operations);return result

    def contract(self):
        return {"arithmetic":"binary64; finite inputs, intermediates and results within +/-1e15",
                "evaluation":"algebraic precedence; unary signs; parentheses when enabled",
                "display":f"{self.precision} significant digits; internal results retain 17 digits",
                "errors":"typed parse/domain/division errors; display error, then recover through Clear or new digits",
                "input_budget":{"characters":1024,"tokens":256,"parenthesis_depth":32,"unary_signs":32},
                "editing":"new digits after equals start a new expression; an operator continues the result; backspace after equals/error clears",
                "memory":"M+ and M- evaluate the expression and update a checked memory value; MR recalls; MC clears memory" if self.memory else "disabled"}
