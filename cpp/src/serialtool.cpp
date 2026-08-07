#include "serialtool.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTabWidget>
#include <QScrollArea>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QFrame>
#include <QProgressBar>
#include <QToolButton>
#include <QScrollBar>
#include <QMenu>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QFont>
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QUrl>
#include <QDesktopServices>
#include <QMouseEvent>
#include <QEvent>
#include <QSerialPortInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QDir>
#include <QDateTime>
#include <QProcess>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QHash>
#include <algorithm>
#include <functional>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

#include "utils.h"
#include "updater.h"

namespace {
// 程序性滚动保护（构造函数置 true，析构恢复 false）
struct ScrollGuard {
    bool& flag;
    explicit ScrollGuard(bool& f) : flag(f) { flag = true; }
    ~ScrollGuard() { flag = false; }
};
} // namespace

// ================= 构造 =================

SerialTool::SerialTool(QWidget* parent) : QWidget(parent) {
    buildUi();
    applyTheme(m_theme);
    refreshPorts();
    autoloadConfig();
    // 监听配置文件（多实例快捷命令实时同步；autoload 之后添加，避免加载自身写入误触发）
    m_cfgWatcher = new QFileSystemWatcher(this);
    connect(m_cfgWatcher, &QFileSystemWatcher::fileChanged,
            this, &SerialTool::onConfigFileChanged);
    if (QFileInfo::exists(configPath()))
        m_cfgWatcher->addPath(configPath());
    m_cfgSaveTimer = new QTimer(this);
    m_cfgSaveTimer->setSingleShot(true);
    connect(m_cfgSaveTimer, &QTimer::timeout, this, [this] { saveParams(true); });
    // 启动延迟 2 秒自动检查更新（后台线程，不阻塞 UI；失败静默）
    QTimer::singleShot(2000, this, &SerialTool::checkUpdateAuto);

    // 无边框窗口边缘缩放（事件过滤器全局拦截）
    m_resizeMargin = 8;
    m_resizeEdge = 0;
    installResizeFilter();

    m_flushTimer = new QTimer(this);
    connect(m_flushTimer, &QTimer::timeout, this, &SerialTool::drainRx);
    m_flushTimer->start(50);
}

// ================= 事件（缩放 / DWM / 背景 / 关闭） =================

void SerialTool::installResizeFilter() {
    m_filterInstalled.insert(this);
    setMouseTracking(true);
    installEventFilter(this);
    const auto children = findChildren<QWidget*>();
    for (auto* child : children) {
        m_filterInstalled.insert(child);
        child->setMouseTracking(true);
        child->installEventFilter(this);
    }
}

int SerialTool::detectResizeEdge(const QPoint& pos) const {
    if (isMaximized())
        return 0;
    const int m = m_resizeMargin;
    const int w = width(), h = height();
    int edges = 0;
    if (pos.x() <= m)
        edges |= EdgeLeft;
    if (pos.x() >= w - m)
        edges |= EdgeRight;
    if (pos.y() <= m)
        edges |= EdgeTop;
    if (pos.y() >= h - m)
        edges |= EdgeBottom;
    return edges;
}

Qt::CursorShape SerialTool::resizeCursor(int edges) const {
    if (edges == EdgeLeft || edges == EdgeRight)
        return Qt::SizeHorCursor;
    if (edges == EdgeTop || edges == EdgeBottom)
        return Qt::SizeVerCursor;
    if (edges == (EdgeLeft | EdgeTop) || edges == (EdgeRight | EdgeBottom))
        return Qt::SizeFDiagCursor;
    if (edges == (EdgeRight | EdgeTop) || edges == (EdgeLeft | EdgeBottom))
        return Qt::SizeBDiagCursor;
    return Qt::ArrowCursor;
}

void SerialTool::applyResize(const QPoint& gpos) {
    const int edge = m_resizeEdge;
    QRect geo = m_resizeStartGeo;
    const int dx = gpos.x() - m_resizeStartPos.x();
    const int dy = gpos.y() - m_resizeStartPos.y();
    const int minW = minimumWidth(), minH = minimumHeight();
    if (edge & EdgeLeft) {
        const int newW = geo.width() - dx;
        if (newW >= minW) {
            geo.setX(geo.x() + dx);
            geo.setWidth(newW);
        } else {
            geo.setX(geo.x() + geo.width() - minW);
            geo.setWidth(minW);
        }
    }
    if (edge & EdgeRight)
        geo.setWidth(qMax(minW, geo.width() + dx));
    if (edge & EdgeTop) {
        const int newH = geo.height() - dy;
        if (newH >= minH) {
            geo.setY(geo.y() + dy);
            geo.setHeight(newH);
        } else {
            geo.setY(geo.y() + geo.height() - minH);
            geo.setHeight(minH);
        }
    }
    if (edge & EdgeBottom)
        geo.setHeight(qMax(minH, geo.height() + dy));
    setGeometry(geo);
}

bool SerialTool::eventFilter(QObject* obj, QEvent* event) {
    // 动态补装：配置加载后创建的控件
    if (auto* w = qobject_cast<QWidget*>(obj); w && w != this
        && !m_filterInstalled.contains(w)) {
        m_filterInstalled.insert(w);
        w->setMouseTracking(true);
        w->installEventFilter(this);
    }
    const QEvent::Type t = event->type();
    if (t == QEvent::MouseMove || t == QEvent::MouseButtonPress
        || t == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint gpos = me->globalPosition().toPoint();
        const QPoint pos = mapFromGlobal(gpos);
        if (t == QEvent::MouseButtonRelease) {
            if (m_resizeEdge != 0) {
                m_resizeEdge = 0;
                m_resizeStartPos = QPoint();
                return true;
            }
            return false;
        }
        if (t == QEvent::MouseButtonPress) {
            if (me->button() == Qt::LeftButton) {
                const int edges = detectResizeEdge(pos);
                if (edges) {
                    m_resizeEdge = edges;
                    m_resizeStartPos = gpos;
                    m_resizeStartGeo = geometry();
                    return true;
                }
            }
            return false;
        }
        // MouseMove
        if (m_resizeEdge != 0) {
            applyResize(gpos);
            return true;
        }
        const int edges = detectResizeEdge(pos);
        const Qt::CursorShape cur = resizeCursor(edges);
        if (cur != Qt::ArrowCursor) {
            setCursor(cur);
            return true;   // 边缘区域不传给子控件
        }
        unsetCursor();
    }
    return QWidget::eventFilter(obj, event);
}

void SerialTool::enableDwmEffects() {
#ifdef Q_OS_WIN
    // 1) 系统阴影：扩展 DWM frame 到客户区（经典 Win10 做法）
    const HWND hwnd = HWND(winId());
    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    // 2) Win11 22H2+：系统圆角（DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2）
    const int corner = 2;
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));
#endif
}

void SerialTool::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // 窗口首次显示后启用 DWM 系统阴影 + 圆角
    enableDwmEffects();
}

void SerialTool::paintEvent(QPaintEvent*) {
    // 画窗口不透明背景（主题色 win_bg），系统圆角负责裁剪圆角外的部分
    QPainter p(this);
    p.fillRect(rect(), QColor(m_winBg));
}

void SerialTool::closeEvent(QCloseEvent* e) {
    saveParams(true);
    closePort();
    if (m_settingsWin) {
        m_settingsWin->close();
        m_settingsWin->deleteLater();
        m_settingsWin = nullptr;
    }
    if (m_downloadThread && m_downloadThread->isRunning()) {
        m_downloadThread->cancel();
        m_downloadThread->wait(3000);
    }
    if (m_updateChecker) {
        m_updateChecker->deleteLater();
        m_updateChecker = nullptr;
    }
    e->accept();
}

// ================= UI 构建 =================

