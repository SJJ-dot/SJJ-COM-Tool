#pragma once
// 主题系统：深浅双主题配色（参考 Catppuccin Mocha / Latte 提取），
// 数据结构与 PySide6 版 THEMES 字典一一对应。

#include <QMap>
#include <QString>

namespace sjj {

using ThemeColors = QMap<QString, QString>;

// 深色主题（Catppuccin Mocha）
ThemeColors themeDark();
// 浅色主题（Catppuccin Latte）
ThemeColors themeLight();

// 返回指定主题（不存在时回退浅色）
const ThemeColors& theme(const QString& name);

// 生成全局 QSS（模板中 {key} 占位符替换为主题色值）
QString buildQss(const ThemeColors& c);

} // namespace sjj
