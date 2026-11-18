// Reusable Qt widgets, Unicode text operations and atomic file primitives.
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStringDecoder>
#include <QtCore/QTimer>
#include <QtGui/QCloseEvent>
#include <QtGui/QShortcut>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QWidget>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static constexpr qint64 MAX_BYTES = 4 * 1024 * 1024;
static QString qtext(const std::string &s) {
  return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
static std::string bytes(const QString &s) { return s.toUtf8().toStdString(); }
static double checked_number(double value) {
  if (!std::isfinite(value) || std::abs(value) > 1e15)
    throw std::runtime_error("Number outside supported range");
  return value;
}
static std::string number_text(double value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(12) << (value == 0 ? 0 : value);
  return out.str();
}
static double checked_divide(double left, double right) {
  if (right == 0)
    throw std::runtime_error("Division by zero");
  return checked_number(left / right);
}
static QStringList split_lines(const QString &text) {
  QStringList result;
  qsizetype start = 0;
  for (qsizetype i = 0; i < text.size(); ++i) {
    auto c = text[i].unicode();
    if (c == 10 || c == 13 || c == 11 || c == 12 || (c >= 28 && c <= 30) ||
        c == 0x85 || c == 0x2028 || c == 0x2029) {
      result.push_back(text.mid(start, i - start));
      if (c == 13 && i + 1 < text.size() && text[i + 1] == QChar('\n'))
        ++i;
      start = i + 1;
    }
  }
  if (start < text.size())
    result.push_back(text.mid(start));
  return result;
}
static std::string text_upper(const std::string &s) {
  return bytes(qtext(s).toUpper());
}
static std::string text_lower(const std::string &s) {
  return bytes(qtext(s).toLower());
}
static bool text_space(QChar c) {
  return c.isSpace() || (c.unicode() >= 28 && c.unicode() <= 31);
}
static std::string text_strip(const std::string &s) {
  auto text = qtext(s);
  qsizetype start = 0, end = text.size();
  while (start < end && text_space(text[start]))
    ++start;
  while (end > start && text_space(text[end - 1]))
    --end;
  return bytes(text.mid(start, end - start));
}
static std::string text_reverse_lines(const std::string &s) {
  auto rows = split_lines(qtext(s));
  std::reverse(rows.begin(), rows.end());
  return bytes(rows.join('\n'));
}
static std::string text_sort_lines(const std::string &s) {
  auto rows = split_lines(qtext(s));
  std::sort(rows.begin(), rows.end(), [](const QString &a, const QString &b) {
    auto x = a.toUcs4(), y = b.toUcs4();
    return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end());
  });
  return bytes(rows.join('\n'));
}
static std::string text_unique_lines(const std::string &s) {
  std::set<QString> seen;
  QStringList result;
  for (const auto &row : split_lines(qtext(s)))
    if (seen.insert(row).second)
      result.push_back(row);
  return bytes(result.join('\n'));
}
static double text_words(const std::string &s) {
  bool inside = false;
  double count = 0;
  for (const auto c : qtext(s)) {
    bool space = text_space(c);
    if (!space && !inside)
      ++count;
    inside = !space;
  }
  return count;
}
static double text_chars(const std::string &s) {
  return static_cast<double>(qtext(s).toUcs4().size());
}
static double text_lines(const std::string &s) {
  return static_cast<double>(split_lines(qtext(s)).size());
}
static std::string text_replace(const std::string &s, const std::string &old,
                                const std::string &replacement) {
  if (old.empty()) {
    auto insert = qtext(replacement);
    QString result = insert;
    for (auto cp : qtext(s).toUcs4())
      result += QString::fromUcs4(&cp, 1) + insert;
    return bytes(result);
  }
  return bytes(qtext(s).replace(qtext(old), qtext(replacement)));
}
static QJsonArray parse_array(const std::string &s) {
  QJsonParseError error;
  auto document = QJsonDocument::fromJson(QByteArray::fromStdString(s), &error);
  if (error.error != QJsonParseError::NoError || !document.isArray())
    throw std::runtime_error("Expected a JSON record array");
  return document.array();
}
static QJsonObject parse_object(const std::string &s) {
  auto document = QJsonDocument::fromJson(QByteArray::fromStdString(s));
  if (!document.isObject())
    throw std::runtime_error("Expected record fields");
  return document.object();
}
static double records_count(const QJsonArray &records) {
  return static_cast<double>(records.size());
}
static double count_true(const QJsonArray &records, const std::string &field) {
  double result = 0;
  for (const auto &row : records)
    if (row.toObject()[qtext(field)].toBool())
      ++result;
  return result;
}
struct Dialogs {
  std::function<std::string(QWidget *)> open_path = [](QWidget *w) {
    return bytes(QFileDialog::getOpenFileName(
        w, "Open text file", {}, "Text files (*.txt);;All files (*)"));
  };
  std::function<std::string(QWidget *)> save_path = [](QWidget *w) {
    return bytes(QFileDialog::getSaveFileName(
        w, "Save text file", {}, "Text files (*.txt);;All files (*)"));
  };
  std::function<std::string(QWidget *, const std::string &)> confirm =
      [](QWidget *w, const std::string &name) {
        auto choice = QMessageBox::question(
            w, "Unsaved changes", "Save changes to " + qtext(name) + "?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Cancel);
        return choice == QMessageBox::Save      ? "save"
               : choice == QMessageBox::Discard ? "discard"
                                                : "cancel";
      };
};
class BehaviourWindow : public QWidget {
  struct Field {
    std::string kind;
    std::function<std::string()> text_get;
    std::function<void(const std::string &)> text_set;
    std::function<double()> number_get;
    std::function<void(double)> number_set;
    std::function<QJsonArray()> records_get;
    std::function<void(const QJsonArray &)> records_set;
    QJsonObject schema;
  };
  struct Output {
    QLabel *widget;
    std::function<std::string()> getter;
    std::string label;
  };
  struct RecordView {
    QListWidget *widget;
    std::string name, selection, display, checked;
  };
  std::map<std::string, Field> fields;
  std::map<std::string, std::vector<QWidget *>> inputs;
  std::vector<Output> outputs;
  std::vector<RecordView> record_views;
  std::vector<QTimer *> timers;
  QLabel *status;

public:
  QGridLayout *grid;
  Dialogs dialogs;
  std::function<double()> clock_source;
  std::map<std::string, std::string> paths, saved;
  std::string error_message;
  explicit BehaviourWindow(Dialogs service = {},
                           std::function<double()> clock = {})
      : grid(new QGridLayout(this)), dialogs(std::move(service)),
        clock_source(std::move(clock)) {
    status = new QLabel;
    status->setObjectName("status");
    setMinimumSize(520, 360);
    setStyleSheet("QPushButton {min-height:28px;} QPlainTextEdit "
                  "{font-size:15px;} QLabel {font-size:13px;}");
  }
  void register_text(const std::string &name,
                     std::function<std::string()> getter,
                     std::function<void(const std::string &)> setter,
                     bool persistent) {
    fields[name].kind = "text";
    fields[name].text_get = getter;
    fields[name].text_set = setter;
    if (persistent) {
      paths[name] = "";
      saved[name] = getter();
    }
  }
  void register_number(const std::string &name, std::function<double()> getter,
                       std::function<void(double)> setter) {
    fields[name].kind = "number";
    fields[name].number_get = getter;
    fields[name].number_set = setter;
  }
  void register_records(const std::string &name,
                        std::function<QJsonArray()> getter,
                        std::function<void(const QJsonArray &)> setter,
                        const QJsonObject &schema, bool persistent) {
    fields[name].kind = "records";
    fields[name].records_get = getter;
    fields[name].records_set = setter;
    fields[name].schema = schema;
    validate_records(name, getter());
    if (persistent) {
      paths[name] = "";
      saved[name] = encode_state(name);
    }
  }
  void validate_records(const std::string &name, const QJsonArray &records) {
    const auto schema = fields.at(name).schema;
    if (records.size() > 10000 ||
        QJsonDocument(records).toJson(QJsonDocument::Compact).size() >
            MAX_BYTES)
      throw std::runtime_error("Collection exceeds supported size");
    for (const auto &value : records) {
      if (!value.isObject())
        throw std::runtime_error("Expected a record");
      auto row = value.toObject();
      if (row.keys() != schema.keys())
        throw std::runtime_error("Record fields do not match");
      for (auto it = schema.begin(); it != schema.end(); ++it) {
        auto item = row[it.key()];
        auto type = it.value().toString();
        if (type == "text" && item.isString())
          continue;
        if (type == "bool" && item.isBool())
          continue;
        if (type == "number" && item.isDouble()) {
          checked_number(item.toDouble());
          continue;
        }
        throw std::runtime_error("Invalid record field value");
      }
    }
  }
  std::string encode_state(const std::string &name) {
    auto &f = fields.at(name);
    return f.kind == "text" ? f.text_get()
                            : QJsonDocument(f.records_get())
                                  .toJson(QJsonDocument::Compact)
                                  .toStdString();
  }
  std::string empty_state(const std::string &name) {
    return fields.at(name).kind == "text" ? "" : "[]";
  }
  void put_encoded(const std::string &name, const std::string &content) {
    auto &f = fields.at(name);
    if (f.kind == "text") {
      put_text(name, content);
      return;
    }
    auto records = parse_array(content);
    validate_records(name, records);
    f.records_set(records);
  }
  std::string get_text(const std::string &name) {
    return fields.at(name).text_get();
  }
  void put_text(const std::string &name, const std::string &value) {
    if (value.size() > MAX_BYTES)
      throw std::runtime_error("Text exceeds 4 MiB");
    fields.at(name).text_set(value);
  }
  void perform(const std::function<void()> &handler, bool clear_error = true) {
    if (clear_error)
      error_message.clear();
    try {
      handler();
    } catch (const std::exception &e) {
      error_message = e.what();
    }
    refresh();
  }
  int add_input(const std::string &id, const std::string &kind,
                const std::string &name, const std::string &label, int row) {
    if (!label.empty())
      grid->addWidget(new QLabel(qtext(label)), row++, 0, 1, 4);
    QWidget *widget = nullptr;
    if (kind == "text_edit") {
      auto *edit = new QPlainTextEdit;
      widget = edit;
      QObject::connect(
          edit, &QPlainTextEdit::textChanged, this, [this, edit, name] {
            perform([&] { put_text(name, bytes(edit->toPlainText())); });
          });
    } else if (kind == "line_edit") {
      auto *edit = new QLineEdit;
      widget = edit;
      QObject::connect(edit, &QLineEdit::textChanged, this,
                       [this, name](const QString &text) {
                         perform([&] { put_text(name, bytes(text)); });
                       });
    } else {
      auto *edit = new QDoubleSpinBox;
      widget = edit;
      edit->setRange(-1e15, 1e15);
      edit->setDecimals(12);
      QObject::connect(
          edit, &QDoubleSpinBox::valueChanged, this,
          [this, name](double value) {
            perform([&] { fields.at(name).number_set(checked_number(value)); });
          });
    }
    widget->setObjectName(qtext(id));
    inputs[name].push_back(widget);
    grid->addWidget(widget, row++, 0, 1, 4);
    return row;
  }
  int add_output(const std::string &id, std::function<std::string()> getter,
                 const std::string &label, int row) {
    auto *widget = new QLabel;
    widget->setObjectName(qtext(id));
    widget->setWordWrap(true);
    widget->setTextFormat(Qt::PlainText);
    outputs.push_back({widget, getter, label});
    grid->addWidget(widget, row++, 0, 1, 4);
    return row;
  }
  int add_records(const std::string &id, const std::string &name,
                  const std::string &selection, const std::string &display,
                  const std::string &checked, const std::string &label,
                  int row) {
    if (!label.empty())
      grid->addWidget(new QLabel(qtext(label)), row++, 0, 1, 4);
    auto *widget = new QListWidget;
    widget->setObjectName(qtext(id));
    grid->addWidget(widget, row++, 0, 1, 4);
    QObject::connect(widget, &QListWidget::currentRowChanged, this,
                     [this, selection](int index) {
                       perform([&] { fields.at(selection).number_set(index); });
                     });
    QObject::connect(widget, &QListWidget::itemChanged, this,
                     [this, widget, name, checked](QListWidgetItem *item) {
                       if (checked.empty())
                         return;
                       int index = widget->row(item);
                       auto records = fields.at(name).records_get();
                       if (index < 0 || index >= records.size())
                         return;
                       auto record = records[index].toObject();
                       record[qtext(checked)] =
                           item->checkState() == Qt::Checked;
                       records[index] = record;
                       perform([&] {
                         validate_records(name, records);
                         fields.at(name).records_set(records);
                       });
                     });
    record_views.push_back({widget, name, selection, display, checked});
    return row;
  }
  void add_periodic(int interval, std::function<void()> handler) {
    auto *timer = new QTimer(this);
    timer->setInterval(interval);
    QObject::connect(timer, &QTimer::timeout, this,
                     [this, handler] { perform(handler, false); });
    timers.push_back(timer);
    timer->start();
  }
  double monotonic_now() {
    if (clock_source)
      return checked_number(clock_source());
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
  void add_action(const std::string &id, const std::string &label,
                  std::function<void()> handler, const std::string &shortcut,
                  int row, int column) {
    auto *button = new QPushButton(qtext(label));
    button->setObjectName(qtext(id));
    QObject::connect(button, &QPushButton::clicked, this,
                     [this, handler] { perform(handler); });
    grid->addWidget(button, row, column);
    if (!shortcut.empty()) {
      auto *key = new QShortcut(QKeySequence(qtext(shortcut)), this);
      QObject::connect(key, &QShortcut::activated, this,
                       [this, handler] { perform(handler); });
    }
  }
  void finish(int row) {
    grid->addWidget(status, row, 0, 1, 4);
    refresh();
  }
  void refresh() {
    for (auto &[name, widgets] : inputs)
      for (auto *widget : widgets) {
        QSignalBlocker blocker(widget);
        if (auto *edit = qobject_cast<QPlainTextEdit *>(widget)) {
          auto text = qtext(get_text(name));
          if (edit->toPlainText() != text)
            edit->setPlainText(text);
        } else if (auto *line = qobject_cast<QLineEdit *>(widget)) {
          auto text = qtext(get_text(name));
          if (line->text() != text)
            line->setText(text);
        } else if (auto *spin = qobject_cast<QDoubleSpinBox *>(widget)) {
          auto value = fields.at(name).number_get();
          if (spin->value() != value)
            spin->setValue(value);
        }
      }
    for (auto &output : outputs)
      try {
        output.widget->setText(
            qtext((output.label.empty() ? "" : output.label + ": ") +
                  output.getter()));
      } catch (const std::exception &) {
        output.widget->setText("Error");
      }
    status->setText(qtext(error_message));
    bool dirty = false;
    for (auto &view : record_views) {
      auto records = fields.at(view.name).records_get();
      auto key = QJsonDocument(records).toJson(QJsonDocument::Compact);
      QSignalBlocker blocker(view.widget);
      if (view.widget->property("records").toByteArray() != key) {
        if (view.widget->count() != records.size()) {
          view.widget->clear();
          for (qsizetype i = 0; i < records.size(); ++i)
            new QListWidgetItem(view.widget);
        }
        for (qsizetype i = 0; i < records.size(); ++i) {
          auto record = records[i].toObject();
          auto *item = view.widget->item(static_cast<int>(i));
          item->setText(record[qtext(view.display)].toString());
          if (!view.checked.empty()) {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(record[qtext(view.checked)].toBool()
                                    ? Qt::Checked
                                    : Qt::Unchecked);
          }
        }
        view.widget->setProperty("records", key);
      }
      double selected = fields.at(view.selection).number_get();
      view.widget->setCurrentRow(std::trunc(selected) == selected &&
                                         selected >= 0 &&
                                         selected < records.size()
                                     ? static_cast<int>(selected)
                                     : -1);
    }
    for (const auto &[name, text] : saved)
      dirty |= encode_state(name) != text;
    setWindowModified(dirty);
  }
  void append_record(const std::string &name, const QJsonObject &values,
                     const std::string &selection,
                     const std::vector<std::string> &clear) {
    auto records = fields.at(name).records_get();
    records.append(values);
    validate_records(name, records);
    for (const auto &field : clear)
      if (fields.at(field).kind != "text")
        throw std::runtime_error("Invalid clear target");
    fields.at(name).records_set(records);
    if (!selection.empty())
      fields.at(selection).number_set(records.size() - 1);
    for (const auto &field : clear)
      fields.at(field).text_set("");
  }
  void remove_record(const std::string &name, const std::string &selection) {
    auto records = fields.at(name).records_get();
    double index = fields.at(selection).number_get();
    if (std::trunc(index) != index || index < 0 || index >= records.size())
      throw std::runtime_error("Select an item first");
    records.removeAt(static_cast<qsizetype>(index));
    fields.at(name).records_set(records);
    fields.at(selection).number_set(
        std::min(index, static_cast<double>(records.size() - 1)));
  }
  void set_record(const std::string &name, const std::string &selection,
                  const QJsonObject &values) {
    auto records = fields.at(name).records_get();
    double index = fields.at(selection).number_get();
    if (std::trunc(index) != index || index < 0 || index >= records.size())
      throw std::runtime_error("Select an item first");
    auto record = records[static_cast<qsizetype>(index)].toObject();
    for (auto it = values.begin(); it != values.end(); ++it)
      record[it.key()] = it.value();
    records[static_cast<qsizetype>(index)] = record;
    validate_records(name, records);
    fields.at(name).records_set(records);
  }
  std::string choose_open(const std::string &) {
    return dialogs.open_path(this);
  }
  std::string choose_save(const std::string &) {
    return dialogs.save_path(this);
  }
  std::string choose_existing_or_save(const std::string &name) {
    return paths[name].empty() ? choose_save(name) : paths[name];
  }
  std::string read_utf8(const std::string &path) {
    QFile file(qtext(path));
    if (!file.open(QIODevice::ReadOnly))
      throw std::runtime_error(bytes(file.errorString()));
    auto raw = file.read(MAX_BYTES + 1);
    if (raw.size() > MAX_BYTES)
      throw std::runtime_error("File exceeds 4 MiB");
    if (file.error() != QFileDevice::NoError)
      throw std::runtime_error(bytes(file.errorString()));
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString decoded = decoder.decode(raw);
    if (decoder.hasError())
      throw std::runtime_error("File is not valid UTF-8");
    return bytes(decoded);
  }
  void write_atomic_utf8(const std::string &path, const std::string &text) {
    if (text.size() > MAX_BYTES)
      throw std::runtime_error("Text exceeds 4 MiB");
    QSaveFile file(qtext(path));
    if (!file.open(QIODevice::WriteOnly))
      throw std::runtime_error(bytes(file.errorString()));
    if (file.write(text.data(), static_cast<qint64>(text.size())) !=
            static_cast<qint64>(text.size()) ||
        !file.commit())
      throw std::runtime_error(bytes(file.errorString()));
  }
  bool confirm_discard(const std::string &name) {
    if (!saved.count(name) || encode_state(name) == saved[name])
      return true;
    auto choice = dialogs.confirm(this, name);
    if (choice == "discard")
      return true;
    if (choice != "save")
      return false;
    auto path = choose_existing_or_save(name);
    if (path.empty())
      return false;
    write_atomic_utf8(path, encode_state(name));
    paths[name] = path;
    saved[name] = encode_state(name);
    return true;
  }
  void edit(const std::string &name, const std::string &operation) {
    auto *widget = inputs.at(name).front();
    if (auto *text = qobject_cast<QPlainTextEdit *>(widget)) {
      if (operation == "undo")
        text->undo();
      else if (operation == "redo")
        text->redo();
      else if (operation == "cut")
        text->cut();
      else if (operation == "copy")
        text->copy();
      else if (operation == "paste")
        text->paste();
      else
        text->selectAll();
    } else if (auto *line = qobject_cast<QLineEdit *>(widget)) {
      if (operation == "undo")
        line->undo();
      else if (operation == "redo")
        line->redo();
      else if (operation == "cut")
        line->cut();
      else if (operation == "copy")
        line->copy();
      else if (operation == "paste")
        line->paste();
      else
        line->selectAll();
    }
  }

protected:
  void closeEvent(QCloseEvent *event) override {
    try {
      for (const auto &[name, text] : saved)
        if (!confirm_discard(name)) {
          event->ignore();
          return;
        }
      for (auto *timer : timers)
        timer->stop();
      event->accept();
    } catch (const std::exception &e) {
      error_message = e.what();
      refresh();
      event->ignore();
    }
  }
};

class ApplicationWindow : public BehaviourWindow {
public:
  std::string state_notes = "";
  explicit ApplicationWindow(Dialogs service = {},
                             std::function<double()> clock = {})
      : BehaviourWindow(std::move(service), std::move(clock)) {
    setWindowTitle(qtext("Text Workbench [*]"));
    register_text(
        "notes", [this] { return state_notes; },
        [this](const std::string &v) { state_notes = v; }, true);
    int row = 0;
    row = add_input("notes_input", "text_edit", "notes", "", row);
    row = add_output(
        "words", [this] { return number_text(text_words(state_notes)); },
        "Words", row);
    row = add_output(
        "characters", [this] { return number_text(text_chars(state_notes)); },
        "Characters", row);
    add_action(
        "capitalize", "Uppercase", [this] { on_capitalize(); }, "", row + 0, 0);
    add_action(
        "organize", "Sort unique lines", [this] { on_organize(); }, "", row + 0,
        1);
    add_action("open", "Open", [this] { on_open(); }, "Ctrl+O", row + 0, 2);
    add_action("save", "Save", [this] { on_save(); }, "Ctrl+S", row + 0, 3);
    finish(row + 1);
  }
  void on_capitalize() {
    std::string proposed_notes = text_upper(state_notes);
    if (proposed_notes.size() > MAX_BYTES)
      throw std::runtime_error("Text exceeds 4 MiB");
    state_notes = proposed_notes;
  }
  void on_organize() {
    std::string proposed_notes =
        text_sort_lines(text_unique_lines(state_notes));
    if (proposed_notes.size() > MAX_BYTES)
      throw std::runtime_error("Text exceeds 4 MiB");
    state_notes = proposed_notes;
  }
  void on_open() {
    if (!confirm_discard("notes"))
      return;
    auto path = choose_open("notes");
    if (path.empty())
      return;
    auto content = read_utf8(path);
    put_encoded("notes", content);
    paths["notes"] = path;
    saved["notes"] = encode_state("notes");
  }
  void on_save() {
    auto content = encode_state("notes");
    auto path = choose_existing_or_save("notes");
    if (path.empty())
      return;
    write_atomic_utf8(path, content);
    paths["notes"] = path;
    saved["notes"] = encode_state("notes");
  }
};
#ifndef UNLLM_APP_LIBRARY
int main(int argc, char **argv) {
  QApplication application(argc, argv);
  application.setStyle("Fusion");
  ApplicationWindow window;
  window.show();
  return application.exec();
}
#endif
