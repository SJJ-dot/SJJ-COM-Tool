#pragma once
// 通用工具：常量、编码转换、HEX 解析/显示、校验算法、版本比较、主题图标。

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QIcon>
#include <QColor>
#include <QPair>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

namespace sjj {

// ============ 常量 ============
constexpr int MAX_RECORDS = 5000;          // 接收记录上限

inline const QString APP_TITLE = QStringLiteral("SJJ\u2011COM Tool"); // U+2011 非断连字符
inline const QString GITHUB_URL = QStringLiteral("https://github.com/SJJ-dot/SJJ-COM-Tool.git");
inline const QString GITHUB_REPO = QStringLiteral("SJJ-dot/SJJ-COM-Tool");
inline const QString UPDATE_CHECK_URL =
    QStringLiteral("https://api.github.com/repos/SJJ-dot/SJJ-COM-Tool/releases/latest");

inline const QStringList DEFAULT_BAUDRATES = {
    QStringLiteral("1200"), QStringLiteral("2400"), QStringLiteral("4800"),
    QStringLiteral("9600"), QStringLiteral("14400"), QStringLiteral("19200"),
    QStringLiteral("38400"), QStringLiteral("57600"), QStringLiteral("115200"),
    QStringLiteral("128000"), QStringLiteral("230400"), QStringLiteral("256000"),
    QStringLiteral("460800"), QStringLiteral("500000"), QStringLiteral("921600"),
    QStringLiteral("1000000"), QStringLiteral("1500000"), QStringLiteral("2000000"),
};
inline const QStringList DEFAULT_PARITY = {
    QStringLiteral("None"), QStringLiteral("Even"), QStringLiteral("Odd"),
    QStringLiteral("Mark"), QStringLiteral("Space"),
};
inline const QStringList DEFAULT_STOPBITS = {
    QStringLiteral("1"), QStringLiteral("1.5"), QStringLiteral("2"),
};
inline const QStringList DEFAULT_DATABITS = {
    QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8"),
};
inline const QStringList DEFAULT_FLOW = {
    QStringLiteral("None"), QStringLiteral("RTS/CTS"), QStringLiteral("XON/XOFF"),
};
inline const QStringList CHECKSUMS = {
    QStringLiteral("None"), QStringLiteral("0-ADD8"), QStringLiteral("ADD8"),
    QStringLiteral("XOR8"), QStringLiteral("ADD16"), QStringLiteral("ModbusCRC16"),
    QStringLiteral("CCITT-CRC16"), QStringLiteral("CRC32"),
};
inline const QStringList ENCODINGS = {
    QStringLiteral("UTF-8"), QStringLiteral("GBK"), QStringLiteral("GB2312"),
    QStringLiteral("ASCII"), QStringLiteral("latin-1"),
    QStringLiteral("UTF-16LE"), QStringLiteral("UTF-16BE"),
};

// 流控短名（状态栏显示用）
inline QString flowShort(const QString& flow) {
    if (flow == QStringLiteral("RTS/CTS")) return QStringLiteral("RTS");
    if (flow == QStringLiteral("XON/XOFF")) return QStringLiteral("XON");
    return QStringLiteral("None");
}

// ============ 时间戳 ============
// 格式 [HH:MM:SS.mmm]
QString nowTimestamp();

// ============ 编码 ============
// 按指定编码把字节解码为文本（无效字节替换，不抛异常）
QString decodeBytes(const QByteArray& data, const QString& encoding);
// 按指定编码把文本编码为字节
QByteArray encodeString(const QString& text, const QString& encoding);

// ============ HEX ============
// HEX 显示：每字节两位大写十六进制，空格分隔；
// 0x0A 字节前插入换行保持数据包边界（与 Python 版逻辑一致）
QString hexBody(const QByteArray& data);
// 解析发送框 HEX 文本（去空格/逗号/换行/0x 前缀；偶数长度校验）
// 失败时 ok=false 且 err 含中文错误信息
QByteArray parseHexString(const QString& raw, bool& ok, QString& err);
// 字节数组 → "AB CD" 大写空格分隔
QString spacedHex(const QByteArray& data);

// ============ 校验 ============
// 计算校验：algorithm 取值见 CHECKSUMS；返回校验字节（空表示 None/无数据）
QByteArray computeChecksum(const QByteArray& data, const QString& algorithm);
// 计算校验范围：start 第 N 字节起（1 起，<1 视为 1）；endOff 负数=末尾偏移 N 字节（>0 视为 0）
QByteArray checksumRange(const QByteArray& data, int start, int endOff);

// ============ 版本 ============
// latest 是否比 current 新（dev 视为 0.0.0）
bool versionIsNewer(const QString& latest, const QString& current);

// ============ 主题图标（手绘，避免依赖 QtSvg） ============
QIcon makeSunIcon(const QColor& color, int size = 14);   // 亮色实心圆
QIcon makeMoonIcon(const QColor& color, int size = 14);  // 月亮

} // namespace sjj
