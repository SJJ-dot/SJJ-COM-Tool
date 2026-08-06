"""
SJJ‑COM Tool - 串口调试工具（PySide6 版）

为什么用 PySide6：tkinter 在 Windows 上更新接收区会干扰输入法候选框（平台限制），
Qt 原生集成 Windows IMM32，输入控件与输入法协同正常——接收区实时刷新的同时，
在输入框打字输入法候选框不会乱跳（与 SSCOM 行为一致）。

功能（与 tkinter 版对齐）：
  - 串口打开/关闭；波特率/数据位/停止位/校验位/流控/读超时（更多串口设置弹窗）
  - HEX/文本双模式收发；HEX 显示；时间戳+分包显示；接收编码切换（UTF-8/GBK/...）
  - 暂停刷新：勾选=停止自动滚动；滚轮上翻/拖动滚动条自动勾选，滚回底部自动取消
  - 接收区高亮（搜索关键字）；筛选（只显示匹配消息）；接收内容存内存（上限 5000）
  - 加校验（None/0-ADD8/ADD8/XOR8/ADD16/ModbusCRC16/CCITT-CRC16/CRC32），
    第[x]字节至[x]结束位置（负数=末尾偏移），结果追加到数据末尾，实时显示计算结果
  - 多字符串/历史窗口（右侧 Tab，默认隐藏）：历史记录（双击载入发送框）、
    快捷命令（每行 HEX 勾选/可编辑/按钮文字可右键改/可循环发送）
  - 发送历史自动记录（去重、置顶重排）；发送框文本/HEX 内容双槽记忆
  - 文件发送（直接发送，发送中可停止，支持加校验）；定时发送
  - 参数自动保存/加载到程序目录 sscom_config.json
  - 默认不拉高 DTR/RTS，避免开发板复位/进入下载模式
  - 状态栏：连接状态 + 收发计数 + 握手信号；右下角应用名可点击打开 GitHub
"""

import os
import re
import sys
import time
import struct
import zlib
import json
import base64
import webbrowser
import urllib.request
import urllib.error
from datetime import datetime

import serial
import serial.tools.list_ports

from PySide6.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLabel,
    QComboBox, QPushButton, QCheckBox, QLineEdit, QTextEdit,
    QTabWidget, QListWidget, QListWidgetItem, QFileDialog, QInputDialog,
    QMessageBox, QDialog, QFormLayout, QSpinBox, QScrollArea, QFrame,
    QSizePolicy, QGroupBox, QGridLayout, QStyledItemDelegate, QMenu,
    QToolButton, QGraphicsDropShadowEffect, QStyle, QStyleOptionButton,
    QStyleOptionFocusRect, QStyleOptionComboBox,
)
from PySide6.QtCore import QPoint, QThread, Signal, Qt, QTimer, QUrl, QEvent, QRectF, QByteArray, QSize, QBuffer, QIODevice
from PySide6.QtGui import QTextCursor, QColor, QTextCharFormat, QDesktopServices, QFont, QIcon, QPainter, QPainterPath, QPen, QBrush, QRegion, QPixmap, QPolygon, QPalette
from PySide6.QtSvg import QSvgRenderer

APP_TITLE = "SJJ‑COM Tool"
# 版本号：GitHub Actions 打包时通过环境变量 SJJ_COM_VERSION 注入（如 v1.0.0），本地默认 dev
APP_VERSION = os.environ.get("SJJ_COM_VERSION", "dev")
GITHUB_URL = "https://github.com/SJJ-dot/SJJ-COM-Tool.git"
GITHUB_REPO = "SJJ-dot/SJJ-COM-Tool"          # 更新检查用（owner/repo）
UPDATE_CHECK_URL = "https://api.github.com/repos/SJJ-dot/SJJ-COM-Tool/releases/latest"
MAX_RECORDS = 5000


def _version_tuple(v: str):
    """把版本号字符串转成可比较元组，如 'v1.2.3' / '1.2.3-beta' → (1,2,3)。"""
    parts = []
    for seg in re.split(r"[.\-_]", str(v).lstrip("vV")):
        if seg.isdigit():
            parts.append(int(seg))
        else:
            break
    while len(parts) < 3:
        parts.append(0)
    return tuple(parts[:3])


def _is_newer(latest: str, current: str) -> bool:
    """latest 是否比 current 新。dev 视为 0.0.0，任何正式版都更新。"""
    return _version_tuple(latest) > _version_tuple(current)

DEFAULT_BAUDRATES = [
    "1200", "2400", "4800", "9600", "14400", "19200",
    "38400", "57600", "115200", "128000", "230400",
    "256000", "460800", "500000", "921600", "1000000",
    "1500000", "2000000",
]
DEFAULT_PARITY = ["None", "Even", "Odd", "Mark", "Space"]
DEFAULT_STOPBITS = ["1", "1.5", "2"]
DEFAULT_DATABITS = ["5", "6", "7", "8"]
DEFAULT_FLOW = ["None", "RTS/CTS", "XON/XOFF"]
FLOW_SHORT = {"None": "None", "RTS/CTS": "RTS", "XON/XOFF": "XON"}

CHECKSUMS = ["None", "0-ADD8", "ADD8", "XOR8", "ADD16",
             "ModbusCRC16", "CCITT-CRC16", "CRC32"]
ENCODINGS = ["UTF-8", "GBK", "GB2312", "ASCII", "latin-1", "UTF-16LE", "UTF-16BE"]

MONO_FONT = QFont("Consolas", 10)

# ================= 主题系统 =================
# 深浅双主题配色（参考 Catppuccin Mocha / Latte 提取）
THEMES = {
    "dark": {
        "name": "深色",
        "win_bg": "#1E1E2E",            # 主窗口背景（paintEvent 绘制）
        "titlebar_bg": "#181825",       # 标题栏背景
        "titlebar_fg": "#CDD6F4",       # 标题栏文字/按钮
        "titlebar_hover": "rgba(49,50,68,200)",
        "titlebar_border": "rgba(49,50,68,200)",
        "close_hover": "#D20F39",       # 关闭按钮悬停
        "panel_bg": "#24273A",          # GroupBox 背景
        "panel_border": "rgba(49,50,68,180)",
        "edit_bg": "#181825",           # 接收/发送区 QTextEdit 背景
        "edit_fg": "#CDD6F4",
        "edit_sel": "rgba(69,71,90,200)",
        "tab_pane": "#1E1E2E",
        "tab_sel_bg": "#313244",
        "tab_sel_fg": "#CDD6F4",
        "tab_uns_fg": "#6C7086",
        "tab_hover": "rgba(49,50,68,120)",
        "status_bg": "#11111B",
        "status_fg": "#9399B2",
        "status_border": "rgba(49,50,68,200)",
        "text_primary": "#CDD6F4",
        "text_secondary": "#7F849C",
        "btn_bg": "#313244",
        "btn_hover": "#45475A",
        "btn_fg": "#CDD6F4",
        "input_bg": "#181825",
        "input_border": "#45475A",
        "combo_item_bg": "#1E1E2E",
        "combo_item_sel": "#313244",
        "list_bg": "#181825",
        "list_sel": "#313244",
        "menu_bg": "#1E1E2E",
        "menu_sel": "#313244",
        "menu_fg": "#CDD6F4",
        "scrollbar_handle": "#45475A",
        "scrollbar_hover": "#585B70",
        "ok_color": "#A6E3A1",
        "err_color": "#F38BA8",
        "cs_border": "#FAB387",         # 校验高亮边框（橙）
        "cs_bg": "rgba(250,179,135,45)",
        "link_color": "#89B4FA",
        "accent": "#89B4FA",
    },
    "light": {
        "name": "浅色",
        "win_bg": "#EFF1F5",
        "titlebar_bg": "#E6E9EF",
        "titlebar_fg": "#4C4F69",
        "titlebar_hover": "rgba(204,208,218,120)",
        "titlebar_border": "rgba(204,208,218,180)",
        "close_hover": "#D20F39",
        "panel_bg": "#FFFFFF",
        "panel_border": "rgba(204,208,218,180)",
        "edit_bg": "#FFFFFF",
        "edit_fg": "#4C4F69",
        "edit_sel": "rgba(188,192,204,180)",
        "tab_pane": "#EFF1F5",
        "tab_sel_bg": "#FFFFFF",
        "tab_sel_fg": "#4C4F69",
        "tab_uns_fg": "#7C7F93",
        "tab_hover": "rgba(204,208,218,120)",
        "status_bg": "#DCE0E8",
        "status_fg": "#5C5F77",
        "status_border": "rgba(204,208,218,180)",
        "text_primary": "#4C4F69",
        "text_secondary": "#7C7F93",
        "btn_bg": "#FFFFFF",
        "btn_hover": "#EFF1F5",
        "btn_fg": "#4C4F69",
        "input_bg": "#FFFFFF",
        "input_border": "#CCD0DA",
        "combo_item_bg": "#FFFFFF",
        "combo_item_sel": "#E6E9EF",
        "list_bg": "#FFFFFF",
        "list_sel": "#E6E9EF",
        "menu_bg": "#FFFFFF",
        "menu_sel": "#E6E9EF",
        "menu_fg": "#4C4F69",
        "scrollbar_handle": "#CCD0DA",
        "scrollbar_hover": "#BCC0CC",
        "ok_color": "#40A02B",
        "err_color": "#D20F39",
        "cs_border": "#FE640B",
        "cs_bg": "#FFF3E0",
        "link_color": "#1E66F5",
        "accent": "#1E66F5",
    },
}


def _global_qss(t: dict) -> str:
    """窗口级全局 QSS：标准控件 + 容器统一配色。
    放在 QApplication 级，弹窗/菜单/右键菜单/滚动条全部跟随主题。
    复选框对勾由 _ThemeCheckBox.paintEvent 手动绘制（Qt 6 image:url 在
    sub-control 上不渲染），下拉箭头用系统 native arrow。"""
    return (
        f"#titleBar{{background-color:{t['titlebar_bg']};"
        f"border-bottom:1px solid {t['titlebar_border']};}}"
        f"#titleBar QLabel{{color:{t['titlebar_fg']};background:transparent;}}"
        f"#titleBar QToolButton{{background:transparent;border:none;"
        f"color:{t['titlebar_fg']};padding:0;font-size:14px;"
        f"font-family:Consolas;}}"
        f"#titleBar QToolButton:hover{{background:{t['titlebar_hover']};}}"
        f"#titleBar QToolButton#btn_close:hover{{background:{t['close_hover']};"
        f"color:white;}}"
        f"#titleBar QToolButton#btn_theme{{font-family:\"Segoe UI\","
        f"\"Segoe UI Emoji\",\"Apple Color Emoji\",\"Noto Color Emoji\",sans-serif;"
        f"font-size:14px;}}"
        f"QWidget{{background-color:{t['win_bg']};color:{t['text_primary']};}}"
        f"QLabel{{background:transparent;color:{t['text_primary']};}}"
        f"QGroupBox{{background-color:{t['panel_bg']};border:1px solid {t['panel_border']};"
        f"border-radius:8px;}}"
        f"QPushButton{{background-color:{t['btn_bg']};color:{t['btn_fg']};"
        f"border:1px solid {t['input_border']};border-radius:5px;padding:3px 6px;}}"
        f"QPushButton:hover{{background-color:{t['btn_hover']};}}"
        f"QPushButton:pressed{{background-color:{t['panel_border']};}}"
        f"QPushButton:disabled{{color:{t['text_secondary']};}}"
        f"QLineEdit{{background-color:{t['input_bg']};color:{t['text_primary']};"
        f"border:1px solid {t['input_border']};border-radius:5px;padding:2px 6px;}}"
        f"QLineEdit:focus{{border-color:{t['accent']};}}"
        f"QComboBox{{background-color:{t['input_bg']};color:{t['text_primary']};"
        f"border:1px solid {t['input_border']};border-radius:5px;padding:2px 8px;}}"
        f"QComboBox:hover{{border-color:{t['accent']};}}"
        # 下拉箭头由 _StyledComboBox 内置 QLabel 显示（避免 Qt 6 image 兼容问题）；
        # 隐藏 Qt 自带的 native arrow 避免重叠
        f"QComboBox::drop-down{{border:none;subcontrol-origin:padding;"
        f"subcontrol-position:top right;width:20px;}}"
        f"QComboBox::down-arrow{{image:none;width:0;height:0;}}"
        f"QComboBox QAbstractItemView{{background-color:{t['combo_item_bg']};"
        f"color:{t['text_primary']};selection-background-color:{t['combo_item_sel']};"
        f"border:1px solid {t['input_border']};}}"
        f"QTextEdit{{background-color:{t['edit_bg']};color:{t['edit_fg']};"
        f"selection-background-color:{t['edit_sel']};border:none;}}"
        f"QTabWidget::pane{{background-color:{t['tab_pane']};border:1px solid {t['panel_border']};"
        f"border-radius:6px;top:-1px;}}"
        f"QTabBar::tab{{background:transparent;padding:5px 12px;color:{t['tab_sel_fg']};}}"
        f"QTabBar::tab:selected{{background:{t['tab_sel_bg']};border:1px solid {t['panel_border']};"
        f"border-bottom:none;border-top-left-radius:6px;border-top-right-radius:6px;}}"
        f"QTabBar::tab:!selected{{background:transparent;color:{t['tab_uns_fg']};}}"
        f"QTabBar::tab:hover:!selected{{background:{t['tab_hover']};}}"
        f"QListWidget{{background-color:{t['list_bg']};color:{t['text_primary']};"
        f"border:1px solid {t['input_border']};border-radius:5px;}}"
        f"QListWidget::item{{padding:2px 6px;}}"
        f"QListWidget::item:selected{{background-color:{t['list_sel']};"
        f"color:{t['text_primary']};}}"
        f"QScrollArea{{border:none;background:transparent;}}"
        f"QScrollArea > QWidget > QWidget{{background:transparent;}}"
        f"QMenu{{background-color:{t['menu_bg']};color:{t['menu_fg']};"
        f"border:1px solid {t['input_border']};}}"
        f"QMenu::item{{padding:4px 18px;background:transparent;}}"
        f"QMenu::item:selected{{background-color:{t['menu_sel']};}}"
        f"QScrollBar:vertical{{background:transparent;width:10px;margin:0;}}"
        f"QScrollBar::handle:vertical{{background:{t['scrollbar_handle']};"
        f"border-radius:5px;min-height:24px;}}"
        f"QScrollBar::handle:vertical:hover{{background:{t['scrollbar_hover']};}}"
        f"QScrollBar:horizontal{{background:transparent;height:10px;margin:0;}}"
        f"QScrollBar::handle:horizontal{{background:{t['scrollbar_handle']};"
        f"border-radius:5px;min-width:24px;}}"
        f"QScrollBar::add-line, QScrollBar::sub-line{{height:0;width:0;}}"
        f"QScrollBar::add-page, QScrollBar::sub-page{{background:transparent;}}"
        f"QToolTip{{background-color:{t['menu_bg']};color:{t['menu_fg']};"
        f"border:1px solid {t['input_border']};}}"
        f"#statusBar{{background-color:{t['status_bg']};color:{t['status_fg']};"
        f"border-top:1px solid {t['status_border']};}}"
        f"#statusBar QLabel{{color:{t['status_fg']};background:transparent;}}"
        f"QFrame#cs_group{{border:none;background:transparent;}}"
        f"QFrame#cs_group[cs_highlight=\"true\"]{{border:1px solid {t['cs_border']};"
        f"border-radius:3px;background:{t['cs_bg']};}}"
    )


