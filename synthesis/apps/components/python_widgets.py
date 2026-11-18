"""Generic Qt widget construction and event wiring; no calculator logic."""
import sys
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QApplication,QWidget,QGridLayout,QLineEdit,QLabel,QPushButton


class GenericWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.grid=QGridLayout(self)
        self.updates=[]
        self.keys={}
        self.setMinimumSize(320,360)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setStyleSheet("QPushButton {min-height:40px;font-size:18px;} QLineEdit {font-size:24px;min-height:52px;} QLabel {font-size:12px;}")
    def add_display(self,identifier,kind,row,column,span,getter):
        widget=QLineEdit() if kind=="display" else QLabel()
        widget.setObjectName(identifier)
        if kind=="display":
            widget.setReadOnly(True)
            widget.setAlignment(Qt.AlignmentFlag.AlignRight)
            widget.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.grid.addWidget(widget,row,column,1,span)
        self.updates.append((widget,getter))
    def add_button(self,identifier,label,row,column,handler):
        button=QPushButton(label)
        button.setObjectName(identifier)
        button.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        button.clicked.connect(lambda checked=False:self.perform(handler))
        self.grid.addWidget(button,row,column)
    def bind_key(self,key,handler):self.keys[key]=handler
    def refresh(self):
        for widget,getter in self.updates:widget.setText(getter())
    def perform(self,handler):
        handler()
        self.refresh()
    def keyPressEvent(self,event):
        if event.modifiers() & (Qt.KeyboardModifier.ControlModifier|Qt.KeyboardModifier.AltModifier|Qt.KeyboardModifier.MetaModifier):
            return super().keyPressEvent(event)
        names={Qt.Key.Key_Return:"Return",Qt.Key.Key_Enter:"Enter",Qt.Key.Key_Escape:"Escape",
               Qt.Key.Key_Backspace:"Backspace",Qt.Key.Key_Delete:"Delete"}
        key=names.get(event.key(),event.text())
        if key in self.keys:
            self.perform(self.keys[key]);event.accept()
        else:super().keyPressEvent(event)
