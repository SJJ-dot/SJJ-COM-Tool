#include "utils.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QTextCodec>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>

namespace sjj {

QString nowTimestamp() {
    // 单调墙钟：应用启动时的墙钟基线 + 单调计时器偏移。
    // 保证时间戳连续递增，不受系统时间（NTP/手动）调整影响。
    static const qint64 startWall = QDateTime::currentMSecsSinceEpoch();
    static const QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(startWall + timer.elapsed());
    return QStringLiteral("[%1.%2]")
        .arg(dt.toString(QStringLiteral("HH:mm:ss")))
        .arg(dt.time().msec(), 3, 10, QLatin1Char('0'));
}

// ---------- UTF-16 手工编解码（QTextCodec 对 LE/BE 支持不稳定，自行实现） ----------
static QString decodeUtf16(const QByteArray& data, bool littleEndian) {
    QString out;
    out.reserve(data.size() / 2 + 1);
    const uchar* p = reinterpret_cast<const uchar*>(data.constData());
    const qsizetype n = data.size();
    for (qsizetype i = 0; i + 1 < n; i += 2) {
        quint16 u = littleEndian ? (quint16(p[i]) | (quint16(p[i + 1]) << 8))
                                 : (quint16(p[i]) << 8 | quint16(p[i + 1]));
        out.append(QChar(u));
    }
    return out;
}

static QByteArray encodeUtf16(const QString& text, bool littleEndian) {
    QByteArray out;
    out.reserve(text.size() * 2);
    for (const QChar ch : text) {
        const quint16 u = ch.unicode();
        if (littleEndian) {
            out.append(char(u & 0xFF));
            out.append(char((u >> 8) & 0xFF));
        } else {
            out.append(char((u >> 8) & 0xFF));
            out.append(char(u & 0xFF));
        }
    }
    return out;
}

QString decodeBytes(const QByteArray& data, const QString& encoding) {
    if (encoding == QStringLiteral("ASCII")) {
        QString out;
        out.reserve(data.size());
        for (const char ch : data)
            out += (uchar(ch) < 0x80) ? QChar(uchar(ch)) : QLatin1Char('?');
        return out;
    }
    if (encoding == QStringLiteral("UTF-16LE"))
        return decodeUtf16(data, true);
    if (encoding == QStringLiteral("UTF-16BE"))
        return decodeUtf16(data, false);
    QTextCodec* codec = QTextCodec::codecForName(encoding.toUtf8());
    if (!codec)
        codec = QTextCodec::codecForName("UTF-8");
    return codec->toUnicode(data);
}

QByteArray encodeString(const QString& text, const QString& encoding) {
    if (encoding == QStringLiteral("ASCII"))
        return text.toLatin1();
    if (encoding == QStringLiteral("UTF-16LE"))
        return encodeUtf16(text, true);
    if (encoding == QStringLiteral("UTF-16BE"))
        return encodeUtf16(text, false);
    QTextCodec* codec = QTextCodec::codecForName(encoding.toUtf8());
    if (!codec)
        codec = QTextCodec::codecForName("UTF-8");
    return codec->fromUnicode(text);
}

QString hexBody(const QByteArray& data) {
    // HEX 显示紧凑一行，不再在 0x0A 前插入换行：
    // CR/LF (0D 0A) 是常见协议分隔字节，被拆开会破坏接收日志的可读性。
    QString text;
    text.reserve(data.size() * 3);
    for (qsizetype i = 0; i < data.size(); ++i) {
        const uchar b = uchar(data.at(i));
        const QString hex = QStringLiteral("%1").arg(int(b), 2, 16, QLatin1Char('0')).toUpper();
        if (!text.isEmpty())
            text += QLatin1Char(' ');
        text += hex;
    }
    return text;
}

QByteArray parseHexString(const QString& raw, bool& ok, QString& err) {
    QString s = raw;
    s.remove(QLatin1Char(' ')).remove(QLatin1Char(',')).remove(QLatin1Char('\n'))
        .remove(QLatin1Char('\r')).remove(QLatin1Char('\t'));
    s.replace(QStringLiteral("0x"), QString()).replace(QStringLiteral("0X"), QString());
    if (s.length() % 2 != 0) {
        ok = false;
        err = QStringLiteral("HEX 字符串长度必须为偶数");
        return {};
    }
    ok = true;
    return QByteArray::fromHex(s.toLatin1());
}

QString spacedHex(const QByteArray& data) {
    QString out;
    out.reserve(data.size() * 3);
    for (qsizetype i = 0; i < data.size(); ++i) {
        if (i > 0)
            out += QLatin1Char(' ');
        out += QStringLiteral("%1").arg(uchar(data.at(i)), 2, 16, QLatin1Char('0')).toUpper();
    }
    return out;
}

// ---------- 校验 ----------
static quint32 crc32Impl(const QByteArray& data) {
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    const uchar* p = reinterpret_cast<const uchar*>(data.constData());
    for (qsizetype i = 0; i < data.size(); ++i)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

QByteArray computeChecksum(const QByteArray& data, const QString& algorithm) {
    if (data.isEmpty() || algorithm == QStringLiteral("None"))
        return {};

    quint32 sum = 0;
    for (const uchar b : data)
        sum += b;

    if (algorithm == QStringLiteral("0-ADD8"))
        return QByteArray(1, char((-int(sum)) & 0xFF));
    if (algorithm == QStringLiteral("ADD8"))
        return QByteArray(1, char(sum & 0xFF));
    if (algorithm == QStringLiteral("XOR8")) {
        uchar x = 0;
        for (const uchar b : data)
            x ^= b;
        return QByteArray(1, char(x));
    }
    if (algorithm == QStringLiteral("ADD16"))
        return QByteArray(1, char((sum >> 8) & 0xFF)).append(char(sum & 0xFF));
    if (algorithm == QStringLiteral("ModbusCRC16")) {
        quint16 crc = 0xFFFF;
        for (const uchar b : data) {
            crc ^= b;
            for (int i = 0; i < 8; ++i)
                crc = (crc & 1) ? quint16((crc >> 1) ^ 0xA001) : quint16(crc >> 1);
        }
        return QByteArray(1, char(crc & 0xFF)).append(char((crc >> 8) & 0xFF));
    }
    if (algorithm == QStringLiteral("CCITT-CRC16")) {
        quint16 crc = 0xFFFF;
        for (const uchar b : data) {
            crc ^= quint16(b) << 8;
            for (int i = 0; i < 8; ++i)
                crc = (crc & 0x8000) ? quint16(((crc << 1) ^ 0x1021) & 0xFFFF)
                                     : quint16((crc << 1) & 0xFFFF);
        }
        return QByteArray(1, char((crc >> 8) & 0xFF)).append(char(crc & 0xFF));
    }
    if (algorithm == QStringLiteral("CRC32")) {
        const quint32 crc = crc32Impl(data);   // 小端输出，与 Python struct.pack("<I") 一致
        return QByteArray(1, char(crc & 0xFF))
            .append(char((crc >> 8) & 0xFF))
            .append(char((crc >> 16) & 0xFF))
            .append(char((crc >> 24) & 0xFF));
    }
    return {};
}

QByteArray checksumRange(const QByteArray& data, int start, int endOff) {
    if (start < 1)
        start = 1;
    if (endOff > 0)
        endOff = 0;
    const qsizetype lo = start - 1;
    qsizetype hi = data.size();
    if (endOff < 0)
        hi = data.size() + endOff;   // 老实排除末尾 N 字节
    if (hi > data.size())
        hi = data.size();
    if (hi <= lo)
        return {};
    return data.mid(lo, hi - lo);
}

bool versionIsNewer(const QString& latest, const QString& current) {
    const auto tuple = [](const QString& v) {
        QVector<int> parts;
        QString s = v.trimmed();
        while (s.startsWith(QLatin1Char('v')) || s.startsWith(QLatin1Char('V')))
            s.remove(0, 1);
        const QStringList segs = s.split(QRegularExpression(QStringLiteral("[.\\-_]")),
                                         Qt::SkipEmptyParts);
        for (const QString& seg : segs) {
            bool ok = false;
            const int n = seg.toInt(&ok);
            if (!ok)
                break;
            parts.append(n);
        }
        while (parts.size() < 3)
            parts.append(0);
        return QVector<int>(parts.cbegin(), parts.cbegin() + 3);
    };
    const QVector<int> a = tuple(latest);
    const QVector<int> b = tuple(current);
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i])
            return a[i] > b[i];
    }
    return false;
}

QIcon makeSunIcon(const QColor& color, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QPointF(size / 2.0, size / 2.0), size * 0.38, size * 0.38);
    p.end();
    return QIcon(pm);
}

QIcon makeMoonIcon(const QColor& color, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath full;
    full.addEllipse(QRectF(size * 0.25, size * 0.15, size * 0.66, size * 0.66));
    QPainterPath mask;
    mask.addEllipse(QRectF(size * 0.44, size * 0.30, size * 0.60, size * 0.60));
    p.fillPath(full.subtracted(mask), color);
    p.end();
    return QIcon(pm);
}

} // namespace sjj
