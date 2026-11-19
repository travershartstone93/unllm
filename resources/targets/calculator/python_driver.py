"""External Qt Test driver; the generated application has no test-only mode."""
import importlib.util
import json
import sys
from PySide6.QtCore import Qt,qVersion
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication,QPushButton,QLineEdit

spec=importlib.util.spec_from_file_location("generated_app","/job/program.py")
candidate=importlib.util.module_from_spec(spec);spec.loader.exec_module(candidate)
request=json.load(sys.stdin)
application=QApplication([]);application.setStyle("Fusion")
results={"expressions":[],"gui":[],"toolkit":qVersion()}
for text in request["expressions"]:
    try:results["expressions"].append({"value":candidate.evaluate_expression(text),"error":""})
    except ValueError as error:results["expressions"].append({"value":None,"error":str(error)})
named={"Return":Qt.Key.Key_Return,"Enter":Qt.Key.Key_Enter,"Escape":Qt.Key.Key_Escape,
       "Backspace":Qt.Key.Key_Backspace,"Delete":Qt.Key.Key_Delete}
for index,scenario in enumerate(request["gui"]):
    window=candidate.ApplicationWindow();window.show();application.processEvents()
    trace=[]
    for event in scenario["events"]:
        if "button" in event:
            button=window.findChild(QPushButton,event["button"])
            if button is None:raise AssertionError("Missing button: "+event["button"])
            QTest.mouseClick(button,Qt.MouseButton.LeftButton)
        elif "text" in event:QTest.keyClicks(window,event["text"])
        else:QTest.keyClick(window,named[event["key"]])
        application.processEvents()
        trace.append(window.findChild(QLineEdit,"display").text())
    results["gui"].append({"display":window.findChild(QLineEdit,"display").text(),
        "memory":getattr(window.controller,"memory",None),"error":window.controller.error,
        "buttons":sorted(b.objectName() for b in window.findChildren(QPushButton)),
        "title":window.windowTitle(),"trace":trace})
    if index==0 and request.get("preview"):window.grab().save("/output/preview.png")
    window.close();window.deleteLater();application.processEvents()
print(json.dumps(results,allow_nan=False))
