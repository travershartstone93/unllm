#include <QApplication>
#include <QPushButton>
#include <QSpinBox>
#include <QTest>
#include <QVBoxLayout>
#include <iostream>
int main(int argc, char **argv) {
  QApplication application(argc, argv);
  QWidget window;
  QVBoxLayout layout(&window);
  QSpinBox value;
  QPushButton action("Change");
  value.setRange(-1000, 1000);
  value.setValue(27);
  layout.addWidget(&value);
  layout.addWidget(&action);
  QObject::connect(&action, &QPushButton::clicked,
                   [&] { value.setValue(value.value() - 11); });
  window.show();
  application.processEvents();
  QTest::mouseClick(&action, Qt::LeftButton);
  application.processEvents();
  if (value.value() != 16) {
    std::cerr << "Native Qt event/state probe failed\n";
    return 1;
  }
  std::cout << "Native Qt widget construction and input event passed\n";
  return 0;
}