void SerialTool::buildUi() {
    // 无边框 + 透明背景：阴影和圆角由 Windows DWM 系统绘制
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, true);
    QString ver = QString::fromUtf8(APP_VERSION);
    while (ver.startsWith(QLatin1Char('v')) || ver.startsWith(QLatin1Char('V')))
        ver.remove(0, 1);
    setWindowTitle(QStringLiteral("%1  v%2").arg(sjj::APP_TITLE, ver));
    resize(900, 560);
    setMinimumSize(900, 560);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // 自绘标题栏
    m_titleBar = new TitleBar(this);
    outer->addWidget(m_titleBar);
    connect(m_titleBar, &TitleBar::versionClicked, this, &SerialTool::checkUpdateManual);
    connect(m_titleBar, &TitleBar::themeClicked, this, &SerialTool::toggleTheme);

    // 内容容器
    auto* content = new QWidget(this);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* main = new QVBoxLayout(content);
    main->setContentsMargins(6, 4, 6, 4);
    main->setSpacing(4);
    outer->addWidget(content, 1);

    // ===== 上半部分：接收区 + 命令面板 =====
    auto* upper = new QWidget(content);
    auto* upperLay = new QVBoxLayout(upper);
    upperLay->setContentsMargins(0, 0, 0, 0);
    upperLay->setSpacing(4);

    auto* upperRow = new QHBoxLayout;
    upperRow->setSpacing(4);
    upperLay->addLayout(upperRow, 1);

    // 左侧：接收区
    auto* recvBox = new QGroupBox(QString(), upper);
    auto* recvLay = new QVBoxLayout(recvBox);
    recvLay->setContentsMargins(8, 6, 8, 6);
    recvLay->setSpacing(2);
    m_txtRecv = new QTextEdit(recvBox);
    m_txtRecv->setReadOnly(true);
    QFont mono(QStringLiteral("Consolas"), 10);
    m_txtRecv->setFont(mono);
    m_txtRecv->setLineWrapMode(QTextEdit::WidgetWidth);
    recvLay->addWidget(m_txtRecv, 1);
    upperRow->addWidget(recvBox, 1);

    // 右侧：历史/快捷命令（默认隐藏，固定宽 420）
    m_msTabs = new QTabWidget(upper);
    m_msTabs->setFixedWidth(420);
    buildHistoryTab();
    buildMsTab();
    m_msTabs->hide();
    upperRow->addWidget(m_msTabs);

    // 接收区按钮行
    auto* rt = new QHBoxLayout;
    rt->setSpacing(4);
    auto* btn = new QPushButton(QStringLiteral("清除窗口"), upper);
    connect(btn, &QPushButton::clicked, this, &SerialTool::clearRecv);
    rt->addWidget(btn);
    m_chkShowHex = new ThemeCheckBox(QStringLiteral("HEX显示"), upper);
    rt->addWidget(m_chkShowHex);
    connect(m_chkShowHex, &QCheckBox::stateChanged, this, &SerialTool::refreshView);
    m_chkShowTs = new ThemeCheckBox(QStringLiteral("时间戳"), upper);
    m_chkShowTs->setToolTip(QStringLiteral("显示时间戳和分包（多行数据每行加前缀）"));
    m_chkShowTs->setChecked(true);
    rt->addWidget(m_chkShowTs);
    connect(m_chkShowTs, &QCheckBox::stateChanged, this, &SerialTool::refreshView);
    m_chkPause = new ThemeCheckBox(QStringLiteral("暂停刷新"), upper);
    rt->addWidget(m_chkPause);
    rt->addWidget(new QLabel(QStringLiteral("编码:"), upper));
    m_cmbEncoding = new StyledComboBox(upper);
    m_cmbEncoding->addItems(sjj::ENCODINGS);
    m_cmbEncoding->setEditable(false);
    m_cmbEncoding->setFixedWidth(90);
    connect(m_cmbEncoding, &QComboBox::currentTextChanged, this, &SerialTool::refreshView);
    rt->addWidget(m_cmbEncoding);
    rt->addWidget(new QLabel(QStringLiteral("搜索:"), upper));
    m_entrySearch = new QLineEdit(upper);
    m_entrySearch->setMaximumWidth(180);
    connect(m_entrySearch, &QLineEdit::textChanged, this, &SerialTool::onSearchChange);
    rt->addWidget(m_entrySearch);
    m_chkFilter = new ThemeCheckBox(QStringLiteral("筛选"), upper);
    connect(m_chkFilter, &QCheckBox::stateChanged, this, &SerialTool::refreshView);
    rt->addWidget(m_chkFilter);
    btn = new QPushButton(QStringLiteral("保存数据"), upper);
    connect(btn, &QPushButton::clicked, this, &SerialTool::saveRecv);
    rt->addWidget(btn);
    rt->addStretch(1);
    upperLay->addLayout(rt);

    main->addWidget(upper, 1);

    // 滚动跟随：滚轮上翻/拖动滚动条上移 → 自动勾选暂停刷新；滚回底部自动取消
    connect(m_txtRecv->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &SerialTool::onRecvScroll);

    // ===== 下半部分：串口设置块（左） + 发送区（右） =====
    auto* lower = new QHBoxLayout;
    lower->setSpacing(6);
    main->addLayout(lower);

    // 左侧：串口设置块
    auto* sbBox = new QGroupBox(QString(), content);
    sbBox->setMaximumWidth(290);
    auto* sb = new QVBoxLayout(sbBox);
    sb->setContentsMargins(6, 6, 6, 6);
    sb->setSpacing(2);

    auto* portRow = new QHBoxLayout;
    portRow->setSpacing(2);
    portRow->addWidget(new QLabel(QStringLiteral("端口号:"), sbBox));
    m_cmbPort = new PortComboBox(sbBox);
    m_cmbPort->setEditable(false);
    m_cmbPort->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    portRow->addWidget(m_cmbPort, 1);
    btn = new QPushButton(QStringLiteral("刷新"), sbBox);
    btn->setFixedWidth(40);
    connect(btn, &QPushButton::clicked, this, &SerialTool::refreshPorts);
    portRow->addWidget(btn);
    sb->addLayout(portRow);

    auto* baudRow = new QHBoxLayout;
    baudRow->setSpacing(2);
    auto* lblBaud = new QLabel(QStringLiteral("波特率:"), sbBox);
    lblBaud->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    baudRow->addWidget(lblBaud);
    m_cmbBaud = new StyledComboBox(sbBox);
    m_cmbBaud->addItems(sjj::DEFAULT_BAUDRATES);
    m_cmbBaud->setCurrentText(QStringLiteral("115200"));
    m_cmbBaud->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_cmbBaud->setMinimumWidth(60);
    baudRow->addWidget(m_cmbBaud, 1);
    connect(m_cmbPort, &QComboBox::currentIndexChanged, this, &SerialTool::onPortChanged);
    connect(m_cmbBaud, &QComboBox::currentIndexChanged, this, &SerialTool::onBaudChanged);
    btn = new QPushButton(QStringLiteral("更多设置"), sbBox);
    btn->setFixedWidth(70);
    connect(btn, &QPushButton::clicked, this, &SerialTool::showSettingsDialog);
    baudRow->addWidget(btn);
    m_btnOpen = new QPushButton(QStringLiteral("打开串口"), sbBox);
    m_btnOpen->setFixedWidth(70);
    connect(m_btnOpen, &QPushButton::clicked, this, &SerialTool::togglePort);
    baudRow->addWidget(m_btnOpen);
    sb->addLayout(baudRow);

    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(2);
    btn = new QPushButton(QStringLiteral("历史记录"), sbBox);
    connect(btn, &QPushButton::clicked, this, [this] { toggleWindow(0); });
    btnRow->addWidget(btn, 1);
    btn = new QPushButton(QStringLiteral("快捷命令"), sbBox);
    connect(btn, &QPushButton::clicked, this, [this] { toggleWindow(1); });
    btnRow->addWidget(btn, 1);
    sb->addLayout(btnRow);
    sb->addStretch(1);
    lower->addWidget(sbBox);

    // 右侧：发送区
    auto* sendBox = new QGroupBox(QString(), content);
    auto* sendLay = new QVBoxLayout(sendBox);
    sendLay->setContentsMargins(8, 6, 8, 6);
    sendLay->setSpacing(3);

    auto* fb = new QHBoxLayout;
    fb->setSpacing(4);
    fb->setAlignment(Qt::AlignVCenter);
    m_btnSelectFile = new QPushButton(QStringLiteral("选择文件"), sendBox);
    m_btnSelectFile->setToolTip(QStringLiteral("点击选择要发送的文件"));
    connect(m_btnSelectFile, &QPushButton::clicked, this, &SerialTool::pickFile);
    fb->addWidget(m_btnSelectFile, 1);
    m_btnSendFile = new QPushButton(QStringLiteral("发送文件"), sendBox);
    connect(m_btnSendFile, &QPushButton::clicked, this, &SerialTool::sendFile);
    fb->addWidget(m_btnSendFile);

    // 加校验范围 + 算法（整组包进 cs_group，≠None 时高亮）
    m_csGroup = new QFrame(sendBox);
    m_csGroup->setObjectName(QStringLiteral("cs_group"));
    m_csGroup->setFrameShape(QFrame::NoFrame);
    auto* csLay = new QHBoxLayout(m_csGroup);
    csLay->setContentsMargins(4, 1, 4, 1);
    csLay->setSpacing(3);
    csLay->addWidget(new QLabel(QStringLiteral("第"), m_csGroup));
    m_entryCsStart = new QLineEdit(QStringLiteral("1"), m_csGroup);
    m_entryCsStart->setFixedWidth(40);
    m_entryCsStart->setAlignment(Qt::AlignCenter);
    csLay->addWidget(m_entryCsStart);
    csLay->addWidget(new QLabel(QStringLiteral("字节至末尾"), m_csGroup));
    m_entryCsEnd = new QLineEdit(QStringLiteral("0"), m_csGroup);
    m_entryCsEnd->setFixedWidth(40);
    m_entryCsEnd->setAlignment(Qt::AlignCenter);
    csLay->addWidget(m_entryCsEnd);
    csLay->addWidget(new QLabel(QStringLiteral("加校验"), m_csGroup));
    m_cmbChecksum = new StyledComboBox(m_csGroup);
    m_cmbChecksum->addItems(sjj::CHECKSUMS);
    m_cmbChecksum->setEditable(false);
    m_cmbChecksum->setFixedWidth(110);
    csLay->addWidget(m_cmbChecksum);
    m_lblCsResult = new QLabel(QString(), m_csGroup);
    csLay->addWidget(m_lblCsResult);
    fb->addWidget(m_csGroup);
    connect(m_cmbChecksum, &QComboBox::currentIndexChanged,
            this, &SerialTool::updateChecksumHighlight);
    updateChecksumHighlight();
    fb->addStretch(1);
    sendLay->addLayout(fb);

    auto* sbar = new QHBoxLayout;
    sbar->setSpacing(4);
    auto* btnSend = new QPushButton(QStringLiteral("发送"), sendBox);
    connect(btnSend, &QPushButton::clicked, this, &SerialTool::send);
    sbar->addWidget(btnSend);
    btn = new QPushButton(QStringLiteral("清空发送"), sendBox);
    connect(btn, &QPushButton::clicked, this, &SerialTool::clearSend);
    sbar->addWidget(btn);
    m_chkSendHex = new ThemeCheckBox(QStringLiteral("HEX发送"), sendBox);
    connect(m_chkSendHex, &QCheckBox::stateChanged, this, &SerialTool::onSendHexToggle);
    sbar->addWidget(m_chkSendHex);
    m_chkAddCrlf = new ThemeCheckBox(QStringLiteral("加回车换行"), sendBox);
    sbar->addWidget(m_chkAddCrlf);
    m_chkTimer = new ThemeCheckBox(QStringLiteral("定时发送:"), sendBox);
    connect(m_chkTimer, &QCheckBox::stateChanged, this, &SerialTool::onTimerToggle);
    sbar->addWidget(m_chkTimer);
    m_entryInterval = new QLineEdit(QStringLiteral("1000"), sendBox);
    m_entryInterval->setFixedWidth(56);
    sbar->addWidget(m_entryInterval);
    sbar->addWidget(new QLabel(QStringLiteral("ms/次"), sendBox));
    sbar->addStretch(1);
    sendLay->addLayout(sbar);

    m_txtSend = new QTextEdit(sendBox);
    m_txtSend->setObjectName(QStringLiteral("txt_send"));   // 让全局 QSS 给发送框加边框
    m_txtSend->setFont(mono);
    m_txtSend->setFixedHeight(90);
    connect(m_txtSend, &QTextEdit::textChanged, this, &SerialTool::updateCsPreview);
    sendLay->addWidget(m_txtSend, 1);
    lower->addWidget(sendBox, 1);

    // ===== 状态栏 =====
    m_statusBar = new QWidget(content);
    m_statusBar->setObjectName(QStringLiteral("statusBar"));
    m_statusBar->setAttribute(Qt::WA_StyledBackground, true);
    m_sbLay = new QHBoxLayout(m_statusBar);
    m_sbLay->setContentsMargins(6, 1, 6, 1);
    m_sbLay->setSpacing(8);
    m_lblStatus = new QLabel(QStringLiteral("未连接"), m_statusBar);
    m_sbLay->addWidget(m_lblStatus, 1);

    m_progressContainer = new QWidget(m_statusBar);
    auto* pcLay = new QHBoxLayout(m_progressContainer);
    pcLay->setContentsMargins(0, 0, 0, 0);
    pcLay->setSpacing(6);
    // 进度文字显示在进度条上（内置文字）。
    // 对比度由 QSS 保证：填充深蓝 #1E66F5、未填充深色 #24273A、文字白色——
    // 白字在深蓝/深灰两种背景下都清晰（两套主题一致）
    m_progressBar = new QProgressBar(m_progressContainer);
    m_progressBar->setFixedWidth(220);
    m_progressBar->setFixedHeight(16);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFormat(QStringLiteral("%v%"));
    pcLay->addWidget(m_progressBar);
    m_lblSpeed = new QLabel(QString(), m_progressContainer);
    m_lblSpeed->setMinimumWidth(70);
    m_lblSpeed->setStyleSheet(QStringLiteral("color:transparent;"));
    pcLay->addWidget(m_lblSpeed);
    m_progressContainer->hide();
    m_sbLay->addWidget(m_progressContainer, 0);

    m_btnCancelDl = new QToolButton(m_statusBar);
    m_btnCancelDl->setText(QStringLiteral("\u2715"));
    m_btnCancelDl->setToolTip(QStringLiteral("取消下载"));
    m_btnCancelDl->setAutoRaise(true);
    m_btnCancelDl->hide();
    m_sbLay->addWidget(m_btnCancelDl);

    m_btnRestart = new QToolButton(m_statusBar);
    m_btnRestart->setText(QStringLiteral("立即重启"));
    m_btnRestart->setToolTip(QStringLiteral("用下载好的新版替换当前程序并重启"));
    m_btnRestart->hide();
    m_sbLay->addWidget(m_btnRestart);

    m_lblSr = new QLabel(QStringLiteral("S:0  R:0"), m_statusBar);
    m_sbLay->addWidget(m_lblSr);
    m_lblHandshake = new QLabel(QStringLiteral("CTS=0 DSR=0 RLSD=0"), m_statusBar);
    m_sbLay->addWidget(m_lblHandshake);
    m_lblApp = new QLabel(m_statusBar);
    m_lblApp->setOpenExternalLinks(false);
    m_lblApp->setCursor(Qt::PointingHandCursor);
    connect(m_lblApp, &QLabel::linkActivated, this, &SerialTool::openGithub);
    m_sbLay->addWidget(m_lblApp);
    outer->addWidget(m_statusBar);

    // 定时发送
    m_timerSend = new QTimer(this);
    connect(m_timerSend, &QTimer::timeout, this, &SerialTool::timerSendTick);
    // 多字符串循环发送
    m_msLoopTimer = new QTimer(this);
    connect(m_msLoopTimer, &QTimer::timeout, this, &SerialTool::msLoopTick);
}

