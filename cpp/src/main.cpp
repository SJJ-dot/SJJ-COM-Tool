#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QFileInfo>
#include <QCoreApplication>
#include <QtPlugin>

#include "serialtool.h"

// ===== 静态链接 Qt 时显式导入插件（由 CMake 定义 SJJ_STATIC_BUILD） =====
#ifdef SJJ_STATIC_BUILD
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)      // Windows 平台集成（必需）
Q_IMPORT_PLUGIN(QModernWindowsStylePlugin)      // Windows 原生样式
#endif

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SJJ-COM-Tool"));
    // 统一控件文字字号 = 接收区数据文本字号（10pt），字体族保持系统默认（中文正常显示）
    QFont f = app.font();
    f.setPointSize(10);
    app.setFont(f);
    // 应用图标（窗口标题栏 + 任务栏）：优先内嵌资源，其次程序目录/项目 imgs 目录
    QStringList candidates = {
        QStringLiteral(":/imgs/ic_xue_xi.png"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/imgs/ic_xue_xi.png"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../imgs/ic_xue_xi.png"),
    };
    for (const QString& p : candidates) {
        if (QFileInfo::exists(p)) {
            app.setWindowIcon(QIcon(p));
            break;
        }
    }
    SerialTool win;
    if (!app.windowIcon().isNull())
        win.setWindowIcon(app.windowIcon());
    win.show();
    return app.exec();
}
