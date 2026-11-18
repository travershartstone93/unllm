// Independent GUI/file probe. No generated test driver or outcome oracle.
#define UNLLM_APP_LIBRARY
#include "program.cpp"
#include <QtTest/QTest>
#include <QtGui/QClipboard>
#include <iostream>

int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setStyle("Fusion");
    std::string input((std::istreambuf_iterator<char>(std::cin)),std::istreambuf_iterator<char>());
    auto request=QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
    auto choices=request["dialogs"].toObject();int save_calls=0;
    auto take=[&](const QString& key,const std::string& fallback="") {
        auto values=choices[key].toArray();if(values.isEmpty())return fallback;
        auto result=values.takeAt(0).toString();choices[key]=values;return bytes(result);
    };
    Dialogs dialogs;
    dialogs.open_path=[&](QWidget*){return take("open");};
    dialogs.save_path=[&](QWidget*){++save_calls;return take("save");};
    dialogs.confirm=[&](QWidget*,const std::string&){return take("confirm","cancel");};
    ApplicationWindow window(dialogs);window.show();app.processEvents();
    QJsonArray checks;std::map<QString,double> captures;bool all=true;int index=0;
    auto control=[&](const QString& name) {
        auto* widget=window.findChild<QWidget*>(name);
        if(!widget)throw std::runtime_error("Missing visible control: "+bytes(name));return widget;
    };
    auto text_of=[](QWidget* widget)->QString {
        if(auto* edit=qobject_cast<QPlainTextEdit*>(widget))return edit->toPlainText();
        if(auto* edit=qobject_cast<QLineEdit*>(widget))return edit->text();
        if(auto* label=qobject_cast<QLabel*>(widget))return label->text();
        throw std::runtime_error("Expected a text control");
    };
    auto number_of=[&](QWidget* widget) {
        if(auto* spin=qobject_cast<QDoubleSpinBox*>(widget))return spin->value();
        bool ok=false;double result=text_of(widget).section(": ",-1).toDouble(&ok);
        if(!ok)throw std::runtime_error("Expected a numeric display");return result;
    };
    for(const auto& raw:request["events"].toArray()) {
        auto event=raw.toObject();auto kind=event["kind"].toString();QJsonObject check;bool passed=true;
        try {
            QJsonValue actual,expected;
            if(kind=="enter"||kind=="append") {
                auto* widget=control(event["target"].toString());widget->setFocus();
                QTest::keyClick(widget,kind=="enter"?Qt::Key_A:Qt::Key_End,Qt::ControlModifier);
                auto text=event["text"].toString();bool paste=false;
                for(auto c:text)paste|=c.unicode()>126||c=='\n'||c=='\r';
                if(paste){app.clipboard()->setText(text);QTest::keyClick(widget,Qt::Key_V,Qt::ControlModifier);}
                else QTest::keyClicks(widget,text);
                if(qobject_cast<QDoubleSpinBox*>(widget))QTest::keyClick(widget,Qt::Key_Return);
            } else if(kind=="click") {
                auto* button=qobject_cast<QPushButton*>(control(event["target"].toString()));
                if(!button)throw std::runtime_error("Expected a button");QTest::mouseClick(button,Qt::LeftButton);
            } else if(kind=="select"||kind=="check") {
                auto* list=qobject_cast<QListWidget*>(control(event["target"].toString()));
                if(!list)throw std::runtime_error("Expected a list");auto* item=list->item(event["row"].toInt());
                if(!item)throw std::runtime_error("Missing record row");
                QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(item).center());
                if(kind=="check"&&(item->checkState()==Qt::Checked)!=event["value"].toBool()) {
                    list->setFocus();QTest::keyClick(list,Qt::Key_Space);
                }
            } else if(kind=="wait")QTest::qWait(event["milliseconds"].toInt());
            else if(kind=="capture")captures[event["name"].toString()]=number_of(control(event["target"].toString()));
            else if(kind=="close")window.close();
            else if(kind=="expect_text") {
                actual=text_of(control(event["target"].toString()));expected=event["value"];passed=actual==expected;
            } else if(kind=="expect_number") {
                double value=number_of(control(event["target"].toString()));actual=value;
                if(event.contains("capture")) {double target=captures.at(event["capture"].toString());expected=target;passed=value==target;}
                else if(event.contains("greater_than_capture")) {double target=captures.at(event["greater_than_capture"].toString());expected=target;passed=value>target;}
                else if(event.contains("minimum")) {expected=QJsonArray{event["minimum"],event["maximum"]};passed=value>=event["minimum"].toDouble()&&value<=event["maximum"].toDouble();}
                else {double target=event["value"].toDouble();expected=target;passed=std::isfinite(value)&&std::abs(value-target)<=1e-9+1e-10*std::abs(target);}
            } else if(kind=="expect_items") {
                auto* list=qobject_cast<QListWidget*>(control(event["target"].toString()));
                if(!list)throw std::runtime_error("Expected a list");QJsonArray values;
                for(int i=0;i<list->count();++i)values.append(list->item(i)->text());actual=values;expected=event["value"];passed=actual==expected;
            } else if(kind=="expect_file") {
                QFile file(event["path"].toString());if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Expected saved file");
                auto content=file.readAll();expected=event["value"];
                if(expected.isArray())actual=QJsonDocument::fromJson(content).array();
                else if(expected.isObject())actual=QJsonDocument::fromJson(content).object();
                else actual=QString::fromUtf8(content);
                passed=actual==expected;
            } else if(kind=="expect_save_calls") {actual=save_calls;expected=event["value"];passed=actual==expected;}
            else if(kind=="expect_dirty") {actual=window.isWindowModified();expected=event["value"];passed=actual==expected;}
            else if(kind=="expect_visible") {actual=window.isVisible();expected=event["value"];passed=actual==expected;}
            else if(kind=="expect_error") {actual=!text_of(control("status")).isEmpty();expected=event["value"];passed=actual==expected;}
            else throw std::runtime_error("Unknown independent probe event");
            app.processEvents();
            if(kind.startsWith("expect")) {
                check["event"]=index;check["kind"]=kind;check["actual"]=actual;check["expected"]=expected;check["passed"]=passed;checks.append(check);all&=passed;
            }
        } catch(const std::exception& error) {
            check["event"]=index;check["kind"]=kind;check["error"]=QString::fromUtf8(error.what());check["passed"]=false;
            checks.append(check);all=false;break;
        }
        ++index;
    }
    window.grab().save("/output/independent-preview.png");
    window.dialogs.confirm=[](QWidget*,const std::string&){return std::string("discard");};window.close();
    QJsonObject result;result["passed"]=all&&!checks.isEmpty();result["checks"]=checks;
    std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<'\n';
}