void SerialTool::buildHistoryTab() {
    auto* w = new QWidget(m_msTabs);
    auto* lay = new QVBoxLayout(w);
    auto* bar = new QHBoxLayout;
    auto* btn = new QPushButton(QStringLiteral("删除"), w);
    connect(btn, &QPushButton::clicked, this, &SerialTool::histDelete);
    bar->addWidget(btn);
    btn = new QPushButton(QStringLiteral("清空"), w);
    connect(btn, &QPushButton::clicked, this, &SerialTool::histClear);
    bar->addWidget(btn);
    bar->addStretch(1);
    lay->addLayout(bar);
    m_histList = new QListWidget(w);
    connect(m_histList, &QListWidget::itemDoubleClicked, this, &SerialTool::histLoad);
    lay->addWidget(m_histList, 1);
    m_msTabs->addTab(w, QStringLiteral("历史记录"));
}

void SerialTool::buildMsTab() {
    auto* w = new QWidget(m_msTabs);
    auto* lay = new QVBoxLayout(w);
    auto* bar = new QHBoxLayout;
    auto* btn = new QPushButton(QStringLiteral("\uFF0B 新建"), w);
    connect(btn, &QPushButton::clicked, this, &SerialTool::newMsEntry);
    bar->addWidget(btn);
    auto* line = new QFrame(w);
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Sunken);
    bar->addWidget(line);
    m_chkMsLoop = new ThemeCheckBox(QStringLiteral("循环发送"), w);
    bar->addWidget(m_chkMsLoop);
    bar->addWidget(new QLabel(QStringLiteral("间隔ms:"), w));
    m_entryMsInterval = new QLineEdit(QStringLiteral("1000"), w);
    m_entryMsInterval->setFixedWidth(56);
    bar->addWidget(m_entryMsInterval);
    m_btnStartMs = new QPushButton(QStringLiteral("\u25B6 开始"), w);
    connect(m_btnStartMs, &QPushButton::clicked, this, &SerialTool::startMsLoop);
    bar->addWidget(m_btnStartMs);
    m_btnStopMs = new QPushButton(QStringLiteral("\u25A0 停止"), w);
    m_btnStopMs->setEnabled(false);
    connect(m_btnStopMs, &QPushButton::clicked, this, &SerialTool::stopMsLoop);
    bar->addWidget(m_btnStopMs);
    bar->addStretch(1);
    lay->addLayout(bar);

    m_lblMsHint = new QLabel(
        QStringLiteral("HEX  发送内容（可编辑）            按钮（右键改名 / 点发）"), w);
    lay->addWidget(m_lblMsHint);

    m_msScroll = new QScrollArea(w);
    m_msScroll->setWidgetResizable(true);
    m_msInner = new QWidget;
    m_msRows = new QVBoxLayout(m_msInner);
    m_msRows->setContentsMargins(2, 2, 2, 2);
    m_msRows->setSpacing(2);
    m_msRows->addStretch(1);
    m_msScroll->setWidget(m_msInner);
    lay->addWidget(m_msScroll, 1);
    m_msTabs->addTab(w, QStringLiteral("快捷命令"));
}

// ================= 主题 =================

void SerialTool::applyTheme(const QString& themeName) {
    m_theme = (themeName == QStringLiteral("dark")) ? QStringLiteral("dark")
                                                    : QStringLiteral("light");
    m_themeC = sjj::theme(m_theme);
    m_winBg = m_themeC.value(QStringLiteral("win_bg"));
    if (QApplication* app = qApp)
        app->setStyleSheet(sjj::buildQss(m_themeC));
    m_titleBar->applyTheme(m_themeC, m_theme);
    m_lblMsHint->setStyleSheet(
        QStringLiteral("color:%1;").arg(m_themeC.value(QStringLiteral("text_secondary"))));
    m_lblCsResult->setStyleSheet(
        QStringLiteral("color:%1;font-weight:bold;")
            .arg(m_themeC.value(QStringLiteral("err_color"))));
    m_lblApp->setText(
        QStringLiteral("<a href=\"github\" style=\"color:%1;\">SuperCOM</a>")
            .arg(m_themeC.value(QStringLiteral("link_color"))));
    const auto checkboxes = findChildren<ThemeCheckBox*>();
    for (auto* cb : checkboxes)
        cb->setThemeColors(m_themeC);
    const auto combos = findChildren<StyledComboBox*>();
    for (auto* combo : combos)
        combo->setArrowColor(QColor(m_themeC.value(QStringLiteral("text_primary"))));
    updateChecksumHighlight();
    refreshStatus();
    refreshView();   // 切主题后强制重建接收区（按新主题色重新渲染已有文字）
    update();   // 立即触发 paintEvent 重画窗口背景
}

void SerialTool::toggleTheme() {
    const QString newTheme = (m_theme == QStringLiteral("light"))
        ? QStringLiteral("dark") : QStringLiteral("light");
    applyTheme(newTheme);
    saveParams(true);
}

QString SerialTool::themeTextColor() const {
    return m_themeC.value(QStringLiteral("text_primary"));
}

// ================= 更新检查 / 下载 =================

void SerialTool::checkUpdateAuto() {
    if (m_updateChecker)
        return;
    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::resultReady,
            this, &SerialTool::onUpdateCheckResult);
    m_updateChecker->check(false);
}

void SerialTool::checkUpdateManual() {
    if (m_updateChecker)
        return;
    // 用主题 accent 色作临时提示色（两套主题下都清晰）
    m_lblStatus->setStyleSheet(
        QStringLiteral("color:%1;").arg(m_themeC.value(QStringLiteral("accent"))));
    m_lblStatus->setText(QStringLiteral("正在检查更新..."));
    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::resultReady,
            this, &SerialTool::onUpdateCheckResult);
    m_updateChecker->check(true);
}

void SerialTool::onUpdateCheckResult(const QVariantMap& info, bool manual) {
    if (m_updateChecker) {
        m_updateChecker->deleteLater();
        m_updateChecker = nullptr;
    }
    if (info.isEmpty()) {
        // 网络失败：仅手动检查时提示（自动检查静默）
        if (manual) {
            m_lblStatus->setText(QStringLiteral("检查更新失败（网络不可用）"));
            QMessageBox::information(this, QStringLiteral("检查更新"),
                                     QStringLiteral("无法连接到 GitHub，请检查网络后重试。"));
        }
        refreshStatus();
        return;
    }
    const QString latest = info.value(QStringLiteral("latest")).toString();
    QString ver = QString::fromUtf8(APP_VERSION);
    while (ver.startsWith(QLatin1Char('v')) || ver.startsWith(QLatin1Char('V')))
        ver.remove(0, 1);
    if (!sjj::versionIsNewer(latest, QString::fromUtf8(APP_VERSION))) {
        if (manual) {
            m_lblStatus->setText(QStringLiteral("已是最新版本 v%1").arg(ver));
            QMessageBox::information(this, QStringLiteral("检查更新"),
                                     QStringLiteral("当前已是最新版本 v%1。").arg(ver));
            refreshStatus();
        }
        return;
    }
    // 自动检查：被忽略过的版本不再提示
    if (!manual && m_ignoreUpdateVersion == latest)
        return;
    showUpdateDialog(info);
    refreshStatus();
}

void SerialTool::showUpdateDialog(const QVariantMap& info) {
    const QString latest = info.value(QStringLiteral("latest")).toString();
    QString ver = QString::fromUtf8(APP_VERSION);
    while (ver.startsWith(QLatin1Char('v')) || ver.startsWith(QLatin1Char('V')))
        ver.remove(0, 1);
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("发现新版本"));
    box.setIcon(QMessageBox::Information);
    box.setText(QStringLiteral("发现新版本 v%1\n当前版本 v%2").arg(latest, ver));
    const QString body = info.value(QStringLiteral("body")).toString().trimmed();
    if (!body.isEmpty()) {
        QStringList lines = body.split(QLatin1Char('\n'));
        while (lines.size() > 6)
            lines.removeLast();
        box.setInformativeText(QStringLiteral("更新说明：\n") + lines.join(QLatin1Char('\n')));
    }
    auto* btnUpdate = box.addButton(QStringLiteral("立即更新"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("下次再说"), QMessageBox::RejectRole);
    auto* btnIgnore = box.addButton(QStringLiteral("忽略本次更新"), QMessageBox::DestructiveRole);
    box.setDefaultButton(btnUpdate);
    box.exec();
    auto* clicked = box.clickedButton();
    if (clicked == btnUpdate) {
        startUpdateDownload(info);
    } else if (clicked == btnIgnore) {
        m_ignoreUpdateVersion = latest;
        saveParams(true);
    }
}

void SerialTool::startUpdateDownload(const QVariantMap& info) {
    const QString exeUrl = info.value(QStringLiteral("exe_url")).toString();
    if (exeUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(info.value(QStringLiteral("html_url")).toString()));
        return;
    }
    const auto ret = QMessageBox::question(
        this, QStringLiteral("自动更新"),
        QStringLiteral("将下载最新版 v%1 并自动替换当前程序（下载完成后可选择是否立即重启）。\n是否继续？")
            .arg(info.value(QStringLiteral("latest")).toString()));
    if (ret != QMessageBox::Yes)
        return;
    if (m_downloadThread && m_downloadThread->isRunning())
        return;
    // 状态栏进度条 + 取消按钮（下载时显示，"立即重启"隐藏）
    m_sbLay->setStretch(m_sbLay->indexOf(m_lblStatus), 0);
    m_sbLay->setStretch(m_sbLay->indexOf(m_progressContainer), 1);
    m_progressBar->setValue(0);
    m_progressBar->setFormat(QStringLiteral("%v%"));
    m_lblSpeed->setText(QString());
    m_lblSpeed->setStyleSheet(QStringLiteral("color:transparent;"));
    m_progressContainer->show();
    m_btnCancelDl->show();
    m_btnRestart->hide();
    m_pendingUpdateVersion = info.value(QStringLiteral("latest")).toString();
    m_downloadThread = new ExeDownloader(exeUrl, 5, this);
    connect(m_downloadThread, &ExeDownloader::progress,
            this, &SerialTool::onDownloadProgress);
    connect(m_downloadThread, &ExeDownloader::finishedOk,
            this, &SerialTool::onUpdateDownloaded);
    connect(m_downloadThread, &ExeDownloader::failed,
            this, &SerialTool::onUpdateDownloadFailed);
    // 取消按钮（先断开旧的，避免重复连接）
    disconnect(m_btnCancelDl, &QToolButton::clicked, nullptr, nullptr);
    connect(m_btnCancelDl, &QToolButton::clicked, this, [this] {
        if (m_downloadThread)
            m_downloadThread->cancel();
    });
    m_downloadThread->start();
}

