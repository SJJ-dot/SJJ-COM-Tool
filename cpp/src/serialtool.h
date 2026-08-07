#pragma once
// SJJ-COM Tool 主窗口（C++/Qt6 版，功能与 PySide6 版完全一致）

#include <QWidget>
#include <QSerialPort>
#include <QVector>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QPair>
#include <QVariantMap>
#include <QJsonArray>

#include "themes.h"
#include "combobox.h"
#include "themechkbox.h"
#include "titlebar.h"

class QTextEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QTabWidget;
class QScrollArea;
class QProgressBar;
class QToolButton;
class QDialog;
class QFrame;
class QVBoxLayout;
class QHBoxLayout;
class QFileSystemWatcher;

class UpdateChecker;
class ExeDownloader;

// 一条接收/发送/系统记录（渲染时按当前 HEX/编码/时间戳开关格式化）
struct Record {
    QString kind;          // "rx" | "tx" | "sys"
    QByteArray rawBytes;   // rx: 原始字节
    QString rawText;       // sys 文本 / tx 原文
    QByteArray txData;     // tx: 完整发送数据（含校验字节）
    QByteArray txCs;       // tx: 校验字节
    bool hexInput = false; // tx: 发送源是否 HEX 文本
    QString ts;            // 固定时间戳 [HH:MM:SS.mmm]
};

// 快捷命令条目
struct MsEntry {
    bool hex = false;
    QString content;
    QString label;
};

