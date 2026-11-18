// Generic Qt widget and event primitives; independent of app business logic.
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtGui/QKeyEvent>
#include <functional>
#include <map>
#include <vector>

class GenericWindow:public QWidget {
    QGridLayout* grid;
    std::vector<std::function<void()>> updates;
    std::map<std::string,std::function<void()>> keys;
public:
    GenericWindow():grid(new QGridLayout(this)) {
        setMinimumSize(320,360);setFocusPolicy(Qt::StrongFocus);
        setStyleSheet("QPushButton {min-height:40px;font-size:18px;} QLineEdit {font-size:24px;min-height:52px;} QLabel {font-size:12px;}");
    }
    void add_display(const std::string& identifier,const std::string& kind,int row,int col,int span,std::function<std::string()> getter) {
        if(kind=="display") {
            auto* widget=new QLineEdit;widget->setObjectName(QString::fromStdString(identifier));
            widget->setReadOnly(true);widget->setAlignment(Qt::AlignRight);widget->setFocusPolicy(Qt::NoFocus);
            grid->addWidget(widget,row,col,1,span);
            updates.push_back([widget,getter]{widget->setText(QString::fromStdString(getter()));});
        } else {
            auto* widget=new QLabel;widget->setObjectName(QString::fromStdString(identifier));
            grid->addWidget(widget,row,col,1,span);
            updates.push_back([widget,getter]{widget->setText(QString::fromStdString(getter()));});
        }
    }
    void add_button(const std::string& identifier,const std::string& label,int row,int col,std::function<void()> handler) {
        auto* button=new QPushButton(QString::fromStdString(label));button->setObjectName(QString::fromStdString(identifier));
        button->setFocusPolicy(Qt::NoFocus);
        QObject::connect(button,&QPushButton::clicked,this,[this,handler]{handler();refresh();});
        grid->addWidget(button,row,col);
    }
    void bind_key(const std::string& key,std::function<void()> handler) {keys[key]=handler;}
    void refresh() {for(auto& update:updates)update();}
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if(event->modifiers()&(Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier)) {QWidget::keyPressEvent(event);return;}
        std::string key=event->text().toStdString();
        switch(event->key()) {
            case Qt::Key_Return:key="Return";break;case Qt::Key_Enter:key="Enter";break;
            case Qt::Key_Escape:key="Escape";break;case Qt::Key_Backspace:key="Backspace";break;
            case Qt::Key_Delete:key="Delete";break;default:break;
        }
        auto found=keys.find(key);
        if(found==keys.end())QWidget::keyPressEvent(event);
        else {found->second();refresh();event->accept();}
    }
};