void SerialTool::onDownloadProgress(qint64 done, qint64 total, int speedKbps) {
    if (total > 0) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(int(qMin<qint64>(100, done * 100 / total)));
        m_progressBar->setFormat(
            QStringLiteral("%v%  (%1/%2 KB)").arg(done / 1024).arg(total / 1024));
    } else {
        m_progressBar->setRange(0, 0);   // busy 模式
        m_progressBar->setValue(0);
        m_progressBar->setFormat(QStringLiteral("已下载 %1 KB").arg(done / 1024));
    }
    if (speedKbps > 0) {
        m_lblSpeed->setText(QStringLiteral("%1 KB/s").arg(speedKbps));
        m_lblSpeed->setStyleSheet(QStringLiteral("color:%1;").arg(themeTextColor()));
    }
}

void SerialTool::closeDownloadUi() {
    m_progressContainer->hide();
    m_btnCancelDl->hide();
    m_lblSpeed->setText(QString());
    m_lblSpeed->setStyleSheet(QStringLiteral("color:transparent;"));
    m_sbLay->setStretch(m_sbLay->indexOf(m_lblStatus), 1);
    m_sbLay->setStretch(m_sbLay->indexOf(m_progressContainer), 0);
    disconnect(m_btnCancelDl, &QToolButton::clicked, nullptr, nullptr);
}

void SerialTool::onUpdateDownloaded(const QString& tmp) {
    closeDownloadUi();
    refreshStatus();
    m_updateTmp = tmp;
    // 源码运行（exe 目录下没有 SuperCOM.exe）：无自动替换，提示文件位置
    const QString exeDir = QCoreApplication::applicationDirPath();
    const bool sourceMode = !QFileInfo::exists(exeDir + QStringLiteral("/SuperCOM.exe"));
    if (sourceMode) {
        m_btnRestart->hide();
        QMessageBox::information(
            this, QStringLiteral("更新下载完成"),
            QStringLiteral("新版本 v%1 已下载完成：\n%2\n\n当前为源码运行，无法自动替换 exe，已为你打开所在文件夹。")
                .arg(m_pendingUpdateVersion, tmp));
        QProcess::startDetached(QStringLiteral("explorer"),
                                {QStringLiteral("/select,"), tmp});
        return;
    }
    disconnect(m_btnRestart, &QToolButton::clicked, nullptr, nullptr);
    connect(m_btnRestart, &QToolButton::clicked, this, &SerialTool::performUpdateRestart);
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("更新下载完成"));
    box.setIcon(QMessageBox::Information);
    box.setText(QStringLiteral("新版本 v%1 已下载完成。\n是否立即重启以完成更新？")
                    .arg(m_pendingUpdateVersion));
    auto* btnNow = box.addButton(QStringLiteral("立即重启"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("稍后"), QMessageBox::RejectRole);
    box.setDefaultButton(btnNow);
    box.exec();
    if (box.clickedButton() == btnNow) {
        performUpdateRestart();
    } else {
        // 状态栏显示"立即重启 v..."按钮，稍后手动触发
        m_btnRestart->setText(QStringLiteral("立即重启 v%1").arg(m_pendingUpdateVersion));
        m_btnRestart->show();
    }
}

void SerialTool::onUpdateDownloadFailed(const QString& err) {
    closeDownloadUi();
    refreshStatus();
    QMessageBox::warning(this, QStringLiteral("更新下载失败"),
                         QStringLiteral("下载更新失败：%1\n请稍后重试或前往 GitHub 手动下载。").arg(err));
}

void SerialTool::performUpdateRestart() {
    if (m_updateTmp.isEmpty() || !QFileInfo::exists(m_updateTmp)) {
        QMessageBox::critical(this, QStringLiteral("更新失败"),
                              QStringLiteral("下载文件已丢失：%1\n请重新检查更新。").arg(m_updateTmp));
        m_btnRestart->hide();
        return;
    }
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString target = exeDir + QStringLiteral("/SuperCOM.exe");
    const QString bat = exeDir + QStringLiteral("/_supercom_update.bat");
    // cmd/start 对正斜杠路径兼容性差（explorer.exe 传正斜杠会失败），统一转反斜杠
    const QString tmp = QDir::toNativeSeparators(m_updateTmp);
    const QString tgt = QDir::toNativeSeparators(target);
    // bat 用 UTF-8 with BOM 写入，cmd 需 chcp 65001 切到 UTF-8 代码页
    // （默认 GBK 代码页会导致含中文的 exe 路径乱码，move 找不到文件）
    // 启动新 exe 用 cmd 内建 start（explorer.exe 启动 exe 行为不可靠，实测会失败）
    const QString script = QStringLiteral(
        "@echo off\r\n"
        "chcp 65001 >nul\r\n"
        ":wait\r\n"
        "tasklist /fi \"imagename eq SuperCOM.exe\" 2>nul "
        "| find /i \"SuperCOM.exe\" >nul\r\n"
        "if errorlevel 1 goto replace\r\n"
        "timeout /t 1 /nobreak >nul\r\n"
        "goto wait\r\n"
        ":replace\r\n"
        "move /y \"%1\" \"%2\" >nul 2>&1\r\n"
        "start \"\" \"%2\"\r\n"
        "del \"%~f0\"\r\n")
        // QString::arg 没有多参重载——必须链式调用
        .arg(tmp).arg(tgt);
    QFile f(bat);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, QStringLiteral("更新失败"),
                              QStringLiteral("无法写入更新脚本：%1").arg(f.errorString()));
        return;
    }
    f.write("\xEF\xBB\xBF");
    f.write(script.toUtf8());
    f.close();
    // CREATE_NO_WINDOW 静默启动批处理，不弹黑窗
    if (!QProcess::startDetached(QStringLiteral("cmd"), {QStringLiteral("/c"), bat})) {
        QMessageBox::critical(this, QStringLiteral("更新失败"),
                              QStringLiteral("无法启动更新程序。\n新版本已下载到：%1\n请手动替换 %2")
                                  .arg(m_updateTmp).arg(target));
        return;
    }
    m_btnRestart->hide();
    m_lblStatus->setText(QStringLiteral("更新完成，正在重启..."));
    QTimer::singleShot(800, this, &SerialTool::close);
}

// ================= 端口 / 串口 =================

void SerialTool::refreshPorts() {
    m_suppressApply = true;   // 刷新列表期间不触发"自动重开"
    struct PortInfo {
        QString device;
        QString desc;
        int comNum = -1;
    };
    QVector<PortInfo> ports;
    static const QRegularExpression re(QStringLiteral("^COM(\\d+)$"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto infos = QSerialPortInfo::availablePorts();
    for (const auto& info : infos) {
        PortInfo p;
        p.device = info.portName();
        p.desc = info.description().trimmed();
        const auto m = re.match(p.device);
        if (m.hasMatch())
            p.comNum = m.captured(1).toInt();
        ports.append(p);
    }
    std::sort(ports.begin(), ports.end(), [](const PortInfo& a, const PortInfo& b) {
        if (a.comNum >= 0 && b.comNum >= 0)
            return a.comNum < b.comNum;
        if (a.comNum >= 0)
            return true;
        if (b.comNum >= 0)
            return false;
        return a.device < b.device;
    });
    m_portMap.clear();
    QStringList items;
    for (const auto& p : ports) {
        // 显示 "COM7  名称"；去掉描述末尾重复附带的 (COMx)
        QString desc = p.desc;
        desc.remove(QRegularExpression(QStringLiteral("\\s*\\(?COM\\d+\\)?\\s*$")));
        QString label = p.device;
        if (!desc.isEmpty())
            label += QStringLiteral("  ") + desc;
        items.append(label);
        m_portMap[label] = p.device;
    }
    if (items.isEmpty()) {
        items.append(QStringLiteral("(无可用端口)"));
        m_portMap[items.first()] = QString();
    }
    const QString cur = m_cmbPort->currentText();
    m_cmbPort->clear();
    m_cmbPort->addItems(items);
    if (items.contains(cur))
        m_cmbPort->setCurrentText(cur);
    m_suppressApply = false;
}

QString SerialTool::getSelectedPort() const {
    const QString label = m_cmbPort->currentText();
    if (m_portMap.contains(label))
        return m_portMap.value(label);
    const QStringList parts = label.split(QLatin1Char(' '));
    return parts.isEmpty() ? QString() : parts.first();
}

void SerialTool::showSettingsDialog() {
    if (m_settingsWin) {
        m_settingsWin->showNormal();
        m_settingsWin->raise();
        m_settingsWin->activateWindow();
        return;
    }
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("更多串口设置"));
    dlg->setModal(false);
    m_settingsWin = dlg;
    connect(dlg, &QDialog::finished, this, [this] { m_settingsWin = nullptr; });
    auto* form = new QFormLayout(dlg);

    auto* cmbBaud = new StyledComboBox(dlg);
    cmbBaud->addItems(sjj::DEFAULT_BAUDRATES);
    cmbBaud->setCurrentText(m_cmbBaud->currentText());
    form->addRow(QStringLiteral("波特率:"), cmbBaud);

    auto* cmbDatabits = new StyledComboBox(dlg);
    cmbDatabits->addItems(sjj::DEFAULT_DATABITS);
    cmbDatabits->setCurrentText(m_databits);
    form->addRow(QStringLiteral("数据位:"), cmbDatabits);

    auto* cmbStopbits = new StyledComboBox(dlg);
    cmbStopbits->addItems(sjj::DEFAULT_STOPBITS);
    cmbStopbits->setCurrentText(m_stopbits);
    form->addRow(QStringLiteral("停止位:"), cmbStopbits);

    auto* cmbParity = new StyledComboBox(dlg);
    cmbParity->addItems(sjj::DEFAULT_PARITY);
    cmbParity->setCurrentText(m_parity);
    form->addRow(QStringLiteral("校验位:"), cmbParity);

    auto* cmbFlow = new StyledComboBox(dlg);
    cmbFlow->addItems(sjj::DEFAULT_FLOW);
    cmbFlow->setCurrentText(m_flow);
    form->addRow(QStringLiteral("流控:"), cmbFlow);

    auto* entryTimeout = new QLineEdit(m_readTimeout, dlg);
    form->addRow(QStringLiteral("读超时(ms):"), entryTimeout);

    auto* btns = new QHBoxLayout;
    btns->addStretch(1);
    auto* ok = new QPushButton(QStringLiteral("确定"), dlg);
    connect(ok, &QPushButton::clicked, this,
            [this, dlg, cmbBaud, cmbDatabits, cmbStopbits, cmbParity, cmbFlow, entryTimeout] {
                applySettings(dlg, cmbBaud, cmbDatabits, cmbStopbits, cmbParity,
                              cmbFlow, entryTimeout);
            });
    btns->addWidget(ok);
    auto* cancel = new QPushButton(QStringLiteral("取消"), dlg);
    connect(cancel, &QPushButton::clicked, dlg, &QDialog::close);
    btns->addWidget(cancel);
    form->addRow(btns);
    dlg->resize(300, 260);
    dlg->show();
}

void SerialTool::applySettings(QDialog* dlg, QComboBox* cmbBaud, QComboBox* cmbDatabits,
                               QComboBox* cmbStopbits, QComboBox* cmbParity,
                               QComboBox* cmbFlow, QLineEdit* entryTimeout) {
    bool ok = false;
    const int t = entryTimeout->text().toInt(&ok);
    if (!ok || t < 1) {
        QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("读超时必须是正整数"));
        return;
    }
    m_readTimeout = QString::number(t);
    cmbBaud->currentText().toInt(&ok);
    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("波特率必须是数字"));
        return;
    }
    m_suppressApply = true;   // 批量赋值期间不触发"自动重开"
    m_cmbBaud->setCurrentText(cmbBaud->currentText());
    m_databits = cmbDatabits->currentText();
    m_stopbits = cmbStopbits->currentText();
    m_parity = cmbParity->currentText();
    m_flow = cmbFlow->currentText();
    m_suppressApply = false;
    refreshStatus();
    applySerialSettings();   // 串口已打开 → 立即用新参数重开
    dlg->close();
}

