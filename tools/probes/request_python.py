"""Independent user interaction probe; imports only the generated application."""
import importlib.util
import json
import math
from pathlib import Path
import sys
from PySide6.QtCore import Qt
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication,QWidget,QPlainTextEdit,QLineEdit,QDoubleSpinBox,QPushButton,QLabel,QListWidget

module=importlib.util.spec_from_file_location("trial_candidate","/job/program.py")
candidate=importlib.util.module_from_spec(module);module.loader.exec_module(candidate)
request=json.load(sys.stdin);app=QApplication([]);app.setStyle("Fusion")


class Dialogs:
    def __init__(self):self.choices={k:list(v) for k,v in request.get("dialogs",{}).items()};self.save_calls=0
    def take(self,key,default=""):return self.choices[key].pop(0) if self.choices.get(key) else default
    def open_path(self,parent):return self.take("open")
    def save_path(self,parent):self.save_calls+=1;return self.take("save")
    def confirm(self,parent,name):return self.take("confirm","cancel")


dialogs=Dialogs();window=candidate.ApplicationWindow(dialogs);window.show();app.processEvents()
checks=[];captures={}


def widget(name,kind=QWidget):
    found=window.findChild(kind,name)
    if found is None:raise AssertionError("Missing visible control: "+name)
    return found


def text_of(control):
    return control.toPlainText() if isinstance(control,QPlainTextEdit) else control.text()


def number_of(control):
    return control.value() if isinstance(control,QDoubleSpinBox) else float(text_of(control).rsplit(": ",1)[-1])


def insert(control,text):
    if any(ord(c)>126 or c in "\n\r" for c in text):
        app.clipboard().setText(text);QTest.keyClick(control,Qt.Key.Key_V,Qt.KeyboardModifier.ControlModifier)
    else:QTest.keyClicks(control,text)


for index,event in enumerate(request["events"]):
    kind=event["kind"]
    try:
        actual=expected=None;passed=True
        if kind in ("enter","append"):
            control=widget(event["target"]);control.setFocus()
            if kind=="enter":QTest.keyClick(control,Qt.Key.Key_A,Qt.KeyboardModifier.ControlModifier)
            else:QTest.keyClick(control,Qt.Key.Key_End,Qt.KeyboardModifier.ControlModifier)
            insert(control,event["text"])
            if isinstance(control,QDoubleSpinBox):QTest.keyClick(control,Qt.Key.Key_Return)
        elif kind=="click":QTest.mouseClick(widget(event["target"],QPushButton),Qt.MouseButton.LeftButton)
        elif kind in ("select","check"):
            control=widget(event["target"],QListWidget);item=control.item(event["row"])
            if item is None:raise AssertionError("Missing row")
            QTest.mouseClick(control.viewport(),Qt.MouseButton.LeftButton,pos=control.visualItemRect(item).center())
            if kind=="check" and (item.checkState()==Qt.CheckState.Checked)!=event["value"]:
                control.setFocus();QTest.keyClick(control,Qt.Key.Key_Space)
        elif kind=="wait":QTest.qWait(event["milliseconds"])
        elif kind=="capture":captures[event["name"]]=number_of(widget(event["target"]))
        elif kind=="close":window.close()
        elif kind=="expect_text":
            actual=text_of(widget(event["target"]));expected=event["value"];passed=actual==expected
        elif kind=="expect_number":
            actual=number_of(widget(event["target"]))
            if "capture" in event:expected=captures[event["capture"]];passed=actual==expected
            elif "greater_than_capture" in event:expected=captures[event["greater_than_capture"]];passed=actual>expected
            elif "minimum" in event:expected=[event["minimum"],event["maximum"]];passed=expected[0]<=actual<=expected[1]
            else:expected=event["value"];passed=math.isclose(actual,expected,rel_tol=1e-10,abs_tol=1e-9)
        elif kind=="expect_items":
            control=widget(event["target"],QListWidget);actual=[control.item(i).text() for i in range(control.count())]
            expected=event["value"];passed=actual==expected
        elif kind=="expect_file":
            actual=Path(event["path"]).read_text(encoding="utf-8");expected=event["value"]
            if isinstance(expected,(dict,list)):actual=json.loads(actual)
            passed=actual==expected
        elif kind=="expect_save_calls":actual=dialogs.save_calls;expected=event["value"];passed=actual==expected
        elif kind=="expect_dirty":actual=window.isWindowModified();expected=event["value"];passed=actual==expected
        elif kind=="expect_visible":actual=window.isVisible();expected=event["value"];passed=actual==expected
        elif kind=="expect_error":actual=bool(widget("status",QLabel).text());expected=event["value"];passed=actual==expected
        else:raise AssertionError("Unknown independent probe event: "+kind)
        app.processEvents()
        if kind.startswith("expect"):
            checks.append({"event":index,"kind":kind,"actual":actual,"expected":expected,"passed":passed})
    except Exception as error:
        checks.append({"event":index,"kind":kind,"error":str(error),"passed":False});break
window.grab().save("/output/independent-preview.png")
dialogs.confirm=lambda parent,name:"discard";window.close()
print(json.dumps({"passed":bool(checks) and all(c["passed"] for c in checks),"checks":checks},ensure_ascii=False,allow_nan=False))