def now_ts() -> str:
    return datetime.now().strftime("[%H:%M:%S.") + f"{datetime.now().microsecond // 1000:03d}]"


class SerialReader(QThread):
    """后台读串口线程：读到的字节通过信号发给主线程（Qt 跨线程信号自动排队，安全）。"""
    received = Signal(bytes)
    error = Signal(str)

    def __init__(self, ser):
        super().__init__()
        self.ser = ser
        self.running = True

    def run(self):
        while self.running and self.ser:
            try:
                data = self.ser.read(4096)
                if data:
                    self.received.emit(data)
            except Exception as e:
                self.error.emit(str(e))
                break

    def stop(self):
        self.running = False


class UpdateChecker(QThread):
    """后台检查 GitHub Releases 最新版本（不阻塞 UI）。失败时发 None。"""
    result = Signal(object)

    def __init__(self, timeout: float = 8.0):
        super().__init__()
        self.timeout = timeout

    def run(self):
        try:
            req = urllib.request.Request(
                UPDATE_CHECK_URL,
                headers={
                    "User-Agent": f"SJJ-COM-Tool/{APP_VERSION}",
                    "Accept": "application/vnd.github+json",
                })
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                data = json.loads(resp.read().decode("utf-8"))
            tag = data.get("tag_name", "")
            if not tag:
                self.result.emit(None)
                return
            info = {
                "latest": tag.lstrip("vV"),
                "tag": tag,
                "html_url": data.get("html_url", "")
                            or f"https://github.com/{GITHUB_REPO}/releases",
                "name": data.get("name") or "",
                "body": data.get("body") or "",
            }
            self.result.emit(info)
        except Exception:
            self.result.emit(None)


# ================= 标题栏主题切换按钮 SVG 图标 =================
# 替代 emoji 字符（避免系统字体回退 + emoji 太大/彩色问题）；
# 颜色由 SerialTool.apply_theme 动态注入（CURRENT 占位符）。
_MOON_SVG = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">'
    '<path d="M11 3 a7 7 0 1 0 4 10 5.5 5.5 0 0 1 -4 -10 z" fill="CURRENT"/>'
    '</svg>'
)
# 简化为亮色实心圆（去掉 8 光芒线）
_SUN_SVG = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">'
    '<circle cx="8" cy="8" r="6" fill="CURRENT"/>'
    '</svg>'
)


def _make_theme_icon(svg_template: str, color: str, size: int = 14) -> QIcon:
    """把 SVG 模板（fill="CURRENT" 占位符）渲染成 QIcon，size x size 透明背景。"""
    svg = svg_template.replace("CURRENT", color)
    renderer = QSvgRenderer(QByteArray(svg.encode("utf-8")))
    pixmap = QPixmap(size, size)
    pixmap.fill(Qt.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.Antialiasing, True)
    renderer.render(painter)
    painter.end()
    return QIcon(pixmap)


def _png_data_url(pixmap: QPixmap) -> str:
    """把 QPixmap 序列化为 base64 PNG data URL（用作 QSS image:url）。"""
    ba = QByteArray()
    from PySide6.QtCore import QBuffer, QIODevice
    buf = QBuffer(ba)
    buf.open(QIODevice.WriteOnly)
    pixmap.save(buf, "PNG")
    return "data:image/png;base64," + base64.b64encode(ba.data()).decode("ascii")


def _cache_pixmap(pixmap: QPixmap, name: str) -> str:
    """把 PNG pixmap 写入程序目录 cache/ 目录并返回 file:// URL。
    写文件比 data: URL 更可靠（Qt 6 对 sub-control 的 data: URL 渲染有兼容问题）。"""
    cache_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cache")
    os.makedirs(cache_dir, exist_ok=True)
    path = os.path.join(cache_dir, name)
    pixmap.save(path, "PNG")
    # Windows 路径转 file:// URL
    url = "file:///" + path.replace("\\", "/")
    return url


def _make_check_pixmap(bg_color: str) -> QPixmap:
    """复选框对勾 PNG：bg_color 实心圆角矩形 + 白色对勾（18x18，3px 圆角）。"""
    pixmap = QPixmap(18, 18)
    pixmap.fill(Qt.transparent)
    p = QPainter(pixmap)
    p.setRenderHint(QPainter.Antialiasing, True)
    # 背景圆角矩形
    p.setBrush(QColor(bg_color))
    p.setPen(Qt.NoPen)
    p.drawRoundedRect(0, 0, 18, 18, 3, 3)
    # 白色对勾
    pen = QPen(Qt.white)
    pen.setWidth(2)
    pen.setCapStyle(Qt.RoundCap)
    pen.setJoinStyle(Qt.RoundJoin)
    p.setPen(pen)
    p.drawLine(3, 9, 7, 13)
    p.drawLine(7, 13, 14, 4)
    p.end()
    return pixmap


def _make_arrow_pixmap(fill_color: str) -> QPixmap:
    """下拉箭头 PNG：fill_color 倒三角（10x10）。"""
    pixmap = QPixmap(10, 10)
    pixmap.fill(Qt.transparent)
    p = QPainter(pixmap)
    p.setRenderHint(QPainter.Antialiasing, True)
    p.setBrush(QColor(fill_color))
    p.setPen(Qt.NoPen)
    # 倒三角：顶部边 y=3, 底部点 y=7
    p.drawPolygon(QPolygon([QPoint(1, 3), QPoint(9, 3), QPoint(5, 7)]))
    p.end()
    return pixmap


class _StyledComboBox(QComboBox):
    """主题化下拉框：在右侧叠加一个 QLabel 显示箭头（Qt 限制：widget
    paintEvent 期间不能创建第二个 QPainter，所以用子 QLabel 方案绕开）。
    label 设置 WA_TransparentForMouseEvents 让点击穿透到 combo 不影响下拉。"""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._arrow = QLabel("▾", self)
        self._arrow.setAttribute(Qt.WA_TransparentForMouseEvents, True)
        self._arrow.setAlignment(Qt.AlignCenter)
        self._arrow.setStyleSheet(
            "background:transparent;color:#4C4F69;font-size:14px;")
        self._arrow.resize(18, self.height())
        self._arrow.move(self.width() - 18, 0)
        self._arrow.show()

    def set_arrow_color(self, color):
        if isinstance(color, QColor):
            c = color.name()
        else:
            c = str(color)
        self._arrow.setStyleSheet(
            f"background:transparent;color:{c};font-size:14px;")
        self._arrow.update()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        # label 始终在右侧 18px 区域内
        self._arrow.resize(18, self.height())
        self._arrow.move(self.width() - 18, 0)


class _ThemeCheckBox(QCheckBox):
    """主题化复选框：checked 时显示 accent 蓝底 + 白色对勾（手动绘制，
    绕开 Qt 6 image:url() 在 sub-control 上不渲染的兼容问题）。"""

    def __init__(self, text: str = ""):
        super().__init__(text)
        self._tc = {}

    def set_theme_colors(self, t: dict):
        self._tc = t
        self._refresh_style()

    def _refresh_style(self):
        """按当前主题色单独设置 QSS（避免与全局 QSS 中 ::indicator 规则冲突）。"""
        if not self._tc:
            return
        t = self._tc
        # checked 状态背景 = accent
        # unchecked = input_bg / input_border
        self.setStyleSheet(
            f"QCheckBox{{color:{t['text_primary']};background:transparent;"
            f"spacing:4px;}}"
            f"QCheckBox::indicator{{width:18px;height:18px;border:1px solid "
            f"{t['input_border']};border-radius:3px;background:{t['input_bg']};}}"
            f"QCheckBox::indicator:hover{{border-color:{t['accent']};}}"
            f"QCheckBox::indicator:checked{{background-color:{t['accent']};"
            f"border-color:{t['accent']};}}"
        )

    def paintEvent(self, event):
        # 用 QSS ::indicator 渲染 indicator 容器（背景+边框）
        super().paintEvent(event)
        # 手动绘制白色对勾（QSS image:url 在 sub-control 不渲染）
        if not self.isChecked() or not self._tc:
            return
        painter = QPainter(self)
        try:
            painter.setRenderHint(QPainter.Antialiasing, True)
            opt = QStyleOptionButton()
            self.initStyleOption(opt)
            ind_rect = self.style().subElementRect(
                QStyle.SE_CheckBoxIndicator, opt, self)
            pen = QPen(Qt.white)
            pen.setWidth(2)
            pen.setCapStyle(Qt.RoundCap)
            pen.setJoinStyle(Qt.RoundJoin)
            painter.setPen(pen)
            x, y, w, h = ind_rect.x(), ind_rect.y(), ind_rect.width(), ind_rect.height()
            painter.drawLine(int(x + w * 0.2), int(y + h * 0.55),
                              int(x + w * 0.45), int(y + h * 0.75))
            painter.drawLine(int(x + w * 0.45), int(y + h * 0.75),
                              int(x + w * 0.8), int(y + h * 0.25))
        finally:
            painter.end()