void SerialTool::onPortChanged(int) {
    if (m_suppressApply)
        return;
    applySerialSettings();
}

void SerialTool::onBaudChanged(int) {
    if (m_suppressApply)
        return;
    applySerialSettings();
}

void SerialTool::applySerialSettings() {
    // 串口参数被修改：若串口已打开则立即关闭并按新参数重开；未打开仅刷新状态栏
    if (!m_ser || !m_ser->isOpen()) {
        refreshStatus();
        return;
    }
    const QString port = getSelectedPort();
    if (port.isEmpty() || port == QStringLiteral("(无可用端口)")) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先选择有效的串口"));
        closePort();
        return;
    }
    closePort();
    openPort();
}

void SerialTool::togglePort() {
    if (m_ser && m_ser->isOpen())
        closePort();
    else
        openPort();
}

void SerialTool::openPort() {
    const QString port = getSelectedPort();
    if (port.isEmpty() || port == QStringLiteral("(无可用端口)")) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先选择串口"));
        return;
    }
    bool okBaud = false;
    const int baud = m_cmbBaud->currentText().toInt(&okBaud);
    if (!okBaud) {
        QMessageBox::critical(this, QStringLiteral("打开失败"), QStringLiteral("波特率必须是数字"));
        return;
    }
    auto* ser = new QSerialPort(port, this);
    ser->setBaudRate(baud);
    if (m_databits == QStringLiteral("5")) ser->setDataBits(QSerialPort::Data5);
    else if (m_databits == QStringLiteral("6")) ser->setDataBits(QSerialPort::Data6);
    else if (m_databits == QStringLiteral("7")) ser->setDataBits(QSerialPort::Data7);
    else ser->setDataBits(QSerialPort::Data8);
    if (m_parity == QStringLiteral("Even")) ser->setParity(QSerialPort::EvenParity);
    else if (m_parity == QStringLiteral("Odd")) ser->setParity(QSerialPort::OddParity);
    else if (m_parity == QStringLiteral("Mark")) ser->setParity(QSerialPort::MarkParity);
    else if (m_parity == QStringLiteral("Space")) ser->setParity(QSerialPort::SpaceParity);
    else ser->setParity(QSerialPort::NoParity);
    if (m_stopbits == QStringLiteral("1.5")) ser->setStopBits(QSerialPort::OneAndHalfStop);
    else if (m_stopbits == QStringLiteral("2")) ser->setStopBits(QSerialPort::TwoStop);
    else ser->setStopBits(QSerialPort::OneStop);
    if (m_flow == QStringLiteral("RTS/CTS")) ser->setFlowControl(QSerialPort::HardwareControl);
    else if (m_flow == QStringLiteral("XON/XOFF")) ser->setFlowControl(QSerialPort::SoftwareControl);
    else ser->setFlowControl(QSerialPort::NoFlowControl);

    if (!ser->open(QIODevice::ReadWrite)) {
        QMessageBox::critical(this, QStringLiteral("打开失败"), ser->errorString());
        delete ser;
        return;
    }
    // 默认不拉高 DTR/RTS，避免开发板复位/进入下载模式
    ser->setDataTerminalReady(false);
    ser->setRequestToSend(false);
    connect(ser, &QSerialPort::readyRead, this, &SerialTool::onSerialData);
    connect(ser, &QSerialPort::errorOccurred, this, &SerialTool::onSerialError);
    m_ser = ser;
    ++m_rxEpoch;
    m_btnOpen->setText(QStringLiteral("关闭串口"));
    refreshStatus();
    addRecord(QStringLiteral("sys"),
              QStringLiteral("串口已打开: %1 @ %2").arg(port).arg(baud));
}

void SerialTool::closePort() {
    if (m_timerSend && m_timerSend->isActive())
        m_timerSend->stop();
    if (m_ser) {
        m_ser->disconnect(this);
        m_ser->close();
        m_ser->deleteLater();
        m_ser = nullptr;
    }
    // 代际号递增：关闭瞬间使旧串口排队的信号全部失效
    ++m_rxEpoch;
    // 丢弃尚未刷新的接收/发送事件缓冲
    m_pendingEvents.clear();
    m_btnOpen->setText(QStringLiteral("打开串口"));
    refreshStatus();
    addRecord(QStringLiteral("sys"), QStringLiteral("串口已关闭"));
}

void SerialTool::onSerialData() {
    const QByteArray data = m_ser->readAll();
    m_rxBytes += quint64(data.size());
    // 时间戳在数据到达时刻生成（而非显示时刻）
    Record r;
    r.kind = QStringLiteral("rx");
    r.rawBytes = data;
    r.ts = sjj::nowTimestamp();
    m_pendingEvents.append(r);
    if (m_pendingEvents.size() > sjj::MAX_RECORDS)
        m_pendingEvents = m_pendingEvents.mid(m_pendingEvents.size() - sjj::MAX_RECORDS);
}

void SerialTool::onSerialError(QSerialPort::SerialPortError err) {
    if (err == QSerialPort::NoError)
        return;
    if (!m_ser || !m_ser->isOpen())
        return;
    addRecord(QStringLiteral("sys"),
              QStringLiteral("[读取异常] %1").arg(m_ser->errorString()));
    closePort();
}

void SerialTool::refreshStatus() {
    QString port = getSelectedPort();
    if (port.isEmpty())
        port = QStringLiteral("-");
    const QString param = QStringLiteral("%1,%2,%3,%4,%5")
        .arg(m_cmbBaud->currentText(), m_databits, m_parity.left(1),
             m_stopbits, sjj::flowShort(m_flow));
    if (m_ser && m_ser->isOpen()) {
        m_lblStatus->setText(port + QStringLiteral(" 已打开 ") + param);
        m_lblStatus->setStyleSheet(
            QStringLiteral("color:%1;").arg(m_themeC.value(QStringLiteral("ok_color"))));
    } else {
        m_lblStatus->setText(port + QStringLiteral(" 已关闭 ") + param);
        m_lblStatus->setStyleSheet(
            QStringLiteral("color:%1;").arg(m_themeC.value(QStringLiteral("text_primary"))));
    }
}

// ================= 接收渲染 =================

QString SerialTool::decodeBytesByCurrent(const QByteArray& data) const {
    return sjj::decodeBytes(data, m_cmbEncoding->currentText());
}

QString SerialTool::rxBody(const QByteArray& data) const {
    if (m_chkShowHex->isChecked())
        return sjj::hexBody(data);
    return decodeBytesByCurrent(data);
}

QPair<QString, QString> SerialTool::renderParts(const Record& rec) const {
    const QString ts = rec.ts.isEmpty() ? sjj::nowTimestamp() : rec.ts;
    QString mark, text;
    if (rec.kind == QStringLiteral("rx")) {
        mark = m_chkShowTs->isChecked() ? (ts + QStringLiteral("< ")) : QStringLiteral("< ");
        text = rxBody(rec.rawBytes);
    } else if (rec.kind == QStringLiteral("tx")) {
        mark = m_chkShowTs->isChecked() ? (ts + QStringLiteral("> ")) : QStringLiteral("> ");
        if (!rec.txData.isEmpty()) {
            QByteArray body = rec.txData;
            if (!rec.txCs.isEmpty())
                body = rec.txData.left(rec.txData.size() - rec.txCs.size());
            if (m_chkShowHex->isChecked())
                text = sjj::spacedHex(body);
            else if (rec.hexInput)
                text = decodeBytesByCurrent(body);
            else
                text = rec.rawText;
            if (!rec.txCs.isEmpty())
                text += QStringLiteral(" [") + sjj::spacedHex(rec.txCs) + QStringLiteral("]");
        } else {
            text = rec.rawText;
        }
    } else { // sys
        mark = m_chkShowTs->isChecked() ? (ts + QLatin1Char(' ')) : QString();
        text = rec.rawText;
    }
    return {mark, text};
}

QString SerialTool::renderRecord(const Record& rec) const {
    const auto parts = renderParts(rec);
    return parts.first + parts.second + QLatin1Char('\n');
}

void SerialTool::addTxRecord(const QString& raw, const QByteArray& data,
                             const QByteArray& csBytes, bool hexInput) {
    Record r;
    r.kind = QStringLiteral("tx");
    r.rawText = raw;
    r.txData = data;
    r.txCs = csBytes;
    r.hexInput = hexInput;
    // 时间戳在发送时刻生成；事件进入统一队列，与接收保持真实顺序
    r.ts = sjj::nowTimestamp();
    m_pendingEvents.append(r);
}

void SerialTool::addRecord(const QString& kind, const QString& raw) {
    Record r;
    r.kind = kind;
    r.rawText = raw;
    r.ts = sjj::nowTimestamp();
    m_pendingEvents.append(r);
}

