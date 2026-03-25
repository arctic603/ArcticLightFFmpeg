#include "FFmpegArc.h"
#include <QtWidgets/QApplication>


int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    ArcticLight arctic;
    arctic.show();
    return app.exec();
}

