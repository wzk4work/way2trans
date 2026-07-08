#include "MainWindow.h"

#include <QLabel>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("way2trans"));
    resize(640, 480);

    auto *placeholder = new QLabel(tr("way2trans"), this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);
}