void SerialTool::appendToView(QVector<Record>& recs) {
    ScrollGuard guard(m_programScroll);
    // 超上限裁剪最旧（保持内存与视图一致）
    if (m_records.size() > sjj::MAX_RECORDS) {
        const int overflow = m_records.size() - sjj::MAX_RECORDS;
        const QVector<Record> removed = m_records.mid(0, overflow);
        m_records.remove(0, overflow);
        if (m_chkFilter->isChecked()) {
            refreshView();
            return;
        }
        int delLines = 0;
        for (const auto& r : removed)
            delLines += renderRecord(r).count(QLatin1Char('\n'));
        deleteTopLines(delLines);
    }
    const QString needle = m_entrySearch->text();
    if (m_chkFilter->isChecked() && !needle.isEmpty()) {
        QVector<Record> filtered;
        filtered.reserve(recs.size());
        for (const auto& r : recs) {
            if (renderRecord(r).toLower().contains(needle.toLower()))
                filtered.append(r);
        }
        if (filtered.isEmpty())
            return;
        recs = filtered;
    }
    QTextCursor cursor = m_txtRecv->textCursor();
    cursor.movePosition(QTextCursor::End);
    const QColor accent(m_themeC.value(QStringLiteral("accent")));
    const QColor editFg(m_themeC.value(QStringLiteral("edit_fg")));
    for (const auto& r : recs) {
        const auto parts = renderParts(r);
        if (!parts.first.isEmpty()) {
            QTextCharFormat fmt;
            fmt.setForeground(accent);
            fmt.setFontWeight(QFont::Bold);
            cursor.insertText(parts.first, fmt);
        }
        QTextCharFormat plain;
        plain.setForeground(editFg);
        plain.setFontWeight(QFont::Normal);
        cursor.setCharFormat(plain);
        cursor.insertText(parts.second);
        cursor.insertText(QStringLiteral("\n"));
    }
    if (!needle.isEmpty())
        applyHighlight();
    if (!m_chkPause->isChecked())
        m_txtRecv->verticalScrollBar()->setValue(m_txtRecv->verticalScrollBar()->maximum());
}

void SerialTool::deleteTopLines(int n) {
    QTextCursor cursor(m_txtRecv->document());
    cursor.movePosition(QTextCursor::Start);
    for (int i = 0; i < n; ++i) {
        if (!cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor))
            break;
    }
    cursor.removeSelectedText();
}

void SerialTool::refreshView() {
    ScrollGuard guard(m_programScroll);
    const QString needle = m_entrySearch->text();
    const bool filt = m_chkFilter->isChecked();
    const QColor accent(m_themeC.value(QStringLiteral("accent")));
    const QColor editFg(m_themeC.value(QStringLiteral("edit_fg")));
    m_txtRecv->blockSignals(true);
    m_txtRecv->clear();
    QTextCursor cursor = m_txtRecv->textCursor();
    cursor.movePosition(QTextCursor::End);
    for (const auto& r : m_records) {
        const QString rendered = renderRecord(r);
        if (filt && !needle.isEmpty() && !rendered.toLower().contains(needle.toLower()))
            continue;
        const auto parts = renderParts(r);
        if (!parts.first.isEmpty()) {
            QTextCharFormat fmt;
            fmt.setForeground(accent);
            fmt.setFontWeight(QFont::Bold);
            cursor.insertText(parts.first, fmt);
        }
        QTextCharFormat plain;
        plain.setForeground(editFg);
        plain.setFontWeight(QFont::Normal);
        cursor.setCharFormat(plain);
        cursor.insertText(parts.second + QStringLiteral("\n"));
    }
    m_txtRecv->blockSignals(false);
    applyHighlight();
    if (!m_chkPause->isChecked())
        m_txtRecv->verticalScrollBar()->setValue(m_txtRecv->verticalScrollBar()->maximum());
}

QString SerialTool::accentHex() const {
    return m_themeC.value(QStringLiteral("accent"));
}

void SerialTool::applyHighlight() {
    const QString needle = m_entrySearch->text();
    QList<QTextEdit::ExtraSelection> sels;
    if (!needle.isEmpty()) {
        QTextCharFormat fmt;
        fmt.setBackground(QColor(QStringLiteral("#ffe066")));
        fmt.setForeground(QColor(QStringLiteral("#000000")));
        QTextDocument* doc = m_txtRecv->document();
        QTextCursor cursor(doc);
        while (true) {
            cursor = doc->find(needle, cursor);
            if (cursor.isNull())
                break;
            QTextEdit::ExtraSelection sel;
            sel.format = fmt;
            sel.cursor = cursor;
            sels.append(sel);
        }
    }
    m_txtRecv->setExtraSelections(sels);
}

void SerialTool::onSearchChange() {
    // 防抖：停止输入 200ms 后再刷新
    if (m_searchTimer)
        m_searchTimer->stop();
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    connect(m_searchTimer, &QTimer::timeout, this, &SerialTool::refreshView);
    m_searchTimer->start(200);
}

void SerialTool::onRecvScroll(int value) {
    // 程序性滚动（搜索刷新/批量插入/清空）不勾选
    if (m_programScroll)
        return;
    auto* sb = m_txtRecv->verticalScrollBar();
    m_chkPause->setChecked(value < sb->maximum());
}

void SerialTool::drainRx() {
    // 50ms 周期：把统一事件队列（rx/tx/sys 按发生顺序）刷新到接收区
    if (!m_pendingEvents.isEmpty()) {
        QVector<Record> recs = m_pendingEvents;
        m_pendingEvents.clear();
        m_records += recs;
        appendToView(recs);
    }
    // 状态栏节流 500ms
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastStatusTs >= 500) {
        m_lastStatusTs = now;
        const QString sr = QStringLiteral("S:%1  R:%2").arg(m_txBytes).arg(m_rxBytes);
        if (sr != m_lastSrText) {
            m_lblSr->setText(sr);
            m_lastSrText = sr;
        }
        if (m_ser && m_ser->isOpen()) {
            const auto sigs = m_ser->pinoutSignals();
            const QString hs = QStringLiteral("CTS=%1 DSR=%2 RLSD=%3")
                .arg(sigs & QSerialPort::ClearToSendSignal ? 1 : 0)
                .arg(sigs & QSerialPort::DataSetReadySignal ? 1 : 0)
                .arg(sigs & QSerialPort::DataCarrierDetectSignal ? 1 : 0);
            if (hs != m_lastHsText) {
                m_lblHandshake->setText(hs);
                m_lastHsText = hs;
            }
        } else if (m_lastHsText != QStringLiteral("CTS=0 DSR=0 RLSD=0")) {
            m_lblHandshake->setText(QStringLiteral("CTS=0 DSR=0 RLSD=0"));
            m_lastHsText = QStringLiteral("CTS=0 DSR=0 RLSD=0");
        }
    }
}

void SerialTool::clearRecv() {
    ScrollGuard guard(m_programScroll);
    m_txtRecv->clear();
    m_records.clear();
    m_pendingEvents.clear();   // 丢弃队列中尚未刷新的事件（否则清空后残留又会显示）
}

void SerialTool::saveRecv() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存数据"), QString(),
        QStringLiteral("文本文件 (*.txt);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, QStringLiteral("保存失败"), f.errorString());
        return;
    }
    f.write(m_txtRecv->toPlainText().toUtf8());
    f.close();
    QMessageBox::information(this, QStringLiteral("保存"), QStringLiteral("已保存到\n%1").arg(path));
}

// ================= 校验 =================

QByteArray SerialTool::computeChecksumFor(const QByteArray& data,
                                          const QString& algorithm) const {
    return sjj::computeChecksum(data, algorithm);
}

QByteArray SerialTool::checksumRangeFor(const QByteArray& data) const {
    int start = m_entryCsStart->text().toInt();
    int endOff = m_entryCsEnd->text().toInt();
    if (start < 1)
        start = 1;
    if (endOff > 0)
        endOff = 0;
    return sjj::checksumRange(data, start, endOff);
}

void SerialTool::updateChecksumHighlight() {
    if (!m_csGroup)
        return;
    const bool on = m_cmbChecksum->currentText() != QStringLiteral("None");
    if (m_csGroup->property("cs_highlight").toBool() == on)
        return;
    m_csGroup->setProperty("cs_highlight", on);
    m_csGroup->style()->unpolish(m_csGroup);
    m_csGroup->style()->polish(m_csGroup);
}

QPair<QByteArray, QByteArray> SerialTool::applyChecksum(const QByteArray& data) {
    if (data.isEmpty()) {
        m_lblCsResult->clear();
        return {data, {}};
    }
    if (m_cmbChecksum->currentText() == QStringLiteral("None")) {
        m_lblCsResult->clear();
        return {data, {}};
    }
    const QByteArray head = checksumRangeFor(data);
    if (head.isEmpty()) {
        m_lblCsResult->clear();
        return {data, {}};
    }
    const QByteArray cs = computeChecksumFor(head, m_cmbChecksum->currentText());
    if (cs.isEmpty()) {
        m_lblCsResult->clear();
        return {data, {}};
    }
    m_lblCsResult->setText(sjj::spacedHex(cs));
    return {data + cs, cs};
}

void SerialTool::updateCsPreview() {
    // 防抖 100ms 后根据发送框内容实时预览校验结果
    if (m_csPreviewTimer)
        m_csPreviewTimer->stop();
    m_csPreviewTimer = new QTimer(this);
    m_csPreviewTimer->setSingleShot(true);
    connect(m_csPreviewTimer, &QTimer::timeout, this, &SerialTool::doCsPreview);
    m_csPreviewTimer->start(100);
}

void SerialTool::doCsPreview() {
    const QString raw = m_txtSend->toPlainText();
    if (raw.isEmpty()) {
        m_lblCsResult->clear();
        return;
    }
    bool ok = false;
    QString err;
    QByteArray data = encodeSendText(raw, ok, err);
    if (!ok) {
        m_lblCsResult->clear();
        return;
    }
    // 与发送一致：先加回车换行，再算校验（CRLF 参与校验计算）
    if (m_chkAddCrlf->isChecked())
        data += "\r\n";
    applyChecksum(data);
}

// ================= 发送 =================

QByteArray SerialTool::encodeSendText(const QString& raw, bool& ok, QString& err) const {
    if (m_chkSendHex->isChecked())
        return sjj::parseHexString(raw, ok, err);
    ok = true;
    return sjj::encodeString(raw, QStringLiteral("UTF-8"));
}

void SerialTool::send() {
    doSend(m_txtSend->toPlainText());
}

void SerialTool::doSend(const QString& raw) {
    if (!m_ser || !m_ser->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先打开串口"));
        return;
    }
    if (raw.isEmpty())
        return;
    recordSendHistory(raw);
    bool ok = false;
    QString err;
    QByteArray data = encodeSendText(raw, ok, err);
    if (!ok) {
        QMessageBox::critical(this, QStringLiteral("HEX错误"), err);
        return;
    }
    // 先加回车换行，再算校验码（CRLF 参与校验计算，作为帧的一部分）
    if (m_chkAddCrlf->isChecked())
        data += "\r\n";
    const auto [out, cs] = applyChecksum(data);
    const qint64 n = m_ser->write(out);
    if (n < 0) {
        QMessageBox::critical(this, QStringLiteral("发送失败"), m_ser->errorString());
        return;
    }
    m_txBytes += quint64(n);
    echoSend(raw, out, cs);
    saveCurrentSendSlot();
}

void SerialTool::echoSend(const QString& raw, const QByteArray& data,
                          const QByteArray& csBytes) {
    addTxRecord(raw, data, csBytes, m_chkSendHex->isChecked());
}

void SerialTool::recordSendHistory(const QString& raw) {
    if (raw.isEmpty())
        return;
    m_sendHistory.removeAll(raw);
    m_sendHistory.prepend(raw);
    while (m_sendHistory.size() > 50)
        m_sendHistory.removeLast();
    refreshHistoryList();
}

void SerialTool::clearSend() {
    m_txtSend->clear();
}

void SerialTool::saveCurrentSendSlot() {
    const QString raw = m_txtSend->toPlainText();
    if (m_chkSendHex->isChecked())
        m_lastSendHex = raw;
    else
        m_lastSendText = raw;
}

