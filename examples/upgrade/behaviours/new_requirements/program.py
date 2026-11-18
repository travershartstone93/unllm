"""Reusable widget, document and I/O primitives. No application recipes."""
import math
import json
import time
import os
from pathlib import Path
import sys
import tempfile
from PySide6.QtCore import Qt, QSignalBlocker, QTimer
from PySide6.QtGui import QKeySequence, QShortcut
from PySide6.QtWidgets import QApplication, QWidget, QGridLayout, QPlainTextEdit, QLineEdit, QDoubleSpinBox, QLabel, QPushButton, QFileDialog, QMessageBox, QListWidget, QListWidgetItem
MAX_BYTES = 4 * 1024 * 1024

def checked_number(value):
    if not math.isfinite(value) or abs(value) > 1000000000000000.0:
        raise ValueError('Number outside supported range')
    return value

def checked_divide(left, right):
    if right == 0:
        raise ValueError('Division by zero')
    return checked_number(left / right)

def number_text(value):
    return format(0.0 if value == 0 else value, '.12g')

def text_upper(value):
    return value.upper()

def text_lower(value):
    return value.lower()

def text_strip(value):
    return value.strip()

def text_reverse_lines(value):
    return '\n'.join(reversed(value.splitlines()))

def text_sort_lines(value):
    return '\n'.join(sorted(value.splitlines()))

def text_unique_lines(value):
    return '\n'.join(dict.fromkeys(value.splitlines()))

def text_words(value):
    return float(len(value.split()))

def text_chars(value):
    return float(len(value))

def text_lines(value):
    return float(len(value.splitlines()))

def text_replace(value, old, new):
    return value.replace(old, new)

def records_count(value):
    return float(len(value))

def count_true(value, field):
    return float(sum((row[field] for row in value)))

class Dialogs:

    def open_path(self, parent):
        return QFileDialog.getOpenFileName(parent, 'Open text file', '', 'Text files (*.txt);;All files (*)')[0]

    def save_path(self, parent):
        return QFileDialog.getSaveFileName(parent, 'Save text file', '', 'Text files (*.txt);;All files (*)')[0]

    def confirm(self, parent, name):
        result = QMessageBox.question(parent, 'Unsaved changes', 'Save changes to ' + name + '?', QMessageBox.StandardButton.Save | QMessageBox.StandardButton.Discard | QMessageBox.StandardButton.Cancel, QMessageBox.StandardButton.Cancel)
        return 'save' if result == QMessageBox.StandardButton.Save else 'discard' if result == QMessageBox.StandardButton.Discard else 'cancel'

