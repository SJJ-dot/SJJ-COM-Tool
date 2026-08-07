#pragma once
// 主题化复选框：QSS 绘制 indicator 容器（背景+边框），paintEvent 手动绘制白色对勾
// （绕开 Qt6 中 QSS image:url 在 sub-control 上不渲染的兼容问题）。

#include <QCheckBox>

#include "themes.h"

class ThemeCheckBox : public QCheckBox {
    Q_OBJECT
public:
    explicit ThemeCheckBox(const QString& text = QString(), QWidget* parent = nullptr);
    void setThemeColors(const sjj::ThemeColors& t);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void refreshStyle();

    sjj::ThemeColors m_tc;
};