void SerialTool::loadSendSlot() {
    const QString content = m_chkSendHex->isChecked() ? m_lastSendHex : m_lastSendText;
    m_txtSend->blockSignals(true);
    m_txtSend->setPlainText(content);
    m_txtSend->blockSignals(false);
    updateCsPreview();
}

void SerialTool::onSendHexToggle() {
    const bool newMode = m_chkSendHex->isChecked();
    const QString raw = m_txtSend->toPlainText();
    if (m_sendHexPrev)
        m_lastSendHex = raw;
    else
        m_lastSendText = raw;
    m_sendHexPrev = newMode;
    loadSendSlot();
}

// ================= 定时发送 =================

void SerialTool::onTimerToggle() {
    if (m_chkTimer->isChecked()) {
        bool ok = false;
        int interval = m_entryInterval->text().toInt(&ok);
        if (!ok || interval < 10)
            interval = 1000;
        m_entryInterval->setText(QString::number(interval));
        if (m_ser && m_ser->isOpen())
            m_timerSend->start(interval);
    } else {
        m_timerSend->stop();
    }
}

void SerialTool::timerSendTick() {
    if (!m_ser || !m_ser->isOpen()) {
        m_timerSend->stop();
        m_chkTimer->setChecked(false);
        return;
    }
    send();
}

// ================= 多字符串发送 =================

void SerialTool::refreshMsRows() {
    for (auto& ws : m_msRowWidgets) {
        for (auto* w : ws) {
            w->setParent(nullptr);
            w->deleteLater();
        }
    }
    m_msRowWidgets.clear();
    for (int i = 0; i < m_msEntries.size(); ++i) {
        auto* row = new QHBoxLayout;
        row->setSpacing(2);
        auto* chk = new ThemeCheckBox(QStringLiteral("HEX"));
        chk->setThemeColors(m_themeC);
        chk->setChecked(m_msEntries[i].hex);
        connect(chk, &QCheckBox::toggled, this,
                [this, i](bool v) { msSetHex(i, v); });
        row->addWidget(chk);
        auto* ent = new QLineEdit(m_msEntries[i].content);
        connect(ent, &QLineEdit::textChanged, this,
                [this, i](const QString& t) { msSetContent(i, t); });
        row->addWidget(ent, 1);
        auto* btn = new QPushButton(m_msEntries[i].label);
        btn->setFixedWidth(96);
        connect(btn, &QPushButton::clicked, this, [this, i] { sendMsEntry(i); });
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QWidget::customContextMenuRequested, this,
                [this, i](const QPoint&) { renameMsEntry(i); });
        row->addWidget(btn);
        auto* xbtn = new QPushButton(QStringLiteral("\u2715"));
        xbtn->setFixedWidth(28);
        connect(xbtn, &QPushButton::clicked, this, [this, i] { deleteMsEntry(i); });
        row->addWidget(xbtn);
        m_msRows->insertLayout(m_msRows->count() - 1, row);
        m_msRowWidgets.append({chk, ent, btn, xbtn});
    }
}

void SerialTool::msSetHex(int i, bool val) {
    if (i >= 0 && i < m_msEntries.size()) {
        m_msEntries[i].hex = val;
        saveParams(true);   // 快捷命令变化 → 立即保存（触发其他实例同步）
    }
}

void SerialTool::msSetContent(int i, const QString& t) {
    if (i >= 0 && i < m_msEntries.size()) {
        m_msEntries[i].content = t;
        scheduleQuickCmdSave();   // 内容编辑防抖保存（300ms）
    }
}

// 快捷命令内容编辑防抖保存
void SerialTool::scheduleQuickCmdSave() {
    if (m_cfgSaveTimer)
        m_cfgSaveTimer->start(300);
}

// 配置文件被其他实例修改：仅同步快捷命令（ms_entries），其他配置不动
void SerialTool::onConfigFileChanged(const QString&) {
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return;
    const QJsonArray arr = doc.object().value(QStringLiteral("ms_entries")).toArray();
    // 与当前内容比较：相同（自身保存触发/未变化）则跳过，避免无谓刷新与回环
    if (QJsonDocument(arr).toJson(QJsonDocument::Compact)
        == QJsonDocument(msEntriesToData()).toJson(QJsonDocument::Compact))
        return;
    // 应用文件中的快捷命令
    m_msEntries.clear();
    for (const QJsonValue& v : arr) {
        const QJsonObject e = v.toObject();
        MsEntry me;
        me.hex = e.value(QStringLiteral("hex")).toBool(false);
        me.content = e.value(QStringLiteral("content")).toString();
        me.label = e.value(QStringLiteral("label")).toString(QStringLiteral("发送"));
        if (me.label.isEmpty())
            me.label = QStringLiteral("发送");
        m_msEntries.append(me);
    }
    refreshMsRows();
}

void SerialTool::newMsEntry() {
    const int idx = m_msEntries.size() + 1;
    MsEntry e;
    e.hex = false;
    e.label = QStringLiteral("发送%1").arg(idx);
    m_msEntries.append(e);
    refreshMsRows();
    saveParams(true);   // 同步到其他实例
}

void SerialTool::deleteMsEntry(int i) {
    if (i >= 0 && i < m_msEntries.size()) {
        m_msEntries.removeAt(i);
        refreshMsRows();
        saveParams(true);   // 同步到其他实例
    }
}

void SerialTool::renameMsEntry(int i) {
    if (i < 0 || i >= m_msEntries.size())
        return;
    bool ok = false;
    const QString newLabel = QInputDialog::getText(
        this, QStringLiteral("修改按钮文字"),
        QStringLiteral("请输入第 %1 行的按钮显示文字：").arg(i + 1),
        QLineEdit::Normal, m_msEntries[i].label, &ok);
    if (ok && !newLabel.trimmed().isEmpty()) {
        m_msEntries[i].label = newLabel.trimmed();
        refreshMsRows();
        saveParams(true);   // 同步到其他实例
    }
}

void SerialTool::sendMsEntry(int i) {
    if (!m_ser || !m_ser->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先打开串口"));
        return;
    }
    if (i < 0 || i >= m_msEntries.size())
        return;
    const MsEntry& e = m_msEntries[i];
    const QString raw = e.content;
    if (raw.isEmpty())
        return;
    QByteArray data;
    if (e.hex) {
        bool ok = false;
        QString err;
        data = sjj::parseHexString(raw, ok, err);
        if (!ok) {
            QMessageBox::critical(this, QStringLiteral("HEX错误"),
                                  QStringLiteral("第 %1 行 %2").arg(i + 1).arg(err));
            return;
        }
    } else {
        data = sjj::encodeString(raw, QStringLiteral("UTF-8"));
    }
    // 先加回车换行，再算校验码（CRLF 参与校验计算，作为帧的一部分）
    if (m_chkAddCrlf->isChecked())
        data += "\r\n";
    const auto [out, cs] = applyChecksum(data);
    const qint64 n = m_ser->write(out);
    if (n < 0) {
        QMessageBox::critical(this, QStringLiteral("发送失败"), m_ser->errorString());
        return;
    }
    m_txBytes += quint64(n);
    addTxRecord(raw, out, cs, e.hex);
    recordSendHistory(raw);
}

void SerialTool::startMsLoop() {
    if (!m_ser || !m_ser->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先打开串口"));
        return;
    }
    if (m_msEntries.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("列表为空，请先新建条目"));
        return;
    }
    bool ok = false;
    int interval = m_entryMsInterval->text().toInt(&ok);
    if (!ok || interval < 10)
        interval = 1000;
    m_entryMsInterval->setText(QString::number(interval));
    m_msLooping = true;
    m_msLoopIndex = 0;
    m_btnStopMs->setEnabled(true);
    m_msLoopTimer->start(interval);
}

void SerialTool::msLoopTick() {
    if (!m_msLooping) {
        m_msLoopTimer->stop();
        return;
    }
    if (!m_ser || !m_ser->isOpen()) {
        stopMsLoop();
        return;
    }
    if (m_msLoopIndex >= m_msEntries.size()) {
        if (m_chkMsLoop->isChecked())
            m_msLoopIndex = 0;
        else {
            stopMsLoop();
            return;
        }
    }
    sendMsEntry(m_msLoopIndex);
    ++m_msLoopIndex;
}

void SerialTool::stopMsLoop() {
    m_msLooping = false;
    m_msLoopTimer->stop();
    m_btnStopMs->setEnabled(false);
}

// ================= 文件发送 =================

void SerialTool::pickFile() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择文件"),
                                                      QString(),
                                                      QStringLiteral("所有文件 (*.*)"));
    if (!path.isEmpty()) {
        m_btnSelectFile->setText(QFileInfo(path).fileName());
        m_btnSelectFile->setToolTip(path);
        m_selectedFilePath = path;
    }
}

void SerialTool::sendFile() {
    if (m_fileSending) {
        stopSendFile();
        return;
    }
    if (!m_ser || !m_ser->isOpen()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先打开串口"));
        return;
    }
    if (m_selectedFilePath.isEmpty() || !QFileInfo::exists(m_selectedFilePath)) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先点击「选择文件」选择要发送的文件"));
        return;
    }
    QFile f(m_selectedFilePath);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, QStringLiteral("读取失败"), f.errorString());
        return;
    }
    QByteArray data = f.readAll();
    f.close();
    // 先加回车换行（文本文件），再算校验码（.bin/.hex 不加 CRLF）
    const QString lower = m_selectedFilePath.toLower();
    if (m_chkAddCrlf->isChecked() && !lower.endsWith(QStringLiteral(".bin"))
        && !lower.endsWith(QStringLiteral(".hex")))
        data += "\r\n";
    m_fileData = applyChecksum(data).first;
    m_fileOffset = 0;
    m_fileSending = true;
    m_btnSendFile->setText(QStringLiteral("停止发送"));
    addRecord(QStringLiteral("sys"),
              QStringLiteral("开始发送文件: %1 (%2 字节)")
                  .arg(QFileInfo(m_selectedFilePath).fileName()).arg(m_fileData.size()));
    QTimer::singleShot(0, this, &SerialTool::sendFileChunk);
}

void SerialTool::sendFileChunk() {
    if (!m_fileSending) {
        endSendFile();
        return;
    }
    if (!m_ser || !m_ser->isOpen()) {
        endSendFile();
        return;
    }
    const QByteArray chunk = m_fileData.mid(m_fileOffset, 4096);
    if (chunk.isEmpty()) {
        endSendFile();
        addRecord(QStringLiteral("sys"), QStringLiteral("文件发送完成"));
        return;
    }
    const qint64 n = m_ser->write(chunk);
    if (n < 0) {
        QMessageBox::critical(this, QStringLiteral("发送失败"), m_ser->errorString());
        endSendFile();
        return;
    }
    m_txBytes += quint64(n);
    m_fileOffset += n;
    QTimer::singleShot(2, this, &SerialTool::sendFileChunk);
}

void SerialTool::stopSendFile() {
    m_fileSending = false;
    endSendFile();
    addRecord(QStringLiteral("sys"), QStringLiteral("文件发送已停止"));
}

void SerialTool::endSendFile() {
    m_fileSending = false;
    m_fileData.clear();
    m_fileOffset = 0;
    m_btnSendFile->setText(QStringLiteral("发送文件"));
}

