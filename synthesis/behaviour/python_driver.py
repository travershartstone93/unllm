"""External Qt driver for arbitrary state and widget graphs."""
import importlib.util
import json
import sys
from PySide6.QtCore import Qt
from PySide6.QtGui import QKeySequence
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication,QWidget,QPlainTextEdit,QLineEdit,QDoubleSpinBox,QLabel,QPushButton,QListWidget

module=importlib.util.spec_from_file_location("candidate","/job/program.py")
candidate=importlib.util.module_from_spec(module);module.loader.exec_module(candidate)
request=json.load(sys.stdin)
application=QApplication([]);application.setStyle("Fusion")
results=[]


class Dialogs:
    def __init__(self,choices):self.choices={k:list(v) for k,v in choices.items()}
    def take(self,name,default=""):return self.choices[name].pop(0) if self.choices.get(name) else default
    def open_path(self,parent):return self.take("open")
    def save_path(self,parent):return self.take("save")
    def confirm(self,parent,name):return self.take("confirm","cancel")


for index,scenario in enumerate(request["scenarios"]):
    dialogs=Dialogs(scenario.get("dialogs",{}))
    window=candidate.ApplicationWindow(dialogs)
    if scenario.get("initial"):window.commit(scenario["initial"])
    window.refresh();window.show();application.processEvents()
    trace=[]
    for event in scenario.get("events",[]):
        if "input" in event:
            widget=window.findChild(QPlainTextEdit,event["input"]) or window.findChild(QLineEdit,event["input"])
            if widget is None:raise AssertionError("Missing editable widget")
            widget.setFocus();widget.selectAll()
            text=event["text"]
            if isinstance(widget,QPlainTextEdit):
                # Unicode insertion uses the same Qt edit/undo path as paste.
                widget.insertPlainText(text)
            else:widget.insert(text)
        elif "type" in event:
            widget=window.findChild(QPlainTextEdit,event["type"]) or window.findChild(QLineEdit,event["type"])
            widget.setFocus();QTest.keyClicks(widget,event["text"])
        elif "number" in event:
            widget=window.findChild(QDoubleSpinBox,event["number"])
            widget.setFocus();widget.selectAll()
            QTest.keyClicks(widget,event.get("text",format(event["value"],".15g")))
            QTest.keyClick(widget,Qt.Key.Key_Return)
        elif "button" in event:
            button=window.findChild(QPushButton,event["button"])
            if button is None:raise AssertionError("Missing button")
            QTest.mouseClick(button,Qt.MouseButton.LeftButton)
        elif "key" in event:
            widget=window.findChild(QWidget,event["target"])
            widget.setFocus();QTest.keySequence(widget,QKeySequence(event["key"]))
        elif "select" in event or "check" in event:
            widget=window.findChild(QListWidget,event.get("select",event.get("check")))
            item=widget.item(event["row"])
            if item is None:raise AssertionError("Missing record row")
            QTest.mouseClick(widget.viewport(),Qt.MouseButton.LeftButton,pos=widget.visualItemRect(item).center())
            if "check" in event and (item.checkState()==Qt.CheckState.Checked)!=event["value"]:
                widget.setFocus();QTest.keyClick(widget,Qt.Key.Key_Space)
        elif "wait_ms" in event:QTest.qWait(max(0,min(2000,event["wait_ms"])))
        elif event.get("close"):window.close()
        else:raise AssertionError("Unknown test event")
        application.processEvents()
        if scenario.get("trace"):trace.append({name:window.get(name) for name in window.fields})
    results.append({"state":{name:window.get(name) for name in window.fields},
                    "labels":{label.objectName():label.text() for label in window.findChildren(QLabel) if label.objectName()},
                    "error":window.error_message,"paths":dict(window.paths),"dirty":window.isWindowModified(),
                    "visible":window.isVisible(),"trace":trace})
    if index==0 and request.get("preview"):window.grab().save("/output/preview.png")
    dialogs.confirm=lambda parent,name:"discard"
    window.close();window.deleteLater();application.processEvents()
print(json.dumps(results,allow_nan=False))
