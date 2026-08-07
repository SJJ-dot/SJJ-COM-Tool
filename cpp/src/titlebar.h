#pragma once
// 自绘标题栏（无边框窗口用）：颜色完全可控，不受系统失焦变白影响。
// 含图标、标题（版本号，点击检查更新）、主题切换、最小化/最大化/关闭按钮，
// 支持拖动、双击最大化、右键菜单。

#include <QWidget>
#include <QLabel>
#include <QToolButton>

#include "themes.h"

class QMouseEvent;
class QContextMenuEvent;

class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget* window);

    void applyTheme(const sjj::ThemeColors& t, const QString& themeName);

signals:
    void versionClicked();   // 点击版本号（检查更新）
    void themeClicked();     // 点击主题切换按钮

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    void toggleMax();

    QWidget* m_win = nullptr;          // 顶层窗口引用
    QLabel* m_lblIcon = nullptr;
    QLabel* m_lblTitle = nullptr;
    QToolButton* m_btnTheme = nullptr;
    QToolButton* m_btnMin = nullptr;
    QToolButton* m_btnMax = nullptr;
    QToolButton* m_btnClose = nullptr;
    bool m_pressed = false;
    QPoint m_dragPos;
};
