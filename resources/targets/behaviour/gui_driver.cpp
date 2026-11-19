// External Qt Test harness; observation.h is generated from state types.
#define UNLLM_APP_LIBRARY
#include "program.cpp"
#include <QtTest/QTest>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <iostream>
#include "observation.h"

int main(int argc,char** argv) {
    QApplication application(argc,argv);application.setStyle("Fusion");
    std::string raw((std::istreambuf_iterator<char>(std::cin)),std::istreambuf_iterator<char>());
    auto request=QJsonDocument::fromJson(QByteArray::fromStdString(raw)).object();
    QJsonArray results;int index=0;
    for(const auto& item:request["scenarios"].toArray()) {
        auto scenario=item.toObject();auto choices=scenario["dialogs"].toObject();
        auto take=[&](const QString& key,const std::string& fallback="") {
            auto values=choices[key].toArray();if(values.isEmpty())return fallback;
            auto value=values.takeAt(0).toString();choices[key]=values;return bytes(value);
        };
        Dialogs dialogs;
        dialogs.open_path=[&](QWidget*){return take("open");};
        dialogs.save_path=[&](QWidget*){return take("save");};
        dialogs.confirm=[&](QWidget*,const std::string&){return take("confirm","cancel");};
        ApplicationWindow window(dialogs);
        initialize_state(window,scenario["initial"].toObject());
        window.refresh();window.show();application.processEvents();
        QJsonArray trace;
        for(const auto& event_value:scenario["events"].toArray()) {
            auto event=event_value.toObject();
            if(event.contains("input")||event.contains("type")) {
                auto id=event.contains("input")?event["input"].toString():event["type"].toString();
                auto* edit=window.findChild<QPlainTextEdit*>(id);auto* line=window.findChild<QLineEdit*>(id);
                auto text=event["text"].toString();
                if(!edit&&!line)throw std::runtime_error("Missing editable widget");
                if(edit) {
                    edit->setFocus();
                    if(event.contains("type"))QTest::keyClicks(edit,text);
                    else {edit->selectAll();edit->insertPlainText(text);}
                } else {
                    line->setFocus();
                    if(event.contains("type"))QTest::keyClicks(line,text);
                    else {line->selectAll();line->insert(text);}
                }
            } else if(event.contains("number")) {
                auto* spin=window.findChild<QDoubleSpinBox*>(event["number"].toString());
                if(!spin)throw std::runtime_error("Missing numeric widget");
                spin->setFocus();spin->selectAll();
                auto text=event.contains("text")?event["text"].toString():QString::number(event["value"].toDouble(),'g',15);
                QTest::keyClicks(spin,text);QTest::keyClick(spin,Qt::Key_Return);
            } else if(event.contains("button")) {
                auto* button=window.findChild<QPushButton*>(event["button"].toString());
                if(!button)throw std::runtime_error("Missing button");QTest::mouseClick(button,Qt::LeftButton);
            } else if(event.contains("key")) {
                auto id=event["target"].toString();QWidget* widget=window.findChild<QWidget*>(id);
                if(!widget)throw std::runtime_error("Missing shortcut widget");
                widget->setFocus();QTest::keySequence(widget,QKeySequence(event["key"].toString()));
            } else if(event.contains("select")||event.contains("check")) {
                auto id=event.contains("select")?event["select"].toString():event["check"].toString();
                auto* widget=window.findChild<QListWidget*>(id);
                if(!widget)throw std::runtime_error("Missing record view");
                auto* item=widget->item(event["row"].toInt());if(!item)throw std::runtime_error("Missing record row");
                QTest::mouseClick(widget->viewport(),Qt::LeftButton,Qt::NoModifier,widget->visualItemRect(item).center());
                if(event.contains("check")&&(item->checkState()==Qt::Checked)!=event["value"].toBool()) {
                    widget->setFocus();QTest::keyClick(widget,Qt::Key_Space);
                }
            } else if(event.contains("wait_ms"))QTest::qWait(std::clamp(event["wait_ms"].toInt(),0,2000));
            else if(event["close"].toBool())window.close();
            else throw std::runtime_error("Unknown test event");
            application.processEvents();
            if(scenario["trace"].toBool())trace.append(observe_state(window));
        }
        QJsonObject labels,paths;
        for(auto* label:window.findChildren<QLabel*>())if(!label->objectName().isEmpty())labels[label->objectName()]=label->text();
        for(const auto& [name,path]:window.paths)paths[qtext(name)]=qtext(path);
        QJsonObject observation;observation["state"]=observe_state(window);observation["labels"]=labels;
        observation["paths"]=paths;observation["error"]=qtext(window.error_message);
        observation["dirty"]=window.isWindowModified();observation["visible"]=window.isVisible();observation["trace"]=trace;results.append(observation);
        if(index++==0&&request["preview"].toBool())window.grab().save("/output/preview.png");
        window.dialogs.confirm=[](QWidget*,const std::string&){return std::string("discard");};
        window.close();application.processEvents();
    }
    std::cout<<QJsonDocument(results).toJson(QJsonDocument::Compact).constData()<<'\n';
}
