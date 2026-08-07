#include "themes.h"

namespace sjj {

ThemeColors themeDark() {
    ThemeColors t;
    t["name"]            = QStringLiteral("深色");
    t["win_bg"]          = QStringLiteral("#1E1E2E");       // 主窗口背景（paintEvent 绘制）
    t["titlebar_bg"]     = QStringLiteral("#181825");       // 标题栏背景
    t["titlebar_fg"]     = QStringLiteral("#CDD6F4");       // 标题栏文字/按钮
    t["titlebar_hover"]  = QStringLiteral("rgba(49,50,68,200)");
    t["titlebar_border"] = QStringLiteral("rgba(49,50,68,200)");
    t["close_hover"]     = QStringLiteral("#D20F39");       // 关闭按钮悬停
    t["panel_bg"]        = QStringLiteral("#24273A");       // GroupBox 背景
    t["panel_border"]    = QStringLiteral("rgba(49,50,68,180)");
    t["edit_bg"]         = QStringLiteral("#181825");       // 接收/发送区背景
    t["edit_send_bg"]    = QStringLiteral("#181825");       // 发送框专用背景（与 edit_bg 同）
    t["edit_fg"]         = QStringLiteral("#CDD6F4");
    t["edit_sel"]        = QStringLiteral("rgba(69,71,90,200)");
    t["tab_pane"]        = QStringLiteral("#1E1E2E");
    t["tab_sel_bg"]      = QStringLiteral("#313244");
    t["tab_sel_fg"]      = QStringLiteral("#CDD6F4");
    t["tab_uns_fg"]      = QStringLiteral("#6C7086");
    t["tab_hover"]       = QStringLiteral("rgba(49,50,68,120)");
    t["status_bg"]       = QStringLiteral("#11111B");
    t["status_fg"]       = QStringLiteral("#9399B2");
    t["status_border"]   = QStringLiteral("rgba(49,50,68,200)");
    t["text_primary"]    = QStringLiteral("#CDD6F4");
    t["text_secondary"]  = QStringLiteral("#7F849C");
    t["btn_bg"]          = QStringLiteral("#313244");
    t["btn_hover"]       = QStringLiteral("#45475A");
    t["btn_fg"]          = QStringLiteral("#CDD6F4");
    t["input_bg"]        = QStringLiteral("#181825");
    t["input_border"]    = QStringLiteral("#45475A");
    t["combo_item_bg"]   = QStringLiteral("#1E1E2E");
    t["combo_item_sel"]  = QStringLiteral("#313244");
    t["list_bg"]         = QStringLiteral("#181825");
    t["list_sel"]        = QStringLiteral("#313244");
    t["menu_bg"]         = QStringLiteral("#1E1E2E");
    t["menu_sel"]        = QStringLiteral("#313244");
    t["menu_fg"]         = QStringLiteral("#CDD6F4");
    t["scrollbar_handle"]= QStringLiteral("#45475A");
    t["scrollbar_hover"] = QStringLiteral("#585B70");
    t["ok_color"]        = QStringLiteral("#A6E3A1");
    t["err_color"]       = QStringLiteral("#F38BA8");
    t["cs_border"]       = QStringLiteral("#FAB387");       // 校验高亮边框（橙）
    t["cs_bg"]           = QStringLiteral("rgba(250,179,135,45)");
    t["link_color"]      = QStringLiteral("#89B4FA");
    t["accent"]          = QStringLiteral("#89B4FA");
    t["progress_bg"]     = QStringLiteral("#24273A");   // 进度条未填充背景（深色主题）
    t["progress_chunk"]  = QStringLiteral("#1E66F5");   // 进度条填充（深色主题：深蓝，配白字）
    t["progress_text"]   = QStringLiteral("#FFFFFF");   // 进度条文字（深色主题：白字）
    return t;
}

ThemeColors themeLight() {
    ThemeColors t;
    t["name"]            = QStringLiteral("浅色");
    t["win_bg"]          = QStringLiteral("#EFF1F5");
    t["titlebar_bg"]     = QStringLiteral("#E6E9EF");
    t["titlebar_fg"]     = QStringLiteral("#4C4F69");
    t["titlebar_hover"]  = QStringLiteral("rgba(204,208,218,120)");
    t["titlebar_border"] = QStringLiteral("rgba(204,208,218,180)");
    t["close_hover"]     = QStringLiteral("#D20F39");
    t["panel_bg"]        = QStringLiteral("#FFFFFF");
    t["panel_border"]    = QStringLiteral("rgba(204,208,218,180)");
    t["edit_bg"]         = QStringLiteral("#FFFFFF");
    t["edit_send_bg"]    = QStringLiteral("#EFF1F5");       // 发送框专用背景（浅色主题）
    t["edit_fg"]         = QStringLiteral("#4C4F69");
    t["edit_sel"]        = QStringLiteral("rgba(188,192,204,180)");
    t["tab_pane"]        = QStringLiteral("#EFF1F5");
    t["tab_sel_bg"]      = QStringLiteral("#FFFFFF");
    t["tab_sel_fg"]      = QStringLiteral("#4C4F69");
    t["tab_uns_fg"]      = QStringLiteral("#7C7F93");
    t["tab_hover"]       = QStringLiteral("rgba(204,208,218,120)");
    t["status_bg"]       = QStringLiteral("#DCE0E8");
    t["status_fg"]       = QStringLiteral("#5C5F77");
    t["status_border"]   = QStringLiteral("rgba(204,208,218,180)");
    t["text_primary"]    = QStringLiteral("#4C4F69");
    t["text_secondary"]  = QStringLiteral("#7C7F93");
    t["btn_bg"]          = QStringLiteral("#FFFFFF");
    t["btn_hover"]       = QStringLiteral("#EFF1F5");
    t["btn_fg"]          = QStringLiteral("#4C4F69");
    t["input_bg"]        = QStringLiteral("#FFFFFF");
    t["input_border"]    = QStringLiteral("#CCD0DA");
    t["combo_item_bg"]   = QStringLiteral("#FFFFFF");
    t["combo_item_sel"]  = QStringLiteral("#E6E9EF");
    t["list_bg"]         = QStringLiteral("#FFFFFF");
    t["list_sel"]        = QStringLiteral("#E6E9EF");
    t["menu_bg"]         = QStringLiteral("#FFFFFF");
    t["menu_sel"]        = QStringLiteral("#E6E9EF");
    t["menu_fg"]         = QStringLiteral("#4C4F69");
    t["scrollbar_handle"]= QStringLiteral("#CCD0DA");
    t["scrollbar_hover"] = QStringLiteral("#BCC0CC");
    t["ok_color"]        = QStringLiteral("#40A02B");
    t["err_color"]       = QStringLiteral("#D20F39");
    t["cs_border"]       = QStringLiteral("#FE640B");
    t["cs_bg"]           = QStringLiteral("#FFF3E0");
    t["link_color"]      = QStringLiteral("#1E66F5");
    t["accent"]          = QStringLiteral("#1E66F5");
    t["progress_bg"]     = QStringLiteral("#DCE0E8");   // 进度条未填充背景（浅色主题：浅灰）
    t["progress_chunk"]  = QStringLiteral("#89B4FA");   // 进度条填充（浅色主题：浅蓝，配深字）
    t["progress_text"]   = QStringLiteral("#4C4F69");   // 进度条文字（浅色主题：深灰字）
    return t;
}

const ThemeColors& theme(const QString& name) {
    static const ThemeColors dark = themeDark();
    static const ThemeColors light = themeLight();
    return (name == QStringLiteral("dark")) ? dark : light;
}

// 全局 QSS 模板（占位符 {key} 由 buildQss 替换为主题色值）
static const char* QSS_TEMPLATE = R"(
#titleBar{background-color:{titlebar_bg};border-bottom:1px solid {titlebar_border};}
#titleBar QLabel{color:{titlebar_fg};background:transparent;}
#titleBar QToolButton{background:transparent;border:none;color:{titlebar_fg};padding:0;font-size:14px;font-family:Consolas;}
#titleBar QToolButton:hover{background:{titlebar_hover};}
#titleBar QToolButton#btn_close:hover{background:{close_hover};color:white;}
QWidget{background-color:{win_bg};color:{text_primary};}
QLabel{background:transparent;color:{text_primary};}
QGroupBox{background-color:{panel_bg};border:1px solid {panel_border};border-radius:8px;}
QPushButton{background-color:{btn_bg};color:{btn_fg};border:1px solid {input_border};border-radius:5px;padding:3px 6px;}
QPushButton:hover{background-color:{btn_hover};}
QPushButton:pressed{background-color:{panel_border};}
QPushButton:disabled{color:{text_secondary};}
QLineEdit{background-color:{input_bg};color:{text_primary};border:1px solid {input_border};border-radius:5px;padding:2px 6px;}
QLineEdit:focus{border-color:{accent};}
QComboBox{background-color:{input_bg};color:{text_primary};border:1px solid {input_border};border-radius:5px;padding:2px 8px;}
QComboBox:hover{border-color:{accent};}
QComboBox::drop-down{border:none;subcontrol-origin:padding;subcontrol-position:top right;width:20px;}
QComboBox::down-arrow{image:none;width:0;height:0;}
QComboBox QAbstractItemView{background-color:{combo_item_bg};color:{text_primary};selection-background-color:{combo_item_sel};border:1px solid {input_border};}
QTextEdit{background-color:{edit_bg};color:{edit_fg};selection-background-color:{edit_sel};border:none;}
QTextEdit#txt_send{background-color:{edit_send_bg};border:1px solid {input_border};border-radius:5px;padding:3px 6px;}
QProgressBar{background-color:{progress_bg};border:1px solid {input_border};border-radius:3px;text-align:center;color:{progress_text};}
QProgressBar::chunk{background-color:{progress_chunk};border-radius:2px;margin:1px;}
QTabWidget::pane{background-color:{tab_pane};border:1px solid {panel_border};border-radius:6px;top:-1px;}
QTabBar::tab{background:transparent;padding:5px 12px;color:{tab_sel_fg};}
QTabBar::tab:selected{background:{tab_sel_bg};border:1px solid {panel_border};border-bottom:none;border-top-left-radius:6px;border-top-right-radius:6px;}
QTabBar::tab:!selected{background:transparent;color:{tab_uns_fg};}
QTabBar::tab:hover:!selected{background:{tab_hover};}
QListWidget{background-color:{list_bg};color:{text_primary};border:1px solid {input_border};border-radius:5px;}
QListWidget::item{padding:2px 6px;}
QListWidget::item:selected{background-color:{list_sel};color:{text_primary};}
QScrollArea{border:none;background:transparent;}
QScrollArea > QWidget > QWidget{background:transparent;}
QMenu{background-color:{menu_bg};color:{menu_fg};border:1px solid {input_border};}
QMenu::item{padding:4px 18px;background:transparent;}
QMenu::item:selected{background-color:{menu_sel};}
QScrollBar:vertical{background:transparent;width:10px;margin:0;}
QScrollBar::handle:vertical{background:{scrollbar_handle};border-radius:5px;min-height:24px;}
QScrollBar::handle:vertical:hover{background:{scrollbar_hover};}
QScrollBar:horizontal{background:transparent;height:10px;margin:0;}
QScrollBar::handle:horizontal{background:{scrollbar_handle};border-radius:5px;min-width:24px;}
QScrollBar::add-line, QScrollBar::sub-line{height:0;width:0;}
QScrollBar::add-page, QScrollBar::sub-page{background:transparent;}
QToolTip{background-color:{menu_bg};color:{menu_fg};border:1px solid {input_border};}
#statusBar{background-color:{status_bg};color:{status_fg};border-top:1px solid {status_border};}
#statusBar QLabel{color:{status_fg};background:transparent;}
QFrame#cs_group{border:none;background:transparent;}
QFrame#cs_group[cs_highlight="true"]{border:1px solid {cs_border};border-radius:3px;background:{cs_bg};}
)";

QString buildQss(const ThemeColors& c) {
    QString s = QString::fromUtf8(QSS_TEMPLATE);
    for (auto it = c.cbegin(); it != c.cend(); ++it)
        s.replace(QLatin1Char('{') + it.key() + QLatin1Char('}'), it.value());
    return s;
}

} // namespace sjj