class SerialTool : public QWidget {
    Q_OBJECT
public:
    explicit SerialTool(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    // ---------- UI 构建 ----------
    void buildUi();
    void buildHistoryTab();
    void buildMsTab();

    // ---------- 主题 ----------
    void applyTheme(const QString& themeName);
    void toggleTheme();
    QString themeTextColor() const;   // 当前主题 text_primary

    // ---------- 更新检查 / 下载 ----------
    void checkUpdateAuto();
    void checkUpdateManual();
    void onUpdateCheckResult(const QVariantMap& info, bool manual);
    void showUpdateDialog(const QVariantMap& info);
    void startUpdateDownload(const QVariantMap& info);
    void onDownloadProgress(qint64 done, qint64 total, int speedKbps);
    void onUpdateDownloaded(const QString& tmp);
    void onUpdateDownloadFailed(const QString& err);
    void closeDownloadUi();
    void performUpdateRestart();

    // ---------- 端口 / 串口 ----------
    void refreshPorts();
    QString getSelectedPort() const;
    void showSettingsDialog();
    void applySettings(QDialog* dlg, QComboBox* cmbBaud, QComboBox* cmbDatabits,
                       QComboBox* cmbStopbits, QComboBox* cmbParity,
                       QComboBox* cmbFlow, QLineEdit* entryTimeout);
    void onPortChanged(int idx);
    void onBaudChanged(int idx);
    void applySerialSettings();
    void togglePort();
    void openPort();
    void closePort();
    void onSerialData();
    void onSerialError(QSerialPort::SerialPortError err);
    void refreshStatus();

    // ---------- 接收渲染 ----------
    QString rxBody(const QByteArray& data) const;
    QString decodeBytesByCurrent(const QByteArray& data) const;
    QPair<QString, QString> renderParts(const Record& rec) const;
    QString renderRecord(const Record& rec) const;
    void addTxRecord(const QString& raw, const QByteArray& data,
                     const QByteArray& csBytes = QByteArray(), bool hexInput = false);
    void addRecord(const QString& kind, const QString& raw);
    void appendToView(QVector<Record>& recs);
    void deleteTopLines(int n);
    void refreshView();
    QString accentHex() const;
    void applyHighlight();
    void onSearchChange();
    void onRecvScroll(int value);
    void drainRx();
    void clearRecv();
    void saveRecv();

    // ---------- 校验 ----------
    QByteArray computeChecksumFor(const QByteArray& data, const QString& algorithm) const;
    QByteArray checksumRangeFor(const QByteArray& data) const;
    void updateChecksumHighlight();
    QPair<QByteArray, QByteArray> applyChecksum(const QByteArray& data);
    void updateCsPreview();
    void doCsPreview();

    // ---------- 发送 ----------
    QByteArray encodeSendText(const QString& raw, bool& ok, QString& err) const;
    void send();
    void doSend(const QString& raw);
    void echoSend(const QString& raw, const QByteArray& data, const QByteArray& csBytes);
    void recordSendHistory(const QString& raw);
    void clearSend();
    void saveCurrentSendSlot();
    void loadSendSlot();
    void onSendHexToggle();

    // ---------- 定时发送 ----------
    void onTimerToggle();
    void timerSendTick();

    // ---------- 多字符串发送 ----------
    void refreshMsRows();
    void msSetHex(int i, bool val);
    void msSetContent(int i, const QString& t);
    void newMsEntry();
    void deleteMsEntry(int i);
    void renameMsEntry(int i);
    void sendMsEntry(int i);
    void startMsLoop();
    void msLoopTick();
    void stopMsLoop();
    // 快捷命令多实例实时同步（仅快捷命令；其他配置不实时同步）
    void scheduleQuickCmdSave();              // 内容编辑防抖保存（300ms）
    void onConfigFileChanged(const QString& path);   // 配置文件被其他实例修改

    // ---------- 文件发送 ----------
    void pickFile();
    void sendFile();
    void sendFileChunk();
    void stopSendFile();
    void endSendFile();

    // ---------- 历史记录 ----------
    void refreshHistoryList();
    void histLoad(QListWidgetItem* item);
    void histDelete();
    void histClear();

    // ---------- 面板切换 ----------
    void toggleWindow(int tabIndex);

    // ---------- GitHub ----------
    void openGithub();

    // ---------- 配置 ----------
    QString configPath() const;
    QJsonArray msEntriesToData() const;
    void saveParams(bool silent);
    void loadParams(bool silent);
    void loadParamsInner(bool silent);
    void autoloadConfig();

    // ---------- 无边框窗口缩放 ----------
    void installResizeFilter();
    int detectResizeEdge(const QPoint& pos) const;
    Qt::CursorShape resizeCursor(int edges) const;
    void applyResize(const QPoint& gpos);
    void enableDwmEffects();

    // ============ 控件 ============
    TitleBar* m_titleBar = nullptr;

    QTextEdit* m_txtRecv = nullptr;
    QTabWidget* m_msTabs = nullptr;
    QListWidget* m_histList = nullptr;
    QScrollArea* m_msScroll = nullptr;
    QWidget* m_msInner = nullptr;
    QVBoxLayout* m_msRows = nullptr;
    QLabel* m_lblMsHint = nullptr;

    ThemeCheckBox* m_chkShowHex = nullptr;
    ThemeCheckBox* m_chkShowTs = nullptr;
    ThemeCheckBox* m_chkPause = nullptr;
    ThemeCheckBox* m_chkFilter = nullptr;
    StyledComboBox* m_cmbEncoding = nullptr;
    QLineEdit* m_entrySearch = nullptr;

    PortComboBox* m_cmbPort = nullptr;
    StyledComboBox* m_cmbBaud = nullptr;
    QPushButton* m_btnOpen = nullptr;

    ThemeCheckBox* m_chkSendHex = nullptr;
    ThemeCheckBox* m_chkAddCrlf = nullptr;
    ThemeCheckBox* m_chkTimer = nullptr;
    QLineEdit* m_entryInterval = nullptr;
    QTextEdit* m_txtSend = nullptr;

    QPushButton* m_btnSelectFile = nullptr;
    QPushButton* m_btnSendFile = nullptr;
    QFrame* m_csGroup = nullptr;
    QLineEdit* m_entryCsStart = nullptr;
    QLineEdit* m_entryCsEnd = nullptr;
    StyledComboBox* m_cmbChecksum = nullptr;
    QLabel* m_lblCsResult = nullptr;

    QWidget* m_statusBar = nullptr;
    QHBoxLayout* m_sbLay = nullptr;
    QLabel* m_lblStatus = nullptr;
    QWidget* m_progressContainer = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_lblSpeed = nullptr;
    QToolButton* m_btnCancelDl = nullptr;
    QToolButton* m_btnRestart = nullptr;
    QLabel* m_lblSr = nullptr;
    QLabel* m_lblHandshake = nullptr;
    QLabel* m_lblApp = nullptr;

    ThemeCheckBox* m_chkMsLoop = nullptr;
    QLineEdit* m_entryMsInterval = nullptr;
    QPushButton* m_btnStartMs = nullptr;
    QPushButton* m_btnStopMs = nullptr;

    // ============ 状态 ============
    QSerialPort* m_ser = nullptr;
    quint64 m_rxBytes = 0;
    quint64 m_txBytes = 0;
    QVector<Record> m_records;                        // 全部记录（上限 MAX_RECORDS）
    // 统一收发事件队列：rx/tx/sys 均按"真实发生顺序"入队，
    // 50ms 定时器按队首到尾顺序刷新——保证显示顺序与实际收发顺序一致
    QVector<Record> m_pendingEvents;
    QStringList m_sendHistory;                        // 发送历史（去重、置顶、上限 50）
    QVector<MsEntry> m_msEntries;
    QHash<QString, QString> m_portMap;                // 显示 label → 设备名
    bool m_msLooping = false;
    int m_msLoopIndex = 0;
    bool m_fileSending = false;
    QByteArray m_fileData;
    qsizetype m_fileOffset = 0;
    QString m_selectedFilePath;
    QDialog* m_settingsWin = nullptr;
    bool m_suppressApply = false;    // 程序化修改串口参数时抑制"自动重开"
    bool m_programScroll = false;    // 程序性滚动期间抑制"自动勾选暂停刷新"
    qint64 m_lastStatusTs = 0;
    QString m_lastSrText;
    QString m_lastHsText;
    QString m_lastSendText;
    QString m_lastSendHex;
    bool m_sendHexPrev = false;
    QTimer* m_csPreviewTimer = nullptr;
    QTimer* m_searchTimer = nullptr;
    QTimer* m_flushTimer = nullptr;
    QTimer* m_timerSend = nullptr;
    QTimer* m_msLoopTimer = nullptr;
    QFileSystemWatcher* m_cfgWatcher = nullptr;   // 监听配置文件（快捷命令同步）
    QTimer* m_cfgSaveTimer = nullptr;             // 快捷命令保存防抖
    QVector<QVector<QWidget*>> m_msRowWidgets;   // 每行控件
    QString m_theme = QStringLiteral("light");
    QString m_winBg;
    sjj::ThemeColors m_themeC;
    int m_rxEpoch = 0;                // 串口代际号：关闭后丢弃旧信号
    UpdateChecker* m_updateChecker = nullptr;
    ExeDownloader* m_downloadThread = nullptr;
    QString m_ignoreUpdateVersion;
    QString m_pendingUpdateVersion;
    QString m_updateTmp;

    // 串口参数（与 Python 版 var_* 对应）
    QString m_databits = QStringLiteral("8");
    QString m_stopbits = QStringLiteral("1");
    QString m_parity = QStringLiteral("None");
    QString m_flow = QStringLiteral("None");
    QString m_readTimeout = QStringLiteral("50");

    // 无边框缩放
    int m_resizeMargin = 8;
    int m_resizeEdge = 0;
    QPoint m_resizeStartPos;
    QRect m_resizeStartGeo;
    QSet<QObject*> m_filterInstalled;

    enum EdgeMask { EdgeLeft = 1, EdgeRight = 2, EdgeTop = 4, EdgeBottom = 8 };
};