class _TitleBar(QWidget):
    """自绘标题栏（无边框窗口用）：颜色完全可控，不受 Windows 系统失焦变白影响。
    含图标、标题文字、最小化/最大化/关闭按钮，支持拖动、双击最大化、右键菜单。"""
    def __init__(self, parent: QWidget):
        super().__init__(parent)
        self._win = parent   # 顶层窗口引用（布局 reparent 后 self.parent() 会是 window_card，必须保存）
        # QWidget 子类必须设置 WA_StyledBackground，QSS 背景色才会绘制（否则显示默认白底）
        self.setAttribute(Qt.WA_StyledBackground, True)
        self.setFixedHeight(30)
        self.setObjectName("titleBar")
        h = QHBoxLayout(self)
        h.setContentsMargins(8, 0, 2, 0)
        h.setSpacing(2)
        # 图标 + 标题
        self.lbl_icon = QLabel()
        ic = parent.windowIcon()
        if not ic.isNull():
            self.lbl_icon.setPixmap(ic.pixmap(18, 18))
        self.lbl_icon.setFixedSize(18, 18)
        h.addWidget(self.lbl_icon)
        self.lbl_title = QLabel(parent.windowTitle())
        # 版本号跟随窗口标题（v{APP_VERSION}），标题栏左上角显示。
        # 纯文本（颜色由全局 QSS #titleBar QLabel 控制=titlebar_fg，与其他按钮文字一致，
        # 避免 HTML 链接用默认链接色在深色模式偏深）。点击检查更新由
        # _TitleBar.mouseReleaseEvent 判断"在版本号区域内点击且未拖动"触发。
        self.lbl_title.setText(f"{APP_TITLE}  v{APP_VERSION}")
        self.lbl_title.setStyleSheet("padding-left:4px;")
        self.lbl_title.setCursor(Qt.PointingHandCursor)
        self.lbl_title.setToolTip("点击检查更新")
        h.addWidget(self.lbl_title)
        h.addStretch(1)
        # 按钮：主题切换（最小化左侧）→ 最小化 → 最大化 → 关闭
        # btn_theme 用 SVG icon（月亮/太阳，颜色随主题变化），不用 emoji
        # 避免系统字体回退不可靠 + emoji 太大彩色问题
        self.btn_theme = QToolButton(self)
        self.btn_theme.setObjectName("btn_theme")
        self.btn_theme.setIconSize(QSize(14, 14))
        self.btn_theme.setToolTip("切换到深色模式")
        self.btn_theme.clicked.connect(parent.toggle_theme)
        self.btn_min = QToolButton(self); self.btn_min.setText("—"); self.btn_min.setToolTip("最小化")
        self.btn_max = QToolButton(self); self.btn_max.setText("□"); self.btn_max.setToolTip("最大化")
        self.btn_close = QToolButton(self); self.btn_close.setText("✕")
        self.btn_close.setObjectName("btn_close"); self.btn_close.setToolTip("关闭")
        self.btn_min.clicked.connect(parent.showMinimized)
        self.btn_max.clicked.connect(self._toggle_max)
        self.btn_close.clicked.connect(parent.close)
        for b in (self.btn_theme, self.btn_min, self.btn_max, self.btn_close):
            b.setFixedSize(30, 30)
            h.addWidget(b)
        # 拖动 / 双击 / 右键
        self._pressed = False
        self._drag_pos = None

    def apply_theme(self, t: dict, theme_name: str):
        """标题栏配色由全局 QSS 的 #titleBar 选择器统一控制（不自设样式表，
        避免阻断子控件/QMenu 继承全局主题）。此处仅更新主题切换按钮 SVG icon
        （颜色用主题文字色，浅色模式深月亮/深色模式亮太阳）。"""
        icon_color = t["titlebar_fg"]
        if theme_name == "dark":      # 当前深色 → 显示亮色太阳（点击切回浅色）
            self.btn_theme.setIcon(_make_theme_icon(_SUN_SVG, icon_color))
            self.btn_theme.setToolTip("切换到浅色模式")
        else:                         # 当前浅色 → 显示深色月亮（点击切到深色）
            self.btn_theme.setIcon(_make_theme_icon(_MOON_SVG, icon_color))
            self.btn_theme.setToolTip("切换到深色模式")

    def _toggle_max(self):
        w = self._win
        if w.isMaximized():
            w.showNormal()
            self.btn_max.setText("□"); self.btn_max.setToolTip("最大化")
        else:
            w.showMaximized()
            self.btn_max.setText("❐"); self.btn_max.setToolTip("还原")

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            child = self.childAt(e.position().toPoint())
            # 版本号（lbl_title）/图标区域不启动拖动：
            # 点击版本号用于"手动检查更新"，避免触发拖动后弹窗拦截 release 无法释放
            if child is self.lbl_title or child is self.lbl_icon:
                super().mousePressEvent(e)
                return
            self._drag_pos = e.globalPos() - self._win.frameGeometry().topLeft()
            self._pressed = True
            e.accept()
        else:
            super().mousePressEvent(e)

    def mouseMoveEvent(self, e):
        if self._pressed and self._drag_pos is not None and not self._win.isMaximized():
            win = self._win
            new_pos = e.globalPos() - self._drag_pos
            # 限制拖动范围：标题栏必须在屏幕内（否则无法抓住拖回）
            screen = win.screen().availableGeometry()
            frame = win.frameGeometry()
            if new_pos.y() < screen.top():
                new_pos.setY(screen.top())
            min_x = screen.left() - frame.width() + 120   # 至少保留 120px 可见
            max_x = screen.right() - 120
            if new_pos.x() < min_x:
                new_pos.setX(min_x)
            if new_pos.x() > max_x:
                new_pos.setX(max_x)
            win.move(new_pos)
            e.accept()
        else:
            super().mouseMoveEvent(e)

    def mouseReleaseEvent(self, e):
        if e.button() == Qt.LeftButton:
            # 版本号区域点击且本次未拖动 → 手动检查更新
            if not self._pressed and self.lbl_title.geometry().contains(
                    e.position().toPoint()):
                self._win._check_update_manual()
                e.accept()
                return
            self._pressed = False
            self._drag_pos = None
            super().mouseReleaseEvent(e)
        else:
            super().mouseReleaseEvent(e)

    def mouseDoubleClickEvent(self, e):
        if e.button() == Qt.LeftButton:
            self._toggle_max()
            e.accept()
        else:
            super().mouseDoubleClickEvent(e)

    def contextMenuEvent(self, e):
        m = QMenu(self)
        m.addAction("最小化", self._win.showMinimized)
        m.addAction("还原" if self._win.isMaximized() else "最大化",
                    self._toggle_max)
        m.addSeparator()
        m.addAction("关闭", self._win.close)
        m.exec(e.globalPos())


