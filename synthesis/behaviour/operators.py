"""Parametric operator contracts. Plans are derived from desired effects.

Each operator produces a condition and declares its prerequisites. There is no
ordered procedure or application recipe. Cancellation/errors stop the generated
handler before dependent operators execute.
"""
PERSIST={
    "state":None,
    "discarded":("confirm_discard",("state",)),
    "opened":("choose_open",("discarded",)),
    "loaded":("read_utf8",("opened",)),
    "committed":("commit_state",("loaded",)),
    "load_path":("remember_path",("committed",)),
    "loaded_clean":("mark_saved",("load_path",)),
    "encoded":("encode_state",("state",)),
    "save_path":("choose_existing_or_save",("state",)),
    "new_path":("choose_save",("state",)),
    "written":("write_atomic_utf8",("encoded","save_path")),
    "written_as":("write_atomic_utf8",("encoded","new_path")),
    "saved_path":("remember_path",("written",)),
    "saved_as_path":("remember_path",("written_as",)),
    "saved_clean":("mark_saved",("saved_path",)),
    "saved_as_clean":("mark_saved",("saved_as_path",)),
    "empty":("empty_state",("discarded",)),
    "new_committed":("commit_state",("empty",)),
    "cleared_path":("clear_path",("new_committed",)),
    "new_clean":("mark_saved",("cleared_path",))
}
FINAL={"load":"loaded_clean","save":"saved_clean","save_as":"saved_as_clean","new":"new_clean"}


def contract(effect):
    if effect in FINAL:return PERSIST,FINAL[effect]
    if effect in ("undo","redo","cut","copy","paste","select_all"):
        return {"state":None,"edited":("edit_"+effect,("state",))},"edited"
    if effect in ("append_record","remove_record","set_record"):
        return {"state":None,"updated":(effect,("state",))},"updated"
    if effect=="start_clock":
        return {"state":None,"now":("read_clock",("state",)),
                "anchored":("anchor_clock",("now",)),"running":("enable_clock",("anchored",))},"running"
    if effect=="stop_clock":
        return {"state":None,"now":("read_clock",("state",)),
                "sampled":("update_duration",("now",)),"stopped":("disable_clock",("sampled",))},"stopped"
    if effect=="sample_clock":
        return {"state":None,"now":("read_clock",("state",)),
                "sampled":("update_duration",("now",))},"sampled"
    if effect=="reset_clock":
        return {"state":None,"stopped":("disable_clock",("state",)),
                "zero":("zero_duration",("stopped",))},"zero"
    raise ValueError("Unknown contracted effect: "+effect)


def operators():
    names=set()
    for effect in (*FINAL,"undo","redo","cut","copy","paste","select_all",
                   "append_record","remove_record","set_record","start_clock","stop_clock","sample_clock","reset_clock"):
        graph,_=contract(effect)
        names.update(value[0] for value in graph.values() if value)
    return names