class BehaviourWindow(QWidget):

    def __init__(self, dialogs=None, clock=None):
        super().__init__()
        self.dialogs = dialogs or Dialogs()
        self.grid = QGridLayout(self)
        self.fields = {}
        self.inputs = {}
        self.paths = {}
        self.saved = {}
        self.outputs = []
        self.record_views = []
        self.timers = []
        self.clock = clock or time.monotonic
        self.error_message = ''
        self.status = QLabel()
        self.status.setObjectName('status')
        self.setMinimumSize(520, 360)
        self.setStyleSheet('QPushButton {min-height:28px;} QPlainTextEdit {font-size:15px;} QLabel {font-size:13px;}')

    def register(self, name, kind, getter, setter, persistent=False, schema=None):
        self.fields[name] = (kind, getter, setter, schema)
        if persistent:
            self.paths[name] = ''
            self.saved[name] = self.encode_state(name)

    def get(self, name):
        return self.fields[name][1]()

    def validate_records(self, name, value):
        schema = self.fields[name][3]
        if not isinstance(value, list) or len(value) > 10000:
            raise ValueError('Collection exceeds 10000 records')
        for row in value:
            if not isinstance(row, dict) or set(row) != set(schema):
                raise ValueError('Record fields do not match')
            for key, kind in schema.items():
                item = row[key]
                if kind == 'text' and isinstance(item, str):
                    continue
                if kind == 'bool' and type(item) is bool:
                    continue
                if kind == 'number' and type(item) in (int, float):
                    checked_number(item)
                    continue
                raise ValueError('Invalid record field value')
        if len(json.dumps(value, ensure_ascii=False).encode('utf-8')) > MAX_BYTES:
            raise ValueError('Collection exceeds 4 MiB')

    def commit(self, updates):
        for name, value in updates.items():
            kind = self.fields[name][0]
            if kind == 'number':
                checked_number(value)
            elif kind == 'text':
                if not isinstance(value, str) or len(value.encode('utf-8')) > MAX_BYTES:
                    raise ValueError('Text exceeds 4 MiB')
            elif kind == 'records':
                self.validate_records(name, value)
            elif type(value) is not bool:
                raise ValueError('Expected a Boolean')
        for name, value in updates.items():
            self.fields[name][2](value)

    def perform(self, handler, clear_error=True):
        if clear_error:
            self.error_message = ''
        try:
            handler()
        except (ValueError, OSError) as error:
            self.error_message = str(error)
        self.refresh()

    def change(self, name, value):
        self.perform(lambda: self.commit({name: value}))

    def add_input(self, identifier, kind, name, label, row):
        if label:
            self.grid.addWidget(QLabel(label), row, 0, 1, 4)
            row += 1
        widget = QPlainTextEdit() if kind == 'text_edit' else QLineEdit() if kind == 'line_edit' else QDoubleSpinBox()
        widget.setObjectName(identifier)
        if kind == 'number_input':
            widget.setRange(-1000000000000000.0, 1000000000000000.0)
            widget.setDecimals(12)
            widget.valueChanged.connect(lambda value: self.change(name, value))
        elif kind == 'text_edit':
            widget.textChanged.connect(lambda: self.change(name, widget.toPlainText()))
        else:
            widget.textChanged.connect(lambda value: self.change(name, value))
        self.inputs.setdefault(name, []).append(widget)
        self.grid.addWidget(widget, row, 0, 1, 4)
        return row + 1

    def add_output(self, identifier, getter, label, row):
        widget = QLabel()
        widget.setObjectName(identifier)
        widget.setWordWrap(True)
        widget.setTextFormat(Qt.TextFormat.PlainText)
        self.outputs.append((widget, getter, label))
        self.grid.addWidget(widget, row, 0, 1, 4)
        return row + 1

    def add_records(self, identifier, name, selection, display, checked, label, row):
        if label:
            self.grid.addWidget(QLabel(label), row, 0, 1, 4)
            row += 1
        widget = QListWidget()
        widget.setObjectName(identifier)
        widget.currentRowChanged.connect(lambda index: self.change(selection, float(index)))

        def changed(item):
            index = widget.row(item)
            values = [dict(r) for r in self.get(name)]
            if checked and 0 <= index < len(values):
                values[index][checked] = item.checkState() == Qt.CheckState.Checked
                self.perform(lambda: self.commit({name: values}))
        widget.itemChanged.connect(changed)
        self.record_views.append((widget, name, selection, display, checked))
        self.grid.addWidget(widget, row, 0, 1, 4)
        return row + 1

    def add_periodic(self, interval, handler):
        timer = QTimer(self)
        timer.setInterval(interval)
        timer.timeout.connect(lambda: self.perform(handler, False))
        self.timers.append(timer)
        timer.start()

    def add_action(self, identifier, label, handler, shortcut, row, column):
        button = QPushButton(label)
        button.setObjectName(identifier)
        button.clicked.connect(lambda checked=False: self.perform(handler))
        self.grid.addWidget(button, row, column)
        if shortcut:
            key = QShortcut(QKeySequence(shortcut), self)
            key.activated.connect(lambda: self.perform(handler))

    def finish(self, row):
        self.grid.addWidget(self.status, row, 0, 1, 4)
        self.refresh()

    def refresh(self):
        for name, widgets in self.inputs.items():
            value = self.get(name)
            for widget in widgets:
                if isinstance(widget, QDoubleSpinBox):
                    if widget.value() != value:
                        blocker = QSignalBlocker(widget)
                        widget.setValue(value)
                        del blocker
                else:
                    current = widget.toPlainText() if isinstance(widget, QPlainTextEdit) else widget.text()
                    if current != value:
                        blocker = QSignalBlocker(widget)
                        if isinstance(widget, QPlainTextEdit):
                            widget.setPlainText(value)
                        else:
                            widget.setText(value)
                        del blocker
        for widget, name, selection, display, checked in self.record_views:
            records = self.get(name)
            key = json.dumps(records, sort_keys=True, ensure_ascii=False)
            blocker = QSignalBlocker(widget)
            if widget.property('records') != key:
                if widget.count() != len(records):
                    widget.clear()
                    for _ in records:
                        widget.addItem(QListWidgetItem())
                for index, record in enumerate(records):
                    item = widget.item(index)
                    item.setText(record[display])
                    if checked:
                        item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
                        item.setCheckState(Qt.CheckState.Checked if record[checked] else Qt.CheckState.Unchecked)
                widget.setProperty('records', key)
            wanted = self.get(selection)
            widget.setCurrentRow(int(wanted) if float(wanted).is_integer() and 0 <= wanted < len(records) else -1)
            del blocker
        for widget, getter, label in self.outputs:
            try:
                value = getter()
                text = number_text(value) if type(value) in (int, float) else str(value).lower() if type(value) is bool else value
                widget.setText((label + ': ' if label else '') + text)
            except (ValueError, ZeroDivisionError):
                widget.setText('Error')
        self.status.setText(self.error_message)
        self.setWindowModified(any((self.encode_state(n) != saved for n, saved in self.saved.items())))

    def encode_state(self, name):
        value = self.get(name)
        return value if self.fields[name][0] == 'text' else json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'))

    def decode_state(self, name, content):
        if self.fields[name][0] == 'text':
            return content
        value = json.loads(content)
        self.validate_records(name, value)
        return value

    def empty_state(self, name):
        return '' if self.fields[name][0] == 'text' else '[]'

    def append_record(self, name, values, selection, clear):
        records = [dict(r) for r in self.get(name)] + [values]
        updates = {name: records, **{field: '' for field in clear}}
        if selection:
            updates[selection] = float(len(records) - 1)
        self.commit(updates)

    def remove_record(self, name, selection):
        records = [dict(r) for r in self.get(name)]
        index = self.get(selection)
        if not float(index).is_integer() or not 0 <= index < len(records):
            raise ValueError('Select an item first')
        del records[int(index)]
        self.commit({name: records, selection: float(min(int(index), len(records) - 1))})

    def set_record(self, name, selection, values):
        records = [dict(r) for r in self.get(name)]
        index = self.get(selection)
        if not float(index).is_integer() or not 0 <= index < len(records):
            raise ValueError('Select an item first')
        records[int(index)].update(values)
        self.commit({name: records})

    def choose_open(self, name):
        return self.dialogs.open_path(self)

    def choose_save(self, name):
        return self.dialogs.save_path(self)

    def choose_existing_or_save(self, name):
        return self.paths.get(name) or self.choose_save(name)

    def read_utf8(self, path):
        with open(path, 'rb') as stream:
            raw = stream.read(MAX_BYTES + 1)
        if len(raw) > MAX_BYTES:
            raise ValueError('File exceeds 4 MiB')
        return raw.decode('utf-8', errors='strict')

    def write_atomic_utf8(self, path, content):
        raw = content.encode('utf-8')
        if len(raw) > MAX_BYTES:
            raise ValueError('Text exceeds 4 MiB')
        destination = Path(path)
        temporary = None
        try:
            descriptor, temporary = tempfile.mkstemp(prefix='.unllm-', dir=destination.parent)
            with os.fdopen(descriptor, 'wb') as stream:
                stream.write(raw)
                stream.flush()
                os.fsync(stream.fileno())
            if destination.exists():
                os.chmod(temporary, destination.stat().st_mode & 511)
            os.replace(temporary, destination)
            temporary = None
        finally:
            if temporary is not None:
                os.unlink(temporary)

    def confirm_discard(self, name):
        if self.encode_state(name) == self.saved.get(name, self.encode_state(name)):
            return True
        decision = self.dialogs.confirm(self, name)
        if decision == 'discard':
            return True
        if decision != 'save':
            return False
        path = self.choose_existing_or_save(name)
        if not path:
            return False
        self.write_atomic_utf8(path, self.encode_state(name))
        self.paths[name] = path
        self.saved[name] = self.encode_state(name)
        return True

    def edit(self, name, operation):
        widgets = self.inputs.get(name, [])
        if not widgets:
            raise ValueError('No editable view')
        widget = widgets[0]
        {'undo': widget.undo, 'redo': widget.redo, 'cut': widget.cut, 'copy': widget.copy, 'paste': widget.paste, 'select_all': widget.selectAll}[operation]()

    def closeEvent(self, event):
        try:
            if all((self.confirm_discard(name) for name in self.saved)):
                for timer in self.timers:
                    timer.stop()
                event.accept()
            else:
                event.ignore()
        except (OSError, ValueError) as error:
            self.error_message = str(error)
            self.refresh()
            event.ignore()