class PortComboBox(_StyledComboBox):
    """端口号下拉：宽度不足时直接末尾硬截断；弹出列表项末尾省略号；弹出按内容宽度展开。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        # 弹出列表项：省略号在末尾（默认中间）
        class _ElideDelegate(QStyledItemDelegate):
            def initStyleOption(self, option, index):
                super().initStyleOption(option, index)
                option.textElideMode = Qt.ElideRight
        self.setItemDelegate(_ElideDelegate(self))
        # 弹出列表按内容宽度展开（超长项不强制压缩到收起宽度）
        self.setSizeAdjustPolicy(QComboBox.AdjustToContents)

    def initStyleOption(self, option):
        super().initStyleOption(option)
        option.textElideMode = Qt.ElideNone


class SerialTool(QWidget):
    def __init__(self):
        super().__init__()
        self.ser = None
        self.reader = None
        self.rx_bytes = 0
        self.tx_bytes = 0
        self.records = []            # [{"kind","display"}]
        self.pending_rx = []         # 待刷新的接收显示缓冲
        self.send_history = []       # 发送历史（去重、置顶，上限 50）
        self.ms_entries = []         # 多字符串条目
        self._port_map = {}
        self._ms_looping = False
        self._ms_loop_index = 0
        self._file_sending = False
        self._file_data = b""
        self._file_offset = 0
        self._settings_win = None
        self._suppress_apply = False    # 程序化修改串口参数时抑制"自动重开"
        self._program_scroll = False    # 程序性滚动（搜索刷新/批量插入/清空）期间抑制"自动勾选暂停刷新"
        self._last_status_ts = 0.0
        self._last_sr_text = None
        self._last_hs_text = None
        self.last_send_text = ""
        self.last_send_hex = ""
        self._send_hex_prev = False
        self._cs_preview_timer = None
        self._search_timer = None
        self._ms_row_widgets = []
        self._theme = "light"                     # 当前主题（dark / light）
        self._win_bg = THEMES["light"]["win_bg"]  # paintEvent 画背景用
        self._rx_epoch = 0                        # 串口代际号：关闭后丢弃旧线程排队的信号
        self._update_checker = None               # 后台更新检查线程
        self._ignore_update_version = None        # 用户"忽略本次更新"的版本号（持久化）

        self._build_ui()
        self.apply_theme(self._theme)   # 初始应用主题（QApplication 级 QSS）
        self._refresh_ports()
        self._autoload_config()
        # 启动延迟 2 秒自动检查更新（后台线程，不阻塞 UI；失败静默）
        QTimer.singleShot(2000, self._check_update_auto)

        # 无边框窗口边缘缩放（事件过滤器全局拦截）
        self._resize_margin = 8
        self._resize_edge = None          # 当前拖动的缩放边缘（None=未缩放）
        self._resize_start_pos = None     # 按下时全局坐标
        self._resize_start_geo = None     # 按下时窗口几何

        self.flush_timer = QTimer(self)
        self.flush_timer.timeout.connect(self._drain_rx)
        self.flush_timer.start(50)
        self._install_resize_filter()

    _EDGE_L, _EDGE_R, _EDGE_T, _EDGE_B = 1, 2, 4, 8   # 边缘位掩码（角=组合）

    def _detect_resize_edge(self, pos):
        if self.isMaximized():
            return 0
        m = self._resize_margin
        w, h = self.width(), self.height()
        edges = 0
        if pos.x() <= m:
            edges |= self._EDGE_L
        if pos.x() >= w - m:
            edges |= self._EDGE_R
        if pos.y() <= m:
            edges |= self._EDGE_T
        if pos.y() >= h - m:
            edges |= self._EDGE_B
        return edges

    def _resize_cursor(self, edges):
        if edges in (self._EDGE_L, self._EDGE_R):
            return Qt.SizeHorCursor
        if edges in (self._EDGE_T, self._EDGE_B):
            return Qt.SizeVerCursor
        if edges in (self._EDGE_L | self._EDGE_T, self._EDGE_R | self._EDGE_B):
            return Qt.SizeFDiagCursor
        if edges in (self._EDGE_R | self._EDGE_T, self._EDGE_L | self._EDGE_B):
            return Qt.SizeBDiagCursor
        return None

    def eventFilter(self, obj, event):
        """统一拦截窗口及其子控件的边缘鼠标事件：
        - 鼠标在窗口边缘 8px 内（含子控件覆盖区域）→ 进入/执行缩放
        - 顶部边缘（title_bar 区域）与左边缘（content 区域）因此也可缩放
        - 事件被吞掉不传给子控件，避免干扰正常交互"""
        # 动态补装：配置加载后创建的控件
        if isinstance(obj, QWidget) and obj is not self and obj not in self._filter_installed:
            self._filter_installed.add(obj)
            obj.setMouseTracking(True)
            obj.installEventFilter(self)
        t = event.type()
        if t in (QEvent.MouseMove, QEvent.MouseButtonPress, QEvent.MouseButtonRelease):
            gpos = event.globalPos()
            pos = self.mapFromGlobal(gpos)
            if t == QEvent.MouseButtonRelease:
                if self._resize_edge is not None:
                    self._resize_edge = None
                    self._resize_start_pos = None
                    return True
                return False
            if t == QEvent.MouseButtonPress:
                if event.button() == Qt.LeftButton:
                    edges = self._detect_resize_edge(pos)
                    if edges:
                        # WA_TranslucentBackground 分层窗口不支持 startSystemResize，手动缩放
                        self._resize_edge = edges
                        self._resize_start_pos = gpos
                        self._resize_start_geo = self.geometry()
                        return True
                return False
            if t == QEvent.MouseMove:
                if self._resize_edge is not None:
                    self._apply_resize(gpos)
                    return True
                edges = self._detect_resize_edge(pos)
                cur = self._resize_cursor(edges) if edges else None
                if cur:
                    self.setCursor(cur)
                    return True   # 边缘区域不传给子控件
                self.unsetCursor()
        return super().eventFilter(obj, event)

    def _install_resize_filter(self):
        """给窗口自身及所有子控件安装事件过滤器（边缘缩放全局生效）。
        必须开启子控件 mouseTracking——否则鼠标悬停（未按下）时 move 事件
        不会发给子控件，eventFilter 不触发，边缘光标无法更新。"""
        self._filter_installed = {self}
        self.setMouseTracking(True)
        self.installEventFilter(self)
        for child in self.findChildren(QWidget):
            self._filter_installed.add(child)
            child.setMouseTracking(True)
            child.installEventFilter(self)

    def _apply_resize(self, gpos):
        """按拖动偏移手动调整窗口几何（分层窗口无法用系统缩放）。"""
        edge = self._resize_edge
        x, y, w, h = self._resize_start_geo.getRect()
        dx = gpos.x() - self._resize_start_pos.x()
        dy = gpos.y() - self._resize_start_pos.y()
        min_w, min_h = self.minimumWidth(), self.minimumHeight()
        if edge & self._EDGE_L:
            new_w = w - dx
            if new_w >= min_w:
                x += dx; w = new_w
            else:
                x = x + w - min_w; w = min_w
        if edge & self._EDGE_R:
            w = max(min_w, w + dx)
        if edge & self._EDGE_T:
            new_h = h - dy
            if new_h >= min_h:
                y += dy; h = new_h
            else:
                y = y + h - min_h; h = min_h
        if edge & self._EDGE_B:
            h = max(min_h, h + dy)
        self.setGeometry(x, y, w, h)

    def changeEvent(self, e):
        """最大化时阴影超出屏幕自动不可见，圆角被屏幕边界自然裁剪；
        保持圆角+边距样式不变，避免切换时出现嵌套框错位。"""
        super().changeEvent(e)

    def showEvent(self, e):
        super().showEvent(e)
        # 窗口首次显示后启用 DWM 系统阴影 + 圆角（Windows 原生效果）
        self._enable_dwm_effects()

    def _enable_dwm_effects(self):
        """让 Windows DWM 系统绘制窗口阴影（Win10/11）与圆角（Win11 22H2+）。
        系统阴影在窗口外，窗口大小不含阴影——与普通软件一致。"""
        if sys.platform != "win32":
            return
        try:
            import ctypes
            hwnd = int(self.winId())

            class MARGINS(ctypes.Structure):
                _fields_ = [("cxLeft", ctypes.c_int), ("cxRight", ctypes.c_int),
                            ("cyTop", ctypes.c_int), ("cyBottom", ctypes.c_int)]

            # 1) 系统阴影：扩展 DWM frame 到客户区（经典 Win10 做法）
            margins = MARGINS(-1, -1, -1, -1)
            ctypes.windll.dwmapi.DwmExtendFrameIntoClientArea(hwnd, ctypes.byref(margins))
            # 2) Win11 22H2+：系统圆角（DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2）
            try:
                corner = ctypes.c_int(2)
                ctypes.windll.dwmapi.DwmSetWindowAttribute(
                    hwnd, 33, ctypes.byref(corner), ctypes.sizeof(corner))
            except Exception:
                pass
        except Exception:
            pass

    def paintEvent(self, e):
        """画窗口不透明背景（主题色 win_bg）。WA_TranslucentBackground 下 QSS 背景在某些
        子区域失效，必须用 QPainter 直接画。系统圆角负责裁剪圆角外的部分。"""
        QPainter(self).fillRect(self.rect(), QColor(self._win_bg))

    # ================= UI 构建 =================
    def _build_ui(self):
        # 无边框 + 透明背景：阴影和圆角由 Windows DWM 系统绘制（窗口外，不占窗口内部）
        self.setWindowFlags(Qt.FramelessWindowHint | Qt.Window)
        self.setAttribute(Qt.WA_TranslucentBackground, True)
        self.setWindowTitle(f"{APP_TITLE}  v{APP_VERSION}")
        self.resize(900, 560)
        self.setMinimumSize(900, 560)

        outer = QVBoxLayout(self)   # 布局直接挂顶层窗口
        outer.setContentsMargins(0, 0, 0, 0)
        self._outer_lay = outer

        # 自绘标题栏（窗口顶部）
        self.title_bar = _TitleBar(self)
        outer.addWidget(self.title_bar)

        # 内容容器（变量名保持 main，后续代码不变）
        content = QWidget()
        content.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        main = QVBoxLayout(content)
        main.setContentsMargins(6, 4, 6, 4)
        main.setSpacing(4)
        outer.addWidget(content, 1)

        # ===== 上半部分：横向布局(接收区+命令面板) + 接收区按钮行 =====
        # 用 QHBoxLayout 而非 QSplitter：命令面板固定 420 宽，不存在分隔条，
        # 天然不可拖拽、不可折叠——彻底杜绝"拖拽把面板缩没"的问题。
        # 面板显示时只压缩接收文字显示区（txt_recv），按钮行在下方始终全宽。
        upper = QWidget()
        upper_lay = QVBoxLayout(upper)
        upper_lay.setContentsMargins(0, 0, 0, 0)
        upper_lay.setSpacing(4)

        upper_row = QHBoxLayout()
        upper_row.setSpacing(4)
        upper_lay.addLayout(upper_row, 1)

        # 左侧：接收区（只放文字显示；按钮行放在本容器下面）
        recv_box = QGroupBox("")
        recv_lay = QVBoxLayout(recv_box)
        recv_lay.setContentsMargins(8, 6, 8, 6)
        recv_lay.setSpacing(2)

        self.txt_recv = QTextEdit()
        self.txt_recv.setReadOnly(True)
        self.txt_recv.setFont(MONO_FONT)
        self.txt_recv.setLineWrapMode(QTextEdit.WidgetWidth)
        recv_lay.addWidget(self.txt_recv, 1)

        upper_row.addWidget(recv_box, 1)

        # 右侧：历史 / 快捷命令 窗口（默认隐藏，点按钮打开）
        self.ms_tabs = QTabWidget()
        # 命令面板固定为最小宽度 420，禁止左右拖拽改变大小
        self.ms_tabs.setFixedWidth(420)
        self._build_history_tab()
        self._build_ms_tab()
        self.ms_tabs.hide()
        upper_row.addWidget(self.ms_tabs)

        # 接收区按钮行：清除窗口 / HEX显示 / 加时间戳 / 暂停刷新 / 编码 / 搜索 / 筛选 / 保存数据
        # 放在 upper_row（接收区+命令面板）下面，宽度跟随 upper 容器（即上半部分的全宽），
        # ms_tabs 打开/收起时不会被挤压。
        rt = QHBoxLayout()
        rt.setSpacing(4)
        btn = QPushButton("清除窗口"); btn.clicked.connect(self._clear_recv); rt.addWidget(btn)
        self.chk_show_hex = _ThemeCheckBox("HEX显示"); rt.addWidget(self.chk_show_hex)
        self.chk_show_ts = _ThemeCheckBox("时间戳")
        self.chk_show_ts.setToolTip("显示时间戳和分包（多行数据每行加前缀）")
        self.chk_show_ts.setChecked(True); rt.addWidget(self.chk_show_ts)
        # 切换时间戳时立即重建缓存消息视图（显示/移除已收消息的时间戳）
        self.chk_show_ts.stateChanged.connect(self._refresh_view)
        self.chk_pause = _ThemeCheckBox("暂停刷新"); rt.addWidget(self.chk_pause)
        rt.addWidget(QLabel("编码:"))
        self.cmb_encoding = _StyledComboBox()
        self.cmb_encoding.addItems(ENCODINGS)
        self.cmb_encoding.setEditable(False)
        self.cmb_encoding.setFixedWidth(90)
        rt.addWidget(self.cmb_encoding)
        rt.addWidget(QLabel("搜索:"))
        self.entry_search = QLineEdit()
        self.entry_search.setMaximumWidth(180)
        self.entry_search.textChanged.connect(self._on_search_change)
        rt.addWidget(self.entry_search)
        self.chk_filter = _ThemeCheckBox("筛选")
        self.chk_filter.stateChanged.connect(self._refresh_view)
        rt.addWidget(self.chk_filter)
        btn = QPushButton("保存数据"); btn.clicked.connect(self._save_recv); rt.addWidget(btn)
        rt.addStretch(1)
        upper_lay.addLayout(rt)

        main.addWidget(upper, 1)

        # 滚动跟随：滚轮上翻/拖动滚动条上移 → 自动勾选暂停刷新；滚回底部自动取消
        self.txt_recv.verticalScrollBar().valueChanged.connect(self._on_recv_scroll)

        # ===== 下半部分：串口设置块（左） + 发送区（右）=====
        lower = QHBoxLayout()
        lower.setSpacing(6)
        main.addLayout(lower)

        # 左侧：串口设置块
        sb_box = QGroupBox("")
        sb_box.setMaximumWidth(290)
        sb = QVBoxLayout(sb_box)
        sb.setContentsMargins(6, 6, 6, 6)
        sb.setSpacing(2)

        port_row = QHBoxLayout()
        port_row.setSpacing(2)
        port_row.addWidget(QLabel("端口号:"))
        self.cmb_port = PortComboBox()
        self.cmb_port.setEditable(False)
        self.cmb_port.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        port_row.addWidget(self.cmb_port, 1)
        btn = QPushButton("刷新"); btn.setFixedWidth(40); btn.clicked.connect(self._refresh_ports)
        port_row.addWidget(btn)
        sb.addLayout(port_row)

        baud_row = QHBoxLayout()
        baud_row.setSpacing(2)
        lbl_baud = QLabel("波特率:")
        lbl_baud.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)   # 不伸展，避免与下拉之间出现间隔
        baud_row.addWidget(lbl_baud)
        self.cmb_baud = _StyledComboBox()
        self.cmb_baud.addItems(DEFAULT_BAUDRATES)
        self.cmb_baud.setCurrentText("115200")
        self.cmb_baud.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.cmb_baud.setMinimumWidth(60)
        baud_row.addWidget(self.cmb_baud, 1)   # 伸展吸收剩余空间（保证长波特率完整显示）
        # 串口已打开时，切换端口/波特率 → 立即用新参数重新打开串口
        self.cmb_port.currentIndexChanged.connect(self._on_port_changed)
        self.cmb_baud.currentIndexChanged.connect(self._on_baud_changed)
        btn = QPushButton("更多设置"); btn.setFixedWidth(70); btn.clicked.connect(self._show_settings_dialog)
        baud_row.addWidget(btn)
        self.btn_open = QPushButton("打开串口")
        self.btn_open.setFixedWidth(70)
        self.btn_open.clicked.connect(self._toggle_port)
        baud_row.addWidget(self.btn_open)
        sb.addLayout(baud_row)

        btn_row = QHBoxLayout()
        btn_row.setSpacing(2)
        btn = QPushButton("历史记录"); btn.clicked.connect(lambda: self._toggle_window(0))
        btn_row.addWidget(btn, 1)
        btn = QPushButton("快捷命令"); btn.clicked.connect(lambda: self._toggle_window(1))
        btn_row.addWidget(btn, 1)
        sb.addLayout(btn_row)
        sb.addStretch(1)
        lower.addWidget(sb_box)

        # 右侧：发送区
        send_box = QGroupBox("")
        send_lay = QVBoxLayout(send_box)
        send_lay.setContentsMargins(8, 6, 8, 6)
        send_lay.setSpacing(3)

        # 文件发送 + 加校验
        fb = QHBoxLayout()
        fb.setSpacing(4)
        fb.setAlignment(Qt.AlignVCenter)
        # 选择文件按钮：点击后按钮文本变为文件路径（无需输入框）
        self.btn_select_file = QPushButton("选择文件")
        self.btn_select_file.setToolTip("点击选择要发送的文件")
        self.btn_select_file.clicked.connect(self._pick_file)
        fb.addWidget(self.btn_select_file, 1)
        self.btn_send_file = QPushButton("发送文件")
        self.btn_send_file.clicked.connect(self._send_file)
        fb.addWidget(self.btn_send_file)

        # 加校验范围：第[x]字节至[x] 一行，普通输入框（无箭头）
        # 整组包进 cs_group：加校验≠None 时高亮（橙色边框+浅橙底）
        self.cs_group = QFrame()
        self.cs_group.setObjectName("cs_group")
        self.cs_group.setFrameShape(QFrame.NoFrame)
        cs_lay = QHBoxLayout(self.cs_group)
        cs_lay.setContentsMargins(4, 1, 4, 1)
        cs_lay.setSpacing(3)
        cs_lay.addWidget(QLabel("第"))
        self.entry_cs_start = QLineEdit("1")
        self.entry_cs_start.setFixedWidth(40)
        self.entry_cs_start.setAlignment(Qt.AlignCenter)
        cs_lay.addWidget(self.entry_cs_start)
        cs_lay.addWidget(QLabel("字节至末尾"))
        self.entry_cs_end = QLineEdit("0")
        self.entry_cs_end.setFixedWidth(40)
        self.entry_cs_end.setAlignment(Qt.AlignCenter)
        cs_lay.addWidget(self.entry_cs_end)

        cs_lay.addWidget(QLabel("加校验"))
        self.cmb_checksum = _StyledComboBox()
        self.cmb_checksum.addItems(CHECKSUMS)
        self.cmb_checksum.setEditable(False)
        self.cmb_checksum.setFixedWidth(110)
        cs_lay.addWidget(self.cmb_checksum)
        self.lbl_cs_result = QLabel("")
        cs_lay.addWidget(self.lbl_cs_result)
        fb.addWidget(self.cs_group)
        self.cmb_checksum.currentIndexChanged.connect(self._update_cs_highlight)
        self._update_cs_highlight()
        fb.addStretch(1)
        send_lay.addLayout(fb)

        # 发送/清空发送/发送选项 一排
        sbar = QHBoxLayout()
        sbar.setSpacing(4)
        self.btn_send = QPushButton("发送")
        self.btn_send.clicked.connect(self._send)
        sbar.addWidget(self.btn_send)
        btn = QPushButton("清空发送"); btn.clicked.connect(self._clear_send); sbar.addWidget(btn)
        self.chk_send_hex = _ThemeCheckBox("HEX发送")
        self.chk_send_hex.stateChanged.connect(self._on_send_hex_toggle)
        sbar.addWidget(self.chk_send_hex)
        self.chk_add_crlf = _ThemeCheckBox("加回车换行"); sbar.addWidget(self.chk_add_crlf)
        self.chk_timer = _ThemeCheckBox("定时发送:")
        self.chk_timer.stateChanged.connect(self._on_timer_toggle)
        sbar.addWidget(self.chk_timer)
        self.entry_interval = QLineEdit("1000")
        self.entry_interval.setFixedWidth(56)
        sbar.addWidget(self.entry_interval)
        sbar.addWidget(QLabel("ms/次"))
        sbar.addStretch(1)
        send_lay.addLayout(sbar)

        self.txt_send = QTextEdit()
        self.txt_send.setFont(MONO_FONT)
        self.txt_send.setFixedHeight(90)
        self.txt_send.textChanged.connect(self._update_cs_preview)
        send_lay.addWidget(self.txt_send, 1)
        lower.addWidget(send_box, 1)

        # ===== 状态栏（自建 widget，放进卡片底部，宽度与卡片一致）=====
        self.status_bar = QWidget()
        self.status_bar.setObjectName("statusBar")
        self.status_bar.setAttribute(Qt.WA_StyledBackground, True)
        sb_lay = QHBoxLayout(self.status_bar)
        sb_lay.setContentsMargins(6, 1, 6, 1)
        sb_lay.setSpacing(8)
        self.lbl_status = QLabel("未连接")
        sb_lay.addWidget(self.lbl_status, 1)
        self.lbl_sr = QLabel("S:0  R:0")
        sb_lay.addWidget(self.lbl_sr)
        self.lbl_handshake = QLabel("CTS=0 DSR=0 RLSD=0")
        sb_lay.addWidget(self.lbl_handshake)
        self.lbl_app = QLabel(f'<a href="github" style="color:{THEMES["light"]["link_color"]};">SJJ‑COM Tool</a>')
        self.lbl_app.setOpenExternalLinks(False)
        self.lbl_app.linkActivated.connect(lambda _l: self._open_github())
        self.lbl_app.setCursor(Qt.PointingHandCursor)
        sb_lay.addWidget(self.lbl_app)
        outer.addWidget(self.status_bar)   # 窗口底部（与窗口同宽）

        # 定时发送
        self.timer_send = QTimer(self)
        self.timer_send.timeout.connect(self._timer_send_tick)

        # 多字符串循环发送
        self.ms_loop_timer = QTimer(self)
        self.ms_loop_timer.timeout.connect(self._ms_loop_tick)

    # ================= 主题 =================
    def apply_theme(self, theme_name: str):
        """应用主题：设置全局 QSS（QApplication 级）+ 标题栏 + 状态栏文字等。"""
        if theme_name not in THEMES:
            theme_name = "light"
        self._theme = theme_name
        t = THEMES[theme_name]
        self._win_bg = t["win_bg"]
        app = QApplication.instance()
        if app is not None:
            app.setStyleSheet(_global_qss(t))
        self.title_bar.apply_theme(t, theme_name)
        self.lbl_ms_hint.setStyleSheet(f"color:{t['text_secondary']};")
        self.lbl_cs_result.setStyleSheet(f"color:{t['err_color']};font-weight:bold;")
        self.lbl_app.setText(
            f'<a href="github" style="color:{t["link_color"]};">SJJ‑COM Tool</a>')
        # 主题化复选框：手动绘制，传入颜色
        for cb in self.findChildren(_ThemeCheckBox):
            cb.set_theme_colors(t)
        # 主题化下拉框：手动绘制箭头，颜色随主题变化
        for combo in self.findChildren(_StyledComboBox):
            combo.set_arrow_color(QColor(t["text_primary"]))
        self._update_cs_highlight()
        self._refresh_status()
        self.update()   # 立即触发 paintEvent 重画窗口背景

    def toggle_theme(self):
        """标题栏按钮：深色 ↔ 浅色 切换，并持久化到配置。"""
        new_theme = "dark" if self._theme == "light" else "light"
        self.apply_theme(new_theme)
        self._save_params(silent=True)

    # ================= 更新检查 =================
    def _check_update_auto(self):
        """启动时自动检查（后台线程）。遵守"忽略本次更新"标记。"""
        if self._update_checker is not None and self._update_checker.isRunning():
            return
        self._update_checker = UpdateChecker()
        self._update_checker.result.connect(self._on_update_check_result)
        self._update_checker.start()

    def _check_update_manual(self, *_):
        """点击标题栏版本号：手动检查更新（忽略"忽略本次更新"标记，无更新时提示）。"""
        if self._update_checker is not None and self._update_checker.isRunning():
            return
        # 状态栏提示正在检查
        self.lbl_status.setStyleSheet("color:#F9E2AF;")  # 主题无关的临时提示色
        self.lbl_status.setText("正在检查更新...")
        self._update_checker = UpdateChecker()
        self._update_checker._manual = True
        self._update_checker.result.connect(self._on_update_check_result)
        self._update_checker.start()

    def _on_update_check_result(self, info):
        """后台检查结果：有更新 → 弹窗；手动检查无更新 → 提示。
        所有分支结束后调用 _refresh_status() 恢复串口状态栏（消除"正在检查更新..."提示）。"""
        if info is None:
            # 网络失败：仅手动检查时提示（自动检查静默）；完成后都恢复状态栏
            if getattr(self._update_checker, "_manual", False):
                self.lbl_status.setText("检查更新失败（网络不可用）")
                QMessageBox.information(self, "检查更新",
                                        "无法连接到 GitHub，请检查网络后重试。")
            self._refresh_status()
            return
        latest = info["latest"]
        manual = getattr(self._update_checker, "_manual", False)
        if not _is_newer(latest, APP_VERSION):
            if manual:
                self.lbl_status.setText(f"已是最新版本 v{APP_VERSION}")
                QMessageBox.information(self, "检查更新",
                                        f"当前已是最新版本 v{APP_VERSION}。")
                self._refresh_status()
            return
        # 自动检查：被忽略过的版本不再提示
        if not manual and self._ignore_update_version == latest:
            return
        self._show_update_dialog(info)
        self._refresh_status()

    def _show_update_dialog(self, info: dict):
        """更新弹窗：立即更新 / 下次再说 / 忽略本次更新。"""
        latest = info["latest"]
        box = QMessageBox(self)
        box.setWindowTitle("发现新版本")
        box.setIcon(QMessageBox.Information)
        box.setText(f"发现新版本 v{latest}\n当前版本 v{APP_VERSION}")
        if info.get("body"):
            body = info["body"].strip().splitlines()
            brief = "\n".join(body[:6])
            box.setInformativeText(f"更新说明：\n{brief}")
        btn_update = box.addButton("立即更新", QMessageBox.AcceptRole)
        btn_later = box.addButton("下次再说", QMessageBox.RejectRole)
        btn_ignore = box.addButton("忽略本次更新", QMessageBox.DestructiveRole)
        box.setDefaultButton(btn_update)
        box.exec()
        clicked = box.clickedButton()
        if clicked is btn_update:
            # 打开 release 下载页（exe 已自动附加到 Release 资源）
            webbrowser.open(info["html_url"] or f"https://github.com/{GITHUB_REPO}/releases")
        elif clicked is btn_ignore:
            # 记住忽略的版本，下次自动检查不再提示（手动点击仍可检查）
            self._ignore_update_version = latest
            self._save_params(silent=True)

    # ================= 历史记录 Tab =================
    def _build_history_tab(self):
        w = QWidget()
        lay = QVBoxLayout(w)
        bar = QHBoxLayout()
        btn = QPushButton("删除"); btn.clicked.connect(self._hist_delete); bar.addWidget(btn)
        btn = QPushButton("清空"); btn.clicked.connect(self._hist_clear); bar.addWidget(btn)
        bar.addStretch(1)
        lay.addLayout(bar)
        self.hist_list = QListWidget()
        self.hist_list.itemDoubleClicked.connect(self._hist_load)
        lay.addWidget(self.hist_list, 1)
        self.ms_tabs.addTab(w, "历史记录")

    def _refresh_history_list(self):
        self.hist_list.clear()
        for item in self.send_history:
            disp = item.replace("\n", "⏎ ")
            if len(disp) > 80:
                disp = disp[:80] + "…"
            QListWidgetItem(disp, self.hist_list)

    def _hist_load(self, item):
        idx = self.hist_list.row(item)
        if 0 <= idx < len(self.send_history):
            self.txt_send.setPlainText(self.send_history[idx])

    def _hist_delete(self):
        rows = sorted((self.hist_list.row(i) for i in self.hist_list.selectedItems()),
                      reverse=True)
        for i in rows:
            if 0 <= i < len(self.send_history):
                del self.send_history[i]
        self._refresh_history_list()

    def _hist_clear(self):
        if not self.send_history:
            return
        if QMessageBox.question(self, "确认", "清空全部发送历史？") == QMessageBox.Yes:
            self.send_history = []
            self._refresh_history_list()

    # ================= 快捷命令 Tab =================
    def _build_ms_tab(self):
        w = QWidget()
        lay = QVBoxLayout(w)
        bar = QHBoxLayout()
        btn = QPushButton("＋ 新建"); btn.clicked.connect(self._new_ms_entry); bar.addWidget(btn)
        line = QFrame(); line.setFrameShape(QFrame.VLine); line.setFrameShadow(QFrame.Sunken)
        bar.addWidget(line)
        self.chk_ms_loop = _ThemeCheckBox("循环发送"); bar.addWidget(self.chk_ms_loop)
        bar.addWidget(QLabel("间隔ms:"))
        self.entry_ms_interval = QLineEdit("1000")
        self.entry_ms_interval.setFixedWidth(56)
        bar.addWidget(self.entry_ms_interval)
        btn = QPushButton("▶ 开始"); btn.clicked.connect(self._start_ms_loop); bar.addWidget(btn)
        self.btn_stop_ms = QPushButton("■ 停止")
        self.btn_stop_ms.setEnabled(False)
        self.btn_stop_ms.clicked.connect(self._stop_ms_loop)
        bar.addWidget(self.btn_stop_ms)
        bar.addStretch(1)
        lay.addLayout(bar)

        self.lbl_ms_hint = QLabel("HEX  发送内容（可编辑）            按钮（右键改名 / 点发）")
        lay.addWidget(self.lbl_ms_hint)

        self.ms_scroll = QScrollArea()
        self.ms_scroll.setWidgetResizable(True)
        self.ms_inner = QWidget()
        self.ms_rows = QVBoxLayout(self.ms_inner)
        self.ms_rows.setContentsMargins(2, 2, 2, 2)
        self.ms_rows.setSpacing(2)
        self.ms_rows.addStretch(1)
        self.ms_scroll.setWidget(self.ms_inner)
        lay.addWidget(self.ms_scroll, 1)
        self.ms_tabs.addTab(w, "快捷命令")

    def _refresh_ms_rows(self):
        # 删除旧行（保留末尾 stretch）
        for ws in self._ms_row_widgets:
            for w2 in ws:
                w2.setParent(None)
                w2.deleteLater()
        self._ms_row_widgets = []
        for i, e in enumerate(self.ms_entries):
            row = QHBoxLayout()
            chk = _ThemeCheckBox("HEX")
            chk.setChecked(e["hex"])
            chk.stateChanged.connect(lambda _s, i=i: self._ms_set_hex(i, chk.isChecked()))
            row.addWidget(chk)
            ent = QLineEdit(e["content"])
            ent.textChanged.connect(lambda t, i=i: self._ms_set_content(i, t))
            row.addWidget(ent, 1)
            btn = QPushButton(e["label"])
            btn.setFixedWidth(96)
            btn.clicked.connect(lambda _c, i=i: self._send_ms_entry(i))
            btn.setContextMenuPolicy(Qt.CustomContextMenu)
            btn.customContextMenuRequested.connect(lambda _p, i=i: self._rename_ms_entry(i))
            row.addWidget(btn)
            xbtn = QPushButton("✕")
            xbtn.setFixedWidth(28)
            xbtn.clicked.connect(lambda _c, i=i: self._delete_ms_entry(i))
            row.addWidget(xbtn)
            self.ms_rows.insertLayout(self.ms_rows.count() - 1, row)
            self._ms_row_widgets.append([chk, ent, btn, xbtn])

    def _ms_set_hex(self, i, val):
        if 0 <= i < len(self.ms_entries):
            self.ms_entries[i]["hex"] = bool(val)

    def _ms_set_content(self, i, t):
        if 0 <= i < len(self.ms_entries):
            self.ms_entries[i]["content"] = t

    def _new_ms_entry(self):
        idx = len(self.ms_entries) + 1
        self.ms_entries.append({"hex": False, "content": "", "label": f"发送{idx}"})
        self._refresh_ms_rows()

    def _delete_ms_entry(self, i):
        if 0 <= i < len(self.ms_entries):
            del self.ms_entries[i]
            self._refresh_ms_rows()

    def _rename_ms_entry(self, i):
        if not (0 <= i < len(self.ms_entries)):
            return
        cur = self.ms_entries[i]["label"]
        new, ok = QInputDialog.getText(self, "修改按钮文字",
                                       f"请输入第 {i + 1} 行的按钮显示文字：",
                                       text=cur)
        if ok and new.strip():
            self.ms_entries[i]["label"] = new.strip()
            self._refresh_ms_rows()

    # ================= 历史/快捷命令窗口切换 =================
    def _toggle_window(self, tab_index: int):
        if self.ms_tabs.isVisible():
            if self.ms_tabs.currentIndex() == tab_index:
                self.ms_tabs.hide()
                return
            self.ms_tabs.setCurrentIndex(tab_index)
        else:
            self.ms_tabs.show()
            self.ms_tabs.setCurrentIndex(tab_index)
            if tab_index == 0:
                self._refresh_history_list()

    # ================= 端口 =================
    def _refresh_ports(self):
        self._suppress_apply = True   # 刷新列表期间不触发"自动重开"
        try:
            ports = list(serial.tools.list_ports.comports())

            def sort_key(p):
                m = re.match(r"COM(\d+)", p.device.upper())
                return (0, int(m.group(1))) if m else (1, p.device)

            ports.sort(key=sort_key)
            items, self._port_map = [], {}
            for p in ports:
                # 显示 "COM7  名称"；去掉名称(描述)末尾重复附带的 (COMx)
                desc = p.description.strip()
                desc = re.sub(r"\s*\(?COM\d+\)?\s*$", "", desc)
                label = f"{p.device}  {desc}".strip()
                items.append(label)
                self._port_map[label] = p.device
            if not items:
                items = ["(无可用端口)"]
                self._port_map = {items[0]: ""}
            cur = self.cmb_port.currentText()
            self.cmb_port.clear()
            self.cmb_port.addItems(items)
            if cur in items:
                self.cmb_port.setCurrentText(cur)
        finally:
            self._suppress_apply = False

    def _get_selected_port(self) -> str:
        label = self.cmb_port.currentText()
        return self._port_map.get(label, label.split()[0] if label else "")

    # ================= 更多串口设置弹窗 =================
    def _show_settings_dialog(self):
        if self._settings_win is not None:
            try:
                self._settings_win.showNormal()
                self._settings_win.raise_()
                self._settings_win.activateWindow()
                return
            except Exception:
                pass
        dlg = QDialog(self)
        dlg.setWindowTitle("更多串口设置")
        dlg.setModal(False)
        self._settings_win = dlg
        dlg.finished.connect(lambda _r: setattr(self, "_settings_win", None))
        form = QFormLayout(dlg)

        self.cmb_set_baud = _StyledComboBox()
        self.cmb_set_baud.addItems(DEFAULT_BAUDRATES)
        self.cmb_set_baud.setCurrentText(self.cmb_baud.currentText())
        form.addRow("波特率:", self.cmb_set_baud)

        self.cmb_set_databits = _StyledComboBox()
        self.cmb_set_databits.addItems(DEFAULT_DATABITS)
        self.cmb_set_databits.setCurrentText(self.var_databits)
        form.addRow("数据位:", self.cmb_set_databits)

        self.cmb_set_stopbits = _StyledComboBox()
        self.cmb_set_stopbits.addItems(DEFAULT_STOPBITS)
        self.cmb_set_stopbits.setCurrentText(self.var_stopbits)
        form.addRow("停止位:", self.cmb_set_stopbits)

        self.cmb_set_parity = _StyledComboBox()
        self.cmb_set_parity.addItems(DEFAULT_PARITY)
        self.cmb_set_parity.setCurrentText(self.var_parity)
        form.addRow("校验位:", self.cmb_set_parity)

        self.cmb_set_flow = _StyledComboBox()
        self.cmb_set_flow.addItems(DEFAULT_FLOW)
        self.cmb_set_flow.setCurrentText(self.var_flow)
        form.addRow("流控:", self.cmb_set_flow)

        self.entry_set_timeout = QLineEdit(self.var_read_timeout)
        form.addRow("读超时(ms):", self.entry_set_timeout)

        btns = QHBoxLayout()
        btns.addStretch(1)
        ok = QPushButton("确定")
        ok.clicked.connect(lambda: self._apply_settings(dlg))
        btns.addWidget(ok)
        cancel = QPushButton("取消")
        cancel.clicked.connect(dlg.close)
        btns.addWidget(cancel)
        form.addRow(btns)
        dlg.resize(300, 260)
        dlg.show()

    var_databits = "8"
    var_stopbits = "1"
    var_parity = "None"
    var_flow = "None"
    var_read_timeout = "50"

    def _apply_settings(self, dlg):
        try:
            t = int(self.entry_set_timeout.text())
            if t < 1:
                raise ValueError
            self.var_read_timeout = str(t)
        except ValueError:
            QMessageBox.critical(self, "错误", "读超时必须是正整数")
            return
        try:
            int(self.cmb_set_baud.currentText())
        except ValueError:
            QMessageBox.critical(self, "错误", "波特率必须是数字")
            return
        self._suppress_apply = True   # 批量赋值期间不触发"自动重开"
        try:
            self.cmb_baud.setCurrentText(self.cmb_set_baud.currentText())
            self.var_databits = self.cmb_set_databits.currentText()
            self.var_stopbits = self.cmb_set_stopbits.currentText()
            self.var_parity = self.cmb_set_parity.currentText()
            self.var_flow = self.cmb_set_flow.currentText()
        finally:
            self._suppress_apply = False
        self._refresh_status()
        self._apply_serial_settings()   # 串口已打开 → 立即用新参数重开
        dlg.close()

    # ================= 串口操作 =================
    def _on_port_changed(self, _idx):
        """端口号下拉变化：串口已打开时立即用新端口+当前参数重开。"""
        if self._suppress_apply:
            return
        self._apply_serial_settings()

    def _on_baud_changed(self, _idx):
        """波特率下拉变化：串口已打开时立即用新波特率重开。"""
        if self._suppress_apply:
            return
        self._apply_serial_settings()

    def _apply_serial_settings(self):
        """串口参数被修改：若串口已打开则立即关闭并按新参数重开；未打开仅刷新状态栏。"""
        if not (self.ser and self.ser.is_open):
            self._refresh_status()
            return
        port = self._get_selected_port()
        if not port or port == "(无可用端口)":
            QMessageBox.warning(self, "提示", "请先选择有效的串口")
            self._close_port()
            return
        self._close_port()
        self._open_port()

    def _toggle_port(self):
        if self.ser and self.ser.is_open:
            self._close_port()
        else:
            self._open_port()

    def _open_port(self):
        port = self._get_selected_port()
        if not port or port == "(无可用端口)":
            QMessageBox.warning(self, "提示", "请先选择串口")
            return
        try:
            baud = int(self.cmb_baud.currentText())
            bytesize = int(self.var_databits)
            stopbits = float(self.var_stopbits)
            parity = self.var_parity[0]
            flow = self.var_flow
            rtscts = (flow == "RTS/CTS")
            xonxoff = (flow == "XON/XOFF")
            read_to = max(1, int(self.var_read_timeout)) / 1000.0
            self.ser = serial.Serial(
                port=port, baudrate=baud, bytesize=bytesize, parity=parity,
                stopbits=stopbits, rtscts=rtscts, xonxoff=xonxoff, timeout=read_to)
            self.ser.dtr = False
            self.ser.rts = False
            self.reader = SerialReader(self.ser)
            # 每次开串口递增代际号并绑定到信号：关闭后旧线程排队的
            # received/error 信号到达时因代际不匹配被丢弃，避免"关闭后还收数据"
            self._rx_epoch += 1
            epoch = self._rx_epoch
            self.reader.received.connect(
                lambda d, e=epoch: self._on_serial_data(d, e))
            self.reader.error.connect(
                lambda m, e=epoch: self._on_serial_error(m, e))
            self.reader.start()
            self.btn_open.setText("关闭串口")
            self._refresh_status()
            self._add_record("sys", f"串口已打开: {port} @ {baud}")
        except Exception as e:
            QMessageBox.critical(self, "打开失败", str(e))
            self.ser = None

    def _close_port(self):
        if self.reader is not None:
            try:
                self.reader.stop()
                self.reader.wait(500)
            except Exception:
                pass
            self.reader = None
        if self.timer_send.isActive():
            self.timer_send.stop()
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        # 代际号递增：关闭瞬间使旧串口线程排队的 received/error 信号全部失效
        self._rx_epoch += 1
        # 丢弃尚未刷新的接收缓冲（关闭瞬间 pending 里的残留数据）
        self.pending_rx = []
        self.btn_open.setText("打开串口")
        self._refresh_status()
        self._add_record("sys", "串口已关闭")

    def _on_serial_data(self, data: bytes, epoch: int):
        """收到数据。epoch 不匹配说明是已关闭串口排队的残留信号，直接丢弃。"""
        if epoch != self._rx_epoch:
            return
        self.rx_bytes += len(data)
        self.pending_rx.append(self._rx_body(data))
        if len(self.pending_rx) > MAX_RECORDS:
            self.pending_rx = self.pending_rx[-MAX_RECORDS:]

    def _on_serial_error(self, msg: str, epoch: int):
        """读线程报错。旧串口排队的 error 信号同样按代际丢弃。"""
        if epoch != self._rx_epoch:
            return
        self._add_record("sys", f"[读取异常] {msg}")
        self._close_port()

    def _refresh_status(self):
        port = self._get_selected_port() or "-"
        flow_short = FLOW_SHORT.get(self.var_flow, "N")
        param = (f"{self.cmb_baud.currentText()},{self.var_databits},"
                 f"{self.var_parity[0]},{self.var_stopbits},{flow_short}")
        t = THEMES[self._theme]
        if self.ser and self.ser.is_open:
            self.lbl_status.setText(f"{port} 已打开 {param}")
            self.lbl_status.setStyleSheet(f"color:{t['ok_color']};")
        else:
            self.lbl_status.setText(f"{port} 已关闭 {param}")
            self.lbl_status.setStyleSheet(f"color:{t['text_primary']};")

    # ================= 接收渲染 =================
    def _rx_body(self, data: bytes) -> str:
        """把收到的字节转成原始文本（HEX 或按编码解码，不含时间戳/箭头前缀）。"""
        if self.chk_show_hex.isChecked():
            return " ".join(f"{b:02X}" for b in data)
        enc = self.cmb_encoding.currentText()
        try:
            return data.decode(enc, errors="replace")
        except LookupError:
            return data.decode("utf-8", errors="replace")
        except Exception:
            return data.decode("latin-1", errors="replace")

    def _render_record(self, rec) -> str:
        """按当前"时间戳"开关把缓存记录（原始内容 raw）渲染为显示文本。
        rx 恒带 <、tx 恒带 > 指示箭头（时间戳可选）；sys 消息时间戳可选。"""
        kind = rec["kind"]
        raw = rec["raw"]
        if kind == "rx":
            mark = now_ts() + "<" if self.chk_show_ts.isChecked() else "<"
        elif kind == "tx":
            mark = now_ts() + ">" if self.chk_show_ts.isChecked() else ">"
        else:  # sys
            mark = now_ts() + " " if self.chk_show_ts.isChecked() else ""
        # 多行数据：续行对齐到 mark 宽度（分包显示）
        indent = " " * len(mark)
        text = ("\n" + indent).join(raw.split("\n"))
        return mark + text + "\n"

    def _add_record(self, kind: str, raw: str):
        """存一条消息的原始内容（不含时间戳），渲染时按当前开关格式化。"""
        self.records.append({"kind": kind, "raw": raw})
        self._append_to_view([self._render_record({"kind": kind, "raw": raw})])

    def _add_records_batch(self, bodies):
        """批量追加接收消息的原始内容（一次插入，性能好）。"""
        for b in bodies:
            self.records.append({"kind": "rx", "raw": b})
        self._append_to_view([self._render_record({"kind": "rx", "raw": b})
                              for b in bodies])

    def _append_to_view(self, displays):
        self._program_scroll = True
        try:
            # 超上限裁剪最旧（保持内存与视图一致）
            if len(self.records) > MAX_RECORDS:
                overflow = len(self.records) - MAX_RECORDS
                removed = self.records[:overflow]
                del self.records[:overflow]
                if self.chk_filter.isChecked():
                    self._refresh_view()
                    return
                del_lines = sum(self._render_record(r).count("\n") for r in removed)
                self._delete_top_lines(del_lines)

            needle = self.entry_search.text()
            if self.chk_filter.isChecked() and needle:
                displays = [d for d in displays if needle.lower() in d.lower()]
                if not displays:
                    return

            cursor = self.txt_recv.textCursor()
            cursor.movePosition(QTextCursor.End)
            cursor.insertText("".join(displays))
            if needle:
                self._apply_highlight()
            if not self.chk_pause.isChecked():
                self.txt_recv.verticalScrollBar().setValue(
                    self.txt_recv.verticalScrollBar().maximum())
        finally:
            self._program_scroll = False

    def _delete_top_lines(self, n: int):
        doc = self.txt_recv.document()
        cursor = QTextCursor(doc)
        cursor.movePosition(QTextCursor.Start)
        for _ in range(n):
            if not cursor.movePosition(QTextCursor.Down, QTextCursor.KeepAnchor):
                break
        cursor.removeSelectedText()

    def _refresh_view(self):
        """按 筛选/搜索/时间戳开关 状态从内存 records 重建接收区视图。"""
        needle = self.entry_search.text()
        filt = self.chk_filter.isChecked()
        self._program_scroll = True
        try:
            self.txt_recv.blockSignals(True)
            self.txt_recv.clear()
            for rec in self.records:
                rendered = self._render_record(rec)
                if filt and needle and needle.lower() not in rendered.lower():
                    continue
                self.txt_recv.moveCursor(QTextCursor.End)
                self.txt_recv.insertPlainText(rendered)
            self.txt_recv.blockSignals(False)
            self._apply_highlight()
            if not self.chk_pause.isChecked():
                self.txt_recv.verticalScrollBar().setValue(
                    self.txt_recv.verticalScrollBar().maximum())
        finally:
            self._program_scroll = False

    def _apply_highlight(self):
        needle = self.entry_search.text()
        sels = []
        if needle:
            fmt = QTextCharFormat()
            fmt.setBackground(QColor("#ffe066"))
            fmt.setForeground(QColor("#000000"))
            doc = self.txt_recv.document()
            cursor = QTextCursor(doc)
            while True:
                cursor = doc.find(needle, cursor)
                if cursor.isNull():
                    break
                sel = QTextEdit.ExtraSelection()
                sel.format = fmt
                sel.cursor = cursor
                sels.append(sel)
        self.txt_recv.setExtraSelections(sels)

    def _on_search_change(self):
        # 防抖：停止输入 200ms 后再刷新（避免每次按键全量重建）
        if self._search_timer is not None:
            self._search_timer.stop()
        self._search_timer = QTimer(self)
        self._search_timer.setSingleShot(True)
        self._search_timer.timeout.connect(self._refresh_view)
        self._search_timer.start(200)

    def _on_recv_scroll(self, value):
        """滚轮上翻/拖动滚动条上移 → 自动勾选暂停刷新；滚回底部自动取消。
        程序性滚动（搜索刷新/批量插入/清空引起的滚动条变化）不勾选。"""
        if self._program_scroll:
            return
        sb = self.txt_recv.verticalScrollBar()
        self.chk_pause.setChecked(value < sb.maximum())

    def _drain_rx(self):
        """50ms 周期：把缓冲的接收数据显示到接收区 + 节流刷新状态栏。"""
        if self.pending_rx:
            self._add_records_batch(self.pending_rx)
            self.pending_rx = []
        # 状态栏节流 500ms
        now = time.monotonic()
        if now - self._last_status_ts >= 0.5:
            self._last_status_ts = now
            sr = f"S:{self.tx_bytes}  R:{self.rx_bytes}"
            if sr != self._last_sr_text:
                self.lbl_sr.setText(sr)
                self._last_sr_text = sr
            if self.ser and self.ser.is_open:
                try:
                    ms = self.ser.getModemStatusBits()
                    hs = (f"CTS={1 if ms.get('CTS') else 0} "
                          f"DSR={1 if ms.get('DSR') else 0} RLSD={1 if ms.get('CD') else 0}")
                    if hs != self._last_hs_text:
                        self.lbl_handshake.setText(hs)
                        self._last_hs_text = hs
                except Exception:
                    pass
            elif self._last_hs_text != "CTS=0 DSR=0 RLSD=0":
                self.lbl_handshake.setText("CTS=0 DSR=0 RLSD=0")
                self._last_hs_text = "CTS=0 DSR=0 RLSD=0"

    def _clear_recv(self):
        self._program_scroll = True
        try:
            self.txt_recv.clear()
            self.records.clear()
        finally:
            self._program_scroll = False

    def _save_recv(self):
        path, _ = QFileDialog.getSaveFileName(self, "保存数据", "",
                                              "文本文件 (*.txt);;所有文件 (*.*)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8", errors="replace") as f:
                f.write(self.txt_recv.toPlainText())
            QMessageBox.information(self, "保存", f"已保存到\n{path}")
        except Exception as e:
            QMessageBox.critical(self, "保存失败", str(e))

    # ================= 校验 =================
    def _compute_checksum(self, data: bytes, algorithm: str) -> bytes:
        if not data or algorithm == "None":
            return b""
        if algorithm == "0-ADD8":
            s = sum(data) & 0xFF
            return bytes([(-s) & 0xFF])
        if algorithm == "ADD8":
            return bytes([sum(data) & 0xFF])
        if algorithm == "XOR8":
            x = 0
            for b in data:
                x ^= b
            return bytes([x])
        if algorithm == "ADD16":
            s = sum(data) & 0xFFFF
            return bytes([(s >> 8) & 0xFF, s & 0xFF])
        if algorithm == "ModbusCRC16":
            crc = 0xFFFF
            for b in data:
                crc ^= b
                for _ in range(8):
                    crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
            return bytes([crc & 0xFF, (crc >> 8) & 0xFF])
        if algorithm == "CCITT-CRC16":
            crc = 0xFFFF
            for b in data:
                crc ^= b << 8
                for _ in range(8):
                    crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
            return bytes([(crc >> 8) & 0xFF, crc & 0xFF])
        if algorithm == "CRC32":
            return struct.pack("<I", zlib.crc32(data) & 0xFFFFFFFF)
        return b""

    def _checksum_range(self, data: bytes) -> bytes:
        try:
            start = int(self.entry_cs_start.text())
        except ValueError:
            start = 1
        if start < 1:
            start = 1
        try:
            end_off = int(self.entry_cs_end.text())
        except ValueError:
            end_off = 0
        if end_off > 0:
            end_off = 0
        lo = start - 1
        hi = len(data) + end_off
        if hi > len(data):
            hi = len(data)
        if hi <= lo:
            return b""
        return data[lo:hi]

    def _update_cs_highlight(self, *_):
        """加校验≠None 时，校验参数整组高亮（主题橙色边框+浅橙底）；None 恢复无框。
        通过动态属性 cs_highlight 驱动全局 QSS 的属性选择器，避免子控件样式被截断。"""
        if not hasattr(self, "cs_group"):
            return
        on = self.cmb_checksum.currentText() != "None"
        if bool(self.cs_group.property("cs_highlight")) == on:
            return
        self.cs_group.setProperty("cs_highlight", on)
        # 属性变化后需要重新应用样式
        self.cs_group.style().unpolish(self.cs_group)
        self.cs_group.style().polish(self.cs_group)

    def _apply_checksum(self, data: bytes):
        if not data:
            self.lbl_cs_result.setText("")
            return data, b""
        algo = self.cmb_checksum.currentText()
        if algo == "None":
            self.lbl_cs_result.setText("")
            return data, b""
        head = self._checksum_range(data)
        if not head:
            self.lbl_cs_result.setText("")
            return data, b""
        cs = self._compute_checksum(head, algo)
        if not cs:
            self.lbl_cs_result.setText("")
            return data, b""
        self.lbl_cs_result.setText(cs.hex(" ").upper())
        return data + cs, cs

    def _update_cs_preview(self):
        """防抖 100ms 后根据发送框内容实时预览校验结果。"""
        if self._cs_preview_timer is not None:
            self._cs_preview_timer.stop()
        self._cs_preview_timer = QTimer(self)
        self._cs_preview_timer.setSingleShot(True)
        self._cs_preview_timer.timeout.connect(self._do_cs_preview)
        self._cs_preview_timer.start(100)

    def _do_cs_preview(self):
        try:
            raw = self.txt_send.toPlainText()
            if not raw:
                self.lbl_cs_result.setText("")
                return
            data = self._encode_send_text(raw)
            self._apply_checksum(data)
        except Exception:
            self.lbl_cs_result.setText("")

    # ================= 发送 =================
    def _encode_send_text(self, raw: str) -> bytes:
        if self.chk_send_hex.isChecked():
            hex_str = (raw.replace(" ", "").replace(",", "").replace("\n", "")
                       .replace("\r", "").replace("\t", "")
                       .replace("0x", "").replace("0X", ""))
            if len(hex_str) % 2 != 0:
                raise ValueError("HEX 字符串长度必须为偶数")
            if not hex_str:
                return b""
            data = bytes.fromhex(hex_str)
        else:
            data = raw.encode("utf-8", errors="replace")
        return data

    def _send(self):
        raw = self.txt_send.toPlainText()
        self._do_send(raw)

    def _do_send(self, raw: str):
        if not (self.ser and self.ser.is_open):
            QMessageBox.warning(self, "提示", "请先打开串口")
            return
        if not raw:
            return
        self._record_send_history(raw)
        try:
            data = self._encode_send_text(raw)
            data, cs_bytes = self._apply_checksum(data)
            if self.chk_add_crlf.isChecked():
                data += b"\r\n"
            self.ser.write(data)
            self.tx_bytes += len(data)
            self._echo_send(raw, data, cs_bytes)
            self._save_current_send_slot()
        except ValueError as e:
            QMessageBox.critical(self, "HEX错误", str(e))
        except Exception as e:
            QMessageBox.critical(self, "发送失败", str(e))

    def _echo_send(self, raw: str, data: bytes, cs_bytes: bytes = b""):
        """回显发送内容；附加的校验字节用中括号 [..] 标注。"""
        if self.chk_send_hex.isChecked() or self.chk_show_hex.isChecked():
            if cs_bytes:
                body = data[:len(data) - len(cs_bytes)]
                echo = " ".join(f"{b:02X}" for b in body)
                echo += " [" + " ".join(f"{b:02X}" for b in cs_bytes) + "]"
            else:
                echo = " ".join(f"{b:02X}" for b in data)
        else:
            echo = raw
            if cs_bytes:
                echo += " [" + " ".join(f"{b:02X}" for b in cs_bytes) + "]"
        self._add_record("tx", echo)

    def _record_send_history(self, raw: str):
        if not raw:
            return
        if raw in self.send_history:
            self.send_history.remove(raw)
        self.send_history.insert(0, raw)
        if len(self.send_history) > 50:
            self.send_history = self.send_history[:50]
        self._refresh_history_list()

    def _clear_send(self):
        self.txt_send.clear()

    # 发送框内容槽位（文本/HEX 双槽记忆）
    def _save_current_send_slot(self):
        raw = self.txt_send.toPlainText()
        if self.chk_send_hex.isChecked():
            self.last_send_hex = raw
        else:
            self.last_send_text = raw

    def _load_send_slot(self):
        content = self.last_send_hex if self.chk_send_hex.isChecked() else self.last_send_text
        self.txt_send.blockSignals(True)
        self.txt_send.setPlainText(content)
        self.txt_send.blockSignals(False)
        self._update_cs_preview()

    def _on_send_hex_toggle(self, *_):
        new_mode = self.chk_send_hex.isChecked()
        raw = self.txt_send.toPlainText()
        if self._send_hex_prev:
            self.last_send_hex = raw
        else:
            self.last_send_text = raw
        self._send_hex_prev = new_mode
        self._load_send_slot()

    # ================= 定时发送 =================
    def _on_timer_toggle(self, *_):
        if self.chk_timer.isChecked():
            try:
                interval = max(10, int(self.entry_interval.text()))
            except ValueError:
                interval = 1000
            self.entry_interval.setText(str(interval))
            if self.ser and self.ser.is_open:
                self.timer_send.start(interval)
        else:
            self.timer_send.stop()

    def _timer_send_tick(self):
        if not (self.ser and self.ser.is_open):
            self.timer_send.stop()
            self.chk_timer.setChecked(False)
            return
        self._send()

    # ================= 多字符串发送 =================
    def _send_ms_entry(self, i: int):
        if not (self.ser and self.ser.is_open):
            QMessageBox.warning(self, "提示", "请先打开串口")
            return
        if not (0 <= i < len(self.ms_entries)):
            return
        e = self.ms_entries[i]
        raw = e["content"]
        if not raw:
            return
        try:
            if e["hex"]:
                hex_str = (raw.replace(" ", "").replace(",", "").replace("\n", "")
                           .replace("\r", "").replace("\t", "")
                           .replace("0x", "").replace("0X", ""))
                if len(hex_str) % 2 != 0:
                    QMessageBox.critical(self, "HEX错误",
                                         f"第 {i + 1} 行 HEX 字符串长度必须为偶数")
                    return
                data = bytes.fromhex(hex_str)
            else:
                data = raw.encode("utf-8", errors="replace")
            data, cs_bytes = self._apply_checksum(data)
            if self.chk_add_crlf.isChecked():
                data += b"\r\n"
            self.ser.write(data)
            self.tx_bytes += len(data)
            if e["hex"] or self.chk_show_hex.isChecked():
                if cs_bytes:
                    body = data[:len(data) - len(cs_bytes)]
                    echo = " ".join(f"{b:02X}" for b in body)
                    echo += " [" + " ".join(f"{b:02X}" for b in cs_bytes) + "]"
                else:
                    echo = " ".join(f"{b:02X}" for b in data)
            else:
                echo = raw
                if cs_bytes:
                    echo += " [" + " ".join(f"{b:02X}" for b in cs_bytes) + "]"
            self._add_record("tx", echo)
            self._record_send_history(raw)
        except ValueError as ex:
            QMessageBox.critical(self, "HEX错误", str(ex))
        except Exception as ex:
            QMessageBox.critical(self, "发送失败", str(ex))

    def _start_ms_loop(self):
        if not (self.ser and self.ser.is_open):
            QMessageBox.warning(self, "提示", "请先打开串口")
            return
        if not self.ms_entries:
            QMessageBox.information(self, "提示", "列表为空，请先新建条目")
            return
        try:
            interval = max(10, int(self.entry_ms_interval.text()))
        except ValueError:
            interval = 1000
        self.entry_ms_interval.setText(str(interval))
        self._ms_looping = True
        self._ms_loop_index = 0
        self.btn_stop_ms.setEnabled(True)
        self.ms_loop_timer.start(interval)

    def _ms_loop_tick(self):
        if not self._ms_looping:
            self.ms_loop_timer.stop()
            return
        if not (self.ser and self.ser.is_open):
            self._stop_ms_loop()
            return
        if self._ms_loop_index >= len(self.ms_entries):
            if self.chk_ms_loop.isChecked():
                self._ms_loop_index = 0
            else:
                self._stop_ms_loop()
                return
        self._send_ms_entry(self._ms_loop_index)
        self._ms_loop_index += 1

    def _stop_ms_loop(self):
        self._ms_looping = False
        self.ms_loop_timer.stop()
        self.btn_stop_ms.setEnabled(False)

    # ================= 文件发送 =================
    def _pick_file(self):
        path, _ = QFileDialog.getOpenFileName(self, "选择文件", "", "所有文件 (*.*)")
        if path:
            # 按钮文本显示完整路径（提示 + 完整路径）
            self.btn_select_file.setText(os.path.basename(path))
            self.btn_select_file.setToolTip(path)
            self._selected_file_path = path

    def _send_file(self):
        if self._file_sending:
            self._stop_send_file()
            return
        if not (self.ser and self.ser.is_open):
            QMessageBox.warning(self, "提示", "请先打开串口")
            return
        path = getattr(self, "_selected_file_path", "") or ""
        if not path or not os.path.exists(path):
            QMessageBox.warning(self, "提示", "请先点击「选择文件」选择要发送的文件")
            return
        try:
            with open(path, "rb") as f:
                data = f.read()
        except Exception as e:
            QMessageBox.critical(self, "读取失败", str(e))
            return
        data, _cs = self._apply_checksum(data)
        if self.chk_add_crlf.isChecked() and not path.lower().endswith((".bin", ".hex")):
            data += b"\r\n"
        self._file_data = data
        self._file_offset = 0
        self._file_sending = True
        self.btn_send_file.setText("停止发送")
        self._add_record("sys", f"开始发送文件: {os.path.basename(path)} ({len(data)} 字节)")
        QTimer.singleShot(0, self._send_file_chunk)

    def _send_file_chunk(self):
        if not self._file_sending:
            self._end_send_file()
            return
        if not (self.ser and self.ser.is_open):
            self._end_send_file()
            return
        chunk = self._file_data[self._file_offset:self._file_offset + 4096]
        if not chunk:
            self._end_send_file()
            self._add_record("sys", "文件发送完成")
            return
        try:
            self.ser.write(chunk)
            self.tx_bytes += len(chunk)
        except Exception as e:
            QMessageBox.critical(self, "发送失败", str(e))
            self._end_send_file()
            return
        self._file_offset += len(chunk)
        QTimer.singleShot(2, self._send_file_chunk)

    def _stop_send_file(self):
        self._file_sending = False
        self._end_send_file()
        self._add_record("sys", "文件发送已停止")

    def _end_send_file(self):
        self._file_sending = False
        self._file_data = b""
        self._file_offset = 0
        self.btn_send_file.setText("发送文件")

    # ================= GitHub =================
    def _open_github(self):
        QDesktopServices.openUrl(QUrl(GITHUB_URL))

    # ================= 配置保存/加载 =================
    def _config_path(self) -> str:
        if getattr(sys, "frozen", False):
            base = os.path.dirname(sys.executable)
        else:
            base = os.path.dirname(os.path.abspath(__file__))
        return os.path.join(base, "sscom_config.json")

    def _icon_path(self) -> str:
        if getattr(sys, "frozen", False):
            base = os.path.dirname(sys.executable)
        else:
            base = os.path.dirname(os.path.abspath(__file__))
        return os.path.join(base, "imgs", "ic_xue_xi.png")

    def _ms_entries_to_data(self):
        return [{"hex": bool(e["hex"]), "content": e["content"], "label": e["label"]}
                for e in self.ms_entries]

    def _save_params(self, silent=True):
        self._save_current_send_slot()
        cfg = {
            "port": self._get_selected_port(),
            "baud": self.cmb_baud.currentText(),
            "databits": self.var_databits,
            "stopbits": self.var_stopbits,
            "parity": self.var_parity,
            "flow": self.var_flow,
            "read_timeout_ms": self.var_read_timeout,
            "show_hex": self.chk_show_hex.isChecked(),
            "send_hex": self.chk_send_hex.isChecked(),
            "add_crlf": self.chk_add_crlf.isChecked(),
            "interval_ms": self.entry_interval.text(),
            "show_ts": self.chk_show_ts.isChecked(),
            "ms_entries": self._ms_entries_to_data(),
            "send_history": self.send_history,
            "checksum": self.cmb_checksum.currentText(),
            "cs_start": self.entry_cs_start.text().strip() or "1",
            "cs_end": self.entry_cs_end.text().strip() or "0",
            "encoding": self.cmb_encoding.currentText(),
            "last_send_text": self.last_send_text,
            "last_send_hex": self.last_send_hex,
            "window_w": self.width(),
            "window_h": self.height(),
            "panel_visible": not self.ms_tabs.isHidden(),
            "panel_tab": self.ms_tabs.currentIndex(),
            "theme": self._theme,
            "ignore_update_version": self._ignore_update_version,
        }
        try:
            with open(self._config_path(), "w", encoding="utf-8") as f:
                json.dump(cfg, f, ensure_ascii=False, indent=2)
        except Exception as e:
            if not silent:
                QMessageBox.critical(self, "保存失败", str(e))

    def _load_params(self, silent=True):
        self._suppress_apply = True   # 配置加载期间不触发"自动重开"
        try:
            self._load_params_inner(silent)
        finally:
            self._suppress_apply = False

    def _load_params_inner(self, silent=True):
        try:
            with open(self._config_path(), "r", encoding="utf-8") as f:
                cfg = json.load(f)
            # 主题优先应用（其余控件样式依赖全局 QSS）
            theme = cfg.get("theme", "light")
            if theme not in THEMES:
                theme = "light"
            self.apply_theme(theme)
            self._ignore_update_version = cfg.get("ignore_update_version")
            self.cmb_baud.setCurrentText(cfg.get("baud", "115200"))
            self.var_databits = cfg.get("databits", "8")
            self.var_stopbits = cfg.get("stopbits", "1")
            self.var_parity = cfg.get("parity", "None")
            self.var_flow = cfg.get("flow", "None")
            self.var_read_timeout = cfg.get("read_timeout_ms", "50")
            self.chk_show_hex.setChecked(cfg.get("show_hex", False))
            self.chk_send_hex.setChecked(cfg.get("send_hex", False))
            self.chk_add_crlf.setChecked(cfg.get("add_crlf", False))
            self.entry_interval.setText(cfg.get("interval_ms", "1000"))
            self.chk_show_ts.setChecked(cfg.get("show_ts", True))
            # 端口匹配
            target = cfg.get("port", "")
            for label, dev in self._port_map.items():
                if dev == target:
                    self.cmb_port.setCurrentText(label)
                    break
            # 多字符串发送面板（含旧字段迁移）
            ms_data = cfg.get("ms_entries")
            if ms_data is None and (cfg.get("send_history") or cfg.get("quick_cmds")):
                seen = set()
                ms_data = []
                for content in (cfg.get("quick_cmds", []) + cfg.get("send_history", [])):
                    if content and content not in seen:
                        ms_data.append({"hex": False, "content": content,
                                        "label": content.replace("\n", " ")[:8] or "导入"})
                        seen.add(content)
            self.ms_entries = []
            for d in (ms_data or []):
                self.ms_entries.append({
                    "hex": bool(d.get("hex", False)),
                    "content": str(d.get("content", "")),
                    "label": str(d.get("label", "发送")) or "发送",
                })
            self._refresh_ms_rows()
            self.send_history = list(cfg.get("send_history", []))[:50]
            self._refresh_history_list()
            ck = cfg.get("checksum", "None")
            if ck in CHECKSUMS:
                self.cmb_checksum.setCurrentText(ck)
            try:
                self.entry_cs_start.setText(str(max(1, int(cfg.get("cs_start", "1")))))
            except Exception:
                pass
            try:
                v = int(cfg.get("cs_end", "0"))
                self.entry_cs_end.setText(str(min(0, v)))
            except Exception:
                pass
            enc = cfg.get("encoding", "UTF-8")
            if enc in ENCODINGS:
                self.cmb_encoding.setCurrentText(enc)
            self.last_send_text = str(cfg.get("last_send_text", ""))
            self.last_send_hex = str(cfg.get("last_send_hex", ""))
            self._send_hex_prev = self.chk_send_hex.isChecked()
            self._load_send_slot()
            # 窗口大小恢复（校验最小值与屏幕范围）
            w = cfg.get("window_w")
            h = cfg.get("window_h")
            if isinstance(w, int) and isinstance(h, int):
                if w < self.minimumWidth():
                    w = self.minimumWidth()
                if h < self.minimumHeight():
                    h = self.minimumHeight()
                scr = self.screen()
                if scr is not None:
                    geo = scr.availableGeometry()
                    if w > geo.width():
                        w = geo.width()
                    if h > geo.height():
                        h = geo.height()
                self.resize(w, h)
            # 命令面板（历史/快捷命令）显示状态恢复
            if cfg.get("panel_visible"):
                self.ms_tabs.show()
                idx = cfg.get("panel_tab", 0)
                if isinstance(idx, int) and 0 <= idx < self.ms_tabs.count():
                    self.ms_tabs.setCurrentIndex(idx)
                if self.ms_tabs.currentIndex() == 0:
                    self._refresh_history_list()
            self._refresh_status()
        except FileNotFoundError:
            pass
        except Exception as e:
            if not silent:
                QMessageBox.critical(self, "加载失败", str(e))

    def _autoload_config(self):
        if os.path.exists(self._config_path()):
            self._load_params(silent=True)
        else:
            self._save_params(silent=True)

    # ================= 关闭 =================
    def closeEvent(self, event):
        self._save_params(silent=True)
        self._close_port()
        try:
            if self._settings_win is not None:
                self._settings_win.close()
        except Exception:
            pass
        event.accept()


def _resource_path(name: str) -> str:
    """定位打包内的资源文件：PyInstaller onefile 解压目录(_MEIPASS) / exe 目录 / 源码目录。"""
    base = getattr(sys, "_MEIPASS", None)
    if not base:
        base = os.path.dirname(sys.executable) if getattr(sys, "frozen", False) \
            else os.path.dirname(os.path.abspath(__file__))
    return os.path.join(base, name)


def main():
    app = QApplication(sys.argv)
    # 统一控件文字字号 = 接收区数据文本字号（10pt），字体族保持系统默认（中文正常显示）
    try:
        _f = app.font()
        _f.setPointSize(10)
        app.setFont(_f)
    except Exception:
        pass
    # 应用图标（窗口标题栏 + 任务栏）——打包后从 _MEIPASS 加载，保证图标正确显示
    try:
        _icon_path = _resource_path(os.path.join("imgs", "ic_xue_xi.png"))
        if os.path.exists(_icon_path):
            app.setWindowIcon(QIcon(_icon_path))
    except Exception:
        pass
    win = SerialTool()
    try:
        if not app.windowIcon().isNull():
            win.setWindowIcon(app.windowIcon())
    except Exception:
        pass
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