// ================= 历史记录 =================

void SerialTool::refreshHistoryList() {
    m_histList->clear();
    for (const QString& item : m_sendHistory) {
        QString disp = item;
        disp.replace(QLatin1Char('\n'), QStringLiteral("\u23CE "));
        if (disp.size() > 80)
            disp = disp.left(80) + QStringLiteral("\u2026");
        m_histList->addItem(disp);
    }
}

void SerialTool::histLoad(QListWidgetItem* item) {
    const int idx = m_histList->row(item);
    if (idx >= 0 && idx < m_sendHistory.size())
        m_txtSend->setPlainText(m_sendHistory[idx]);
}

void SerialTool::histDelete() {
    QVector<int> rows;
    const auto sels = m_histList->selectedItems();
    for (auto* it : sels)
        rows.append(m_histList->row(it));
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int i : rows) {
        if (i >= 0 && i < m_sendHistory.size())
            m_sendHistory.removeAt(i);
    }
    refreshHistoryList();
}

void SerialTool::histClear() {
    if (m_sendHistory.isEmpty())
        return;
    if (QMessageBox::question(this, QStringLiteral("确认"),
                              QStringLiteral("清空全部发送历史？")) == QMessageBox::Yes) {
        m_sendHistory.clear();
        refreshHistoryList();
    }
}

// ================= 面板切换 =================

void SerialTool::toggleWindow(int tabIndex) {
    if (m_msTabs->isVisible()) {
        if (m_msTabs->currentIndex() == tabIndex) {
            m_msTabs->hide();
            return;
        }
        m_msTabs->setCurrentIndex(tabIndex);
    } else {
        m_msTabs->show();
        m_msTabs->setCurrentIndex(tabIndex);
        if (tabIndex == 0)
            refreshHistoryList();
    }
}

// ================= GitHub =================

void SerialTool::openGithub() {
    QDesktopServices::openUrl(QUrl(sjj::GITHUB_URL));
}

// ================= 配置 =================

QString SerialTool::configPath() const {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/sscom_config.json");
}

QJsonArray SerialTool::msEntriesToData() const {
    QJsonArray arr;
    for (const auto& e : m_msEntries) {
        QJsonObject o;
        o[QStringLiteral("hex")] = e.hex;
        o[QStringLiteral("content")] = e.content;
        o[QStringLiteral("label")] = e.label;
        arr.append(o);
    }
    return arr;
}

void SerialTool::saveParams(bool silent) {
    saveCurrentSendSlot();
    QJsonObject o;
    o[QStringLiteral("port")] = getSelectedPort();
    o[QStringLiteral("baud")] = m_cmbBaud->currentText();
    o[QStringLiteral("databits")] = m_databits;
    o[QStringLiteral("stopbits")] = m_stopbits;
    o[QStringLiteral("parity")] = m_parity;
    o[QStringLiteral("flow")] = m_flow;
    o[QStringLiteral("read_timeout_ms")] = m_readTimeout;
    o[QStringLiteral("show_hex")] = m_chkShowHex->isChecked();
    o[QStringLiteral("send_hex")] = m_chkSendHex->isChecked();
    o[QStringLiteral("add_crlf")] = m_chkAddCrlf->isChecked();
    o[QStringLiteral("interval_ms")] = m_entryInterval->text();
    o[QStringLiteral("show_ts")] = m_chkShowTs->isChecked();
    o[QStringLiteral("ms_entries")] = msEntriesToData();
    o[QStringLiteral("send_history")] = QJsonArray::fromStringList(m_sendHistory);
    o[QStringLiteral("checksum")] = m_cmbChecksum->currentText();
    o[QStringLiteral("cs_start")] =
        m_entryCsStart->text().trimmed().isEmpty() ? QStringLiteral("1")
                                                   : m_entryCsStart->text().trimmed();
    o[QStringLiteral("cs_end")] =
        m_entryCsEnd->text().trimmed().isEmpty() ? QStringLiteral("0")
                                                 : m_entryCsEnd->text().trimmed();
    o[QStringLiteral("encoding")] = m_cmbEncoding->currentText();
    o[QStringLiteral("last_send_text")] = m_lastSendText;
    o[QStringLiteral("last_send_hex")] = m_lastSendHex;
    o[QStringLiteral("window_w")] = width();
    o[QStringLiteral("window_h")] = height();
    o[QStringLiteral("panel_visible")] = !m_msTabs->isHidden();
    o[QStringLiteral("panel_tab")] = m_msTabs->currentIndex();
    o[QStringLiteral("theme")] = m_theme;
    o[QStringLiteral("ignore_update_version")] = m_ignoreUpdateVersion;
    QFile f(configPath());
    if (!f.open(QIODevice::WriteOnly)) {
        if (!silent)
            QMessageBox::critical(this, QStringLiteral("保存失败"), f.errorString());
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.close();
}

void SerialTool::loadParams(bool silent) {
    m_suppressApply = true;   // 配置加载期间不触发"自动重开"
    loadParamsInner(silent);
    m_suppressApply = false;
}

void SerialTool::loadParamsInner(bool silent) {
    QFile f(configPath());
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly)) {
        if (!silent)
            QMessageBox::critical(this, QStringLiteral("加载失败"), f.errorString());
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return;
    const QJsonObject o = doc.object();

    // 主题优先应用（其余控件样式依赖全局 QSS）
    QString themeName = o.value(QStringLiteral("theme")).toString(QStringLiteral("light"));
    if (themeName != QStringLiteral("dark"))
        themeName = QStringLiteral("light");
    applyTheme(themeName);
    m_ignoreUpdateVersion = o.value(QStringLiteral("ignore_update_version")).toString();
    m_cmbBaud->setCurrentText(o.value(QStringLiteral("baud")).toString(QStringLiteral("115200")));
    m_databits = o.value(QStringLiteral("databits")).toString(QStringLiteral("8"));
    m_stopbits = o.value(QStringLiteral("stopbits")).toString(QStringLiteral("1"));
    m_parity = o.value(QStringLiteral("parity")).toString(QStringLiteral("None"));
    m_flow = o.value(QStringLiteral("flow")).toString(QStringLiteral("None"));
    m_readTimeout = o.value(QStringLiteral("read_timeout_ms")).toString(QStringLiteral("50"));
    m_chkShowHex->setChecked(o.value(QStringLiteral("show_hex")).toBool(false));
    m_chkSendHex->setChecked(o.value(QStringLiteral("send_hex")).toBool(false));
    m_chkAddCrlf->setChecked(o.value(QStringLiteral("add_crlf")).toBool(false));
    m_entryInterval->setText(o.value(QStringLiteral("interval_ms")).toString(QStringLiteral("1000")));
    m_chkShowTs->setChecked(o.value(QStringLiteral("show_ts")).toBool(true));
    // 端口匹配
    const QString target = o.value(QStringLiteral("port")).toString();
    for (auto it = m_portMap.cbegin(); it != m_portMap.cend(); ++it) {
        if (it.value() == target) {
            m_cmbPort->setCurrentText(it.key());
            break;
        }
    }
    // 多字符串面板（含旧字段迁移：quick_cmds + send_history → ms_entries）
    QJsonArray msArr = o.value(QStringLiteral("ms_entries")).toArray();
    if (msArr.isEmpty()) {
        QStringList seen;
        const QJsonArray quick = o.value(QStringLiteral("quick_cmds")).toArray();
        const QJsonArray hist = o.value(QStringLiteral("send_history")).toArray();
        if (!quick.isEmpty() || !hist.isEmpty()) {
            for (const QJsonValue& v : quick) {
                const QString s = v.toString();
                if (!s.isEmpty() && !seen.contains(s))
                    seen.append(s);
            }
            for (const QJsonValue& v : hist) {
                const QString s = v.toString();
                if (!s.isEmpty() && !seen.contains(s))
                    seen.append(s);
            }
            for (const QString& s : seen) {
                QJsonObject e;
                e[QStringLiteral("hex")] = false;
                e[QStringLiteral("content")] = s;
                QString label = s.left(8);
                label.replace(QLatin1Char('\n'), QLatin1Char(' '));
                e[QStringLiteral("label")] = label.isEmpty() ? QStringLiteral("导入") : label;
                msArr.append(e);
            }
        }
    }
    m_msEntries.clear();
    for (const QJsonValue& v : msArr) {
        const QJsonObject e = v.toObject();
        MsEntry me;
        me.hex = e.value(QStringLiteral("hex")).toBool(false);
        me.content = e.value(QStringLiteral("content")).toString();
        me.label = e.value(QStringLiteral("label")).toString(QStringLiteral("发送"));
        if (me.label.isEmpty())
            me.label = QStringLiteral("发送");
        m_msEntries.append(me);
    }
    refreshMsRows();
    m_sendHistory.clear();
    const QJsonArray histArr = o.value(QStringLiteral("send_history")).toArray();
    for (const QJsonValue& v : histArr)
        m_sendHistory.append(v.toString());
    while (m_sendHistory.size() > 50)
        m_sendHistory.removeLast();
    refreshHistoryList();
    const QString ck = o.value(QStringLiteral("checksum")).toString(QStringLiteral("None"));
    if (sjj::CHECKSUMS.contains(ck))
        m_cmbChecksum->setCurrentText(ck);
    bool okN = false;
    const int csStart = o.value(QStringLiteral("cs_start")).toString(QStringLiteral("1")).toInt(&okN);
    m_entryCsStart->setText(QString::number(okN ? qMax(1, csStart) : 1));
    const int csEnd = o.value(QStringLiteral("cs_end")).toString(QStringLiteral("0")).toInt(&okN);
    m_entryCsEnd->setText(QString::number(okN ? qMin(0, csEnd) : 0));
    const QString enc = o.value(QStringLiteral("encoding")).toString(QStringLiteral("UTF-8"));
    if (sjj::ENCODINGS.contains(enc))
        m_cmbEncoding->setCurrentText(enc);
    m_lastSendText = o.value(QStringLiteral("last_send_text")).toString();
    m_lastSendHex = o.value(QStringLiteral("last_send_hex")).toString();
    m_sendHexPrev = m_chkSendHex->isChecked();
    loadSendSlot();
    // 窗口大小恢复（校验最小值与屏幕范围）
    const int w = o.value(QStringLiteral("window_w")).toInt(0);
    const int h = o.value(QStringLiteral("window_h")).toInt(0);
    if (w >= minimumWidth() && h >= minimumHeight()) {
        int ww = w, hh = h;
        if (screen()) {
            const QRect geo = screen()->availableGeometry();
            if (ww > geo.width())
                ww = geo.width();
            if (hh > geo.height())
                hh = geo.height();
        }
        resize(ww, hh);
    }
    // 命令面板（历史/快捷命令）显示状态恢复
    if (o.value(QStringLiteral("panel_visible")).toBool(false)) {
        m_msTabs->show();
        const int idx = o.value(QStringLiteral("panel_tab")).toInt(0);
        if (idx >= 0 && idx < m_msTabs->count())
            m_msTabs->setCurrentIndex(idx);
        if (m_msTabs->currentIndex() == 0)
            refreshHistoryList();
    }
    refreshStatus();
}

void SerialTool::autoloadConfig() {
    if (QFileInfo::exists(configPath()))
        loadParams(true);
    else
        saveParams(true);
}
