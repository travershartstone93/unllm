"""Typed, serializable boundary objects. No source text is evaluated as code."""
from __future__ import annotations

from dataclasses import asdict, dataclass, field
from hashlib import sha256
import json
import unicodedata


class Unsupported(ValueError):
    pass


class Ambiguous(ValueError):
    pass


def key(text: str) -> str:
    return " ".join(unicodedata.normalize("NFKC", text).casefold().split())


def digest(value) -> str:
    return sha256(json.dumps(value, sort_keys=True, ensure_ascii=False,
                             separators=(",", ":")).encode()).hexdigest()


@dataclass(frozen=True)
class Relation:
    name: str
    domain: str = "entity"
    range: str = "entity"
    transitive: bool = False
    inverse: str = ""
    words: tuple[str, ...] = ()

    def __post_init__(self):
        if not isinstance(self.name,str) or not self.name.isidentifier():
            raise ValueError("Relation names must be identifiers")
        if type(self.transitive) is not bool:
            raise ValueError("Relation transitivity must be boolean")
        if not all(isinstance(t,str) and t for t in (self.domain,self.range)):
            raise ValueError("Relation argument types must be nonempty identifiers")
        if not isinstance(self.inverse,str) or self.inverse and not self.inverse.isidentifier():
            raise ValueError("An inverse relation must be an identifier")
        if not isinstance(self.words,(list,tuple)) or any(not isinstance(w,str) for w in self.words):
            raise ValueError("Relation words must be strings")

    def data(self):
        return asdict(self)


RELATIONS = [
    Relation("located_in", "entity", "place", True, "contains", ("in", "location", "locate")),
    Relation("contains", "place", "entity", False, "located_in", ("contain",)),
    Relation("subclass_of", "type", "type", True, "", ("kind",)),
    Relation("type", "entity", "type", False, "", ()),
    Relation("depends_on", "entity", "entity", True, "", ("depend", "require", "need")),
    Relation("available", "entity", "bool", False, "", ("available",)),
    Relation("blocked", "entity", "bool", False, "", ("blocked",)),
    Relation("author", "work", "agent", False, "authored", ("author",)),
    Relation("authored", "agent", "work", False, "author", ("write",)),
    Relation("birthplace", "person", "place", False, "", ("birthplace", "born")),
    Relation("country", "place", "country", False, "", ("country",)),
    Relation("capital", "country", "place", False, "", ("capital",)),
    Relation("height", "entity", "quantity", False, "", ("height", "tall")),
    Relation("taller_than", "entity", "entity", False, "", ()),
    Relation("population", "place", "number", False, "", ("population",)),
]

TYPE_WORDS = {
    "person": "person", "people": "person",
    "country": "country", "city": "city", "place": "place",
    "book": "work", "novel": "work", "work": "work",
    "program": "program", "application": "program", "library": "program",
    "component": "entity", "thing": "entity",
}


@dataclass(frozen=True)
class Claim:
    id: str
    predicate: str
    subject: str
    object: str
    negative: bool = False
    valid_from: int | None = None
    valid_to: int | None = None
    source: str = "session"
    origin: str = "source"
    qualifiers: dict = field(default_factory=dict)

    def __post_init__(self):
        if not isinstance(self.predicate,str) or not self.predicate.isidentifier():
            raise ValueError("Invalid predicate")
        if not all(isinstance(v, str) and v for v in (self.id, self.subject, self.object)):
            raise ValueError("Claim IDs and arguments must be nonempty strings")
        if any(any(c in value for c in ('"', '\\', '\0', '\n', '\r'))
               for value in (self.id,self.subject,self.object)):
            raise ValueError("Claim symbols must be quote-free identifiers; put display text in entity labels")
        if self.subject.startswith("?") or self.object.startswith("?"):
            raise ValueError("Source claims must be ground")
        if type(self.negative) is not bool:
            raise ValueError("Claim polarity must be boolean")
        if any(value is not None and type(value) is not int for value in (self.valid_from,self.valid_to)):
            raise ValueError("Validity bounds must be integer Unix seconds")
        if set(self.qualifiers)-{"source_location"}:
            raise ValueError("Unsupported semantic qualifiers need an explicit scope adapter")
        if self.valid_from is not None and self.valid_to is not None and self.valid_from >= self.valid_to:
            raise ValueError("Claim validity must be a nonempty half-open interval")
        if self.origin not in ("source", "session", "hypothetical", "metadata"):
            raise ValueError("Invalid claim origin")

    def active(self, now: int) -> bool:
        return ((self.valid_from is None or self.valid_from <= now)
                and (self.valid_to is None or now < self.valid_to))

    def data(self):
        return asdict(self)


@dataclass(frozen=True)
class Goal:
    predicate: str
    subject: str
    object: str
    negative: bool = False

    def __post_init__(self):
        if not isinstance(self.predicate,str) or not self.predicate.isidentifier():
            raise ValueError("A goal needs a declared predicate identifier")
        if not all(isinstance(v,str) and v for v in (self.subject,self.object)):
            raise ValueError("Goal arguments must be nonempty typed symbols")
        if type(self.negative) is not bool:raise ValueError("Goal polarity must be boolean")

    def data(self):
        return asdict(self)


@dataclass
class Meaning:
    act: str
    goals: list[Goal] = field(default_factory=list)
    select: list[str] = field(default_factory=list)
    labels: dict[str, str] = field(default_factory=dict)
    assumptions: list[Goal] = field(default_factory=list)
    count: bool = False
    explain: bool = False
    temporal: str = "present"
    semantics: list[dict] = field(default_factory=list)
    diagnostics: list[str] = field(default_factory=list)
    analyses: list[list[dict]] = field(default_factory=list)

    def signature(self):
        variables = {}
        def normalize(value):
            if value.startswith("?"):
                return variables.setdefault(value, "?v" + str(len(variables)))
            return value
        def ordered(goals):
            return sorted(goals, key=lambda g: (g.predicate,
                "?" if g.subject.startswith("?") else g.subject,
                "?" if g.object.startswith("?") else g.object, g.negative))
        def canonical(goals):
            return [[g.predicate, normalize(g.subject), normalize(g.object), g.negative]
                    for g in ordered(goals)]
        goals, assumptions = canonical(self.goals), canonical(self.assumptions)
        return digest({"act": self.act, "goals": goals,
                       "select": sorted(normalize(s) for s in self.select), "assumptions": assumptions,
                       "count": self.count, "temporal": self.temporal})
