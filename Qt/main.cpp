#include <QApplication>
#include "prismmainwindow.h"
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    PRISMMainWindow w;
    w.show();

    return a.exec();
}