class ApplicationWindow(BehaviourWindow):

    def __init__(self, dialogs=None, clock=None):
        super().__init__(dialogs, clock)
        self.state_quantity = 0
        self.state_text = ''
        self.setWindowTitle('Fresh Requirements')
        self.register('quantity', 'number', lambda: self.state_quantity, lambda value: setattr(self, 'state_quantity', value), False, None)
        self.register('text', 'text', lambda: self.state_text, lambda value: setattr(self, 'state_text', value), False, None)
        row = 0
        row = self.add_input('quantity', 'number_input', 'quantity', '', row)
        row = self.add_input('text', 'text_edit', 'text', '', row)
        row = self.add_output('characters', lambda: text_chars(self.state_text), '', row)
        self.add_action('solve', 'solve', self.on_solve, '', row + 0, 0)
        self.add_action('transform', 'transform', self.on_transform, '', row + 0, 1)
        self.finish(row + 1)

    def on_solve(self):
        self.commit({'quantity': checked_number(0.3333333333333333 * checked_number(self.state_quantity + 15.0))})

    def on_transform(self):
        self.commit({'text': text_reverse_lines(text_upper(text_strip(self.state_text)))})

def main():
    application = QApplication(sys.argv)
    application.setStyle('Fusion')
    window = ApplicationWindow()
    window.show()
    return application.exec()
if __name__ == '__main__':
    raise SystemExit(main())
