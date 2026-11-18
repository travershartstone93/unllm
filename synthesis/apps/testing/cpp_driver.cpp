// Qt Test drives the actual generated window and controller.
#define UNLLM_APP_LIBRARY
#include "program.cpp"
#include <QtTest/QTest>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QFile>
#include <iostream>

int main(int argc,char** argv) {
    QApplication application(argc,argv);application.setStyle("Fusion");
    std::string input((std::istreambuf_iterator<char>(std::cin)),std::istreambuf_iterator<char>());
    auto request=QJsonDocument::fromJson(QByteArray::fromStdString(input)).object();
    QJsonObject results;QJsonArray expressions,scenarios;
    for(const auto& item:request["expressions"].toArray()) {
        QJsonObject result;
        try {result["value"]=evaluate_expression(item.toString().toStdString());result["error"]="";}
        catch(const std::exception& error) {result["value"]=QJsonValue::Null;result["error"]=QString::fromUtf8(error.what());}
        expressions.append(result);
    }
    const std::map<QString,Qt::Key> named{{"Return",Qt::Key_Return},{"Enter",Qt::Key_Enter},
        {"Escape",Qt::Key_Escape},{"Backspace",Qt::Key_Backspace},{"Delete",Qt::Key_Delete}};
    int index=0;
    for(const auto& item:request["gui"].toArray()) {
        ApplicationWindow window;window.show();application.processEvents();QJsonArray trace;
        for(const auto& raw:item.toObject()["events"].toArray()) {
            auto event=raw.toObject();
            if(event.contains("button")) {
                auto* button=window.findChild<QPushButton*>(event["button"].toString());
                if(!button) {std::cerr<<"Missing button\n";return 1;}
                QTest::mouseClick(button,Qt::LeftButton);
            } else if(event.contains("text"))QTest::keyClicks(&window,event["text"].toString());
            else QTest::keyClick(&window,named.at(event["key"].toString()));
            application.processEvents();trace.append(window.findChild<QLineEdit*>("display")->text());
        }
        QJsonObject result;result["display"]=window.findChild<QLineEdit*>("display")->text();
        result["error"]=QString::fromStdString(window.controller.error);
        result["title"]=window.windowTitle();result["trace"]=trace;
        QJsonArray buttons;QStringList ids;
        for(auto* button:window.findChildren<QPushButton*>())ids.push_back(button->objectName());
        ids.sort();for(const auto& id:ids)buttons.append(id);
        result["buttons"]=buttons;
        // Memory is observed through its rendered label, keeping the driver
        // compatible with application graphs that have no memory field.
        auto* memory=window.findChild<QLabel*>("memory_display");
        result["memory_text"]=memory?memory->text():QString();
        scenarios.append(result);
        if(index++==0 && request["preview"].toBool())window.grab().save("/output/preview.png");
        window.close();application.processEvents();
    }
    results["expressions"]=expressions;results["gui"]=scenarios;results["toolkit"]=qVersion();
    std::cout<<QJsonDocument(results).toJson(QJsonDocument::Compact).constData()<<'\n';
}
