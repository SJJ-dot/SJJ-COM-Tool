#include "updater.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThreadPool>
#include <QRunnable>
#include <QMutex>
#include <QAtomicInteger>
#include <QRegularExpression>
#include <QUrl>

#include "utils.h"

// ================= UpdateChecker =================

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent) {}

void UpdateChecker::check(bool manual) {
    if (m_reply)
        return;   // 上一次检查还在进行
    m_manual = manual;
    QNetworkRequest req{QUrl(sjj::UPDATE_CHECK_URL)};
    req.setRawHeader("User-Agent", QStringLiteral("SuperCOM/%1").arg(APP_VERSION).toUtf8());
    req.setRawHeader("Accept", "application/vnd.github+json");
    m_reply = m_nam.get(req);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::onFinished);
}

void UpdateChecker::onFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;
    m_reply = nullptr;
    bool manual = m_manual;
    QVariantMap info;
    if (reply->error() == QNetworkReply::NoError) {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject o = doc.object();
        QString tag = o.value(QStringLiteral("tag_name")).toString();
        if (!tag.isEmpty()) {
            QString latest = tag;
            latest.remove(QRegularExpression(QStringLiteral("^[vV]")));
            info[QStringLiteral("latest")] = latest;
            info[QStringLiteral("tag")] = tag;
            QString html = o.value(QStringLiteral("html_url")).toString();
            if (html.isEmpty())
                html = QStringLiteral("https://github.com/%1/releases").arg(sjj::GITHUB_REPO);
            info[QStringLiteral("html_url")] = html;
            info[QStringLiteral("name")] = o.value(QStringLiteral("name")).toString();
            info[QStringLiteral("body")] = o.value(QStringLiteral("body")).toString();
            // release 附件里的 exe 下载地址（自动更新用）
            QString exeUrl;
            const QJsonArray assets = o.value(QStringLiteral("assets")).toArray();
            for (const QJsonValue& v : assets) {
                const QJsonObject a = v.toObject();
                if (a.value(QStringLiteral("name")).toString().toLower()
                    == QStringLiteral("supercom.exe")) {
                    exeUrl = a.value(QStringLiteral("browser_download_url")).toString();
                    break;
                }
            }
            info[QStringLiteral("exe_url")] = exeUrl;
        }
    }
    reply->deleteLater();
    emit resultReady(info, manual);
}

// ================= ExeDownloader =================

ExeDownloader::ExeDownloader(const QString& url, int threads, QObject* parent)
    : QThread(parent), m_url(url), m_threads(threads > 0 ? threads : 1) {}

ExeDownloader::~ExeDownloader() {
    if (isRunning()) {
        m_cancel = true;
        wait(3000);
    }
}

void ExeDownloader::cancel() {
    m_cancel = true;
}

// 探测服务器是否支持 Range；支持返回 Content-Length，否则 -1
qint64 ExeDownloader::checkRangeSupport() {
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(m_url)};
    req.setRawHeader("User-Agent", QStringLiteral("SuperCOM/%1").arg(APP_VERSION).toUtf8());
    req.setRawHeader("Range", "bytes=0-0");
    QNetworkReply* reply = nam.get(req);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 206) {
        reply->deleteLater();
        return -1;
    }
    // Content-Range: bytes 0-0/123456 → 取总大小
    const QByteArray cr = reply->rawHeader("Content-Range");
    reply->deleteLater();
    const int slash = cr.lastIndexOf('/');
    if (slash >= 0 && slash + 1 < cr.size())
        return cr.mid(slash + 1).toLongLong();
    return -1;
}

// 单线程下载（Range 不支持 / threads<=1 回退）
void ExeDownloader::downloadSingle(const QString& tmp, qint64 total) {
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(m_url)};
    req.setRawHeader("User-Agent", QStringLiteral("SuperCOM/%1").arg(APP_VERSION).toUtf8());
    QNetworkReply* reply = nam.get(req);
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly))
        throw QStringLiteral("无法创建文件: ") + tmp;
    qint64 done = 0;
    QElapsedTimer timer;
    timer.start();
    QEventLoop loop;
    connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        if (m_cancel) {
            reply->abort();
            return;
        }
        const QByteArray chunk = reply->readAll();
        f.write(chunk);
        done += chunk.size();
        const qint64 elapsed = qMax<qint64>(1, timer.elapsed());
        emit progress(done, total, qMax<qint64>(0, done * 1000 / elapsed / 1024));
    });
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    f.close();
    if (reply->error() != QNetworkReply::NoError && !m_cancel)
        throw reply->errorString();
    if (m_cancel)
        throw QStringLiteral("下载已取消");
    reply->deleteLater();
}

// 多线程 Range 分块下载
namespace {

class ChunkWorker : public QRunnable {
public:
    ChunkWorker(const QString& url, qint64 start, qint64 end, const QString& tmp,
                QAtomicInteger<qint64>* done, QAtomicInteger<bool>* cancel,
                QString* firstError, QMutex* errMutex)
        : m_url(url), m_start(start), m_end(end), m_tmp(tmp),
          m_done(done), m_cancel(cancel), m_firstError(firstError), m_errMutex(errMutex) {}

    void run() override {
        QNetworkAccessManager nam;
        QNetworkRequest req{QUrl(m_url)};
        req.setRawHeader("User-Agent", QStringLiteral("SuperCOM/%1").arg(APP_VERSION).toUtf8());
        req.setRawHeader("Range", QStringLiteral("bytes=%1-%2").arg(m_start).arg(m_end).toUtf8());
        QNetworkReply* reply = nam.get(req);
        QFile f(m_tmp);
        if (!f.open(QIODevice::ReadWrite)) {
            setError(QStringLiteral("无法打开临时文件: ") + m_tmp);
            reply->deleteLater();
            return;
        }
        f.seek(m_start);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
            if (m_cancel->loadRelaxed()) {
                reply->abort();
                return;
            }
            const QByteArray chunk = reply->readAll();
            f.write(chunk);
            m_done->fetchAndAddRelaxed(qint64(chunk.size()));
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        f.close();
        if (reply->error() != QNetworkReply::NoError && !m_cancel->loadRelaxed())
            setError(reply->errorString());
        reply->deleteLater();
    }

private:
    void setError(const QString& msg) {
        QMutexLocker locker(m_errMutex);
        if (m_firstError->isEmpty())
            *m_firstError = msg;
    }

    QString m_url;
    qint64 m_start = 0;
    qint64 m_end = 0;
    QString m_tmp;
    QAtomicInteger<qint64>* m_done = nullptr;
    QAtomicInteger<bool>* m_cancel = nullptr;
    QString* m_firstError = nullptr;
    QMutex* m_errMutex = nullptr;
};

} // namespace

void ExeDownloader::downloadMulti(const QString& tmp, qint64 total) {
    const int n = int(qMax<qint64>(1, qMin<qint64>(m_threads, total / (256 * 1024) + 1)));
    const qint64 chunk = total / n;
    // 预分配文件（r+b 按偏移写入）
    {
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly))
            throw QStringLiteral("无法创建文件: ") + tmp;
        f.resize(total);
    }
    QAtomicInteger<qint64> done{0};
    QAtomicInteger<bool> cancelFlag{false};
    QString firstError;
    QMutex errMutex;
    QThreadPool pool;
    pool.setMaxThreadCount(n);
    for (int i = 0; i < n; ++i) {
        const qint64 start = i * chunk;
        const qint64 end = (i == n - 1) ? total - 1 : (i + 1) * chunk - 1;
        pool.start(new ChunkWorker(m_url, start, end, tmp, &done, &cancelFlag,
                                   &firstError, &errMutex));
    }
    QElapsedTimer timer;
    timer.start();
    // 等待所有分块完成（期间汇总进度；取消时置 cancelFlag 让分块线程退出）
    while (!pool.waitForDone(50)) {
        if (m_cancel) {
            cancelFlag = true;
            break;
        }
        const qint64 elapsed = qMax<qint64>(1, timer.elapsed());
        const qint64 d = done.loadRelaxed();
        emit progress(d, total, qMax<qint64>(0, d * 1000 / elapsed / 1024));
    }
    pool.waitForDone(100);
    if (m_cancel)
        throw QStringLiteral("下载已取消");
    if (!firstError.isEmpty())
        throw firstError;
    emit progress(done.loadRelaxed(), total, 0);
}

void ExeDownloader::run() {
    const QString tmp = QCoreApplication::applicationDirPath()
        + QStringLiteral("/SuperCOM.update.exe");
    try {
        const qint64 total = checkRangeSupport();
        if (total > 0 && m_threads > 1 && total > 2 * 1024 * 1024) {
            // 服务器支持 Range 且文件 > 2MB 才多线程（小文件单线程更快）
            downloadMulti(tmp, total);
        } else {
            downloadSingle(tmp, total);
        }
        if (QFileInfo(tmp).size() == 0)
            throw QStringLiteral("下载文件为空");
        emit finishedOk(tmp);
    } catch (const QString& e) {
        if (m_cancel)
            emit failed(QStringLiteral("下载已取消"));
        else
            emit failed(e);
        QFile::remove(tmp);   // 清理半成品临时文件
    } catch (...) {
        emit failed(QStringLiteral("未知错误"));
        QFile::remove(tmp);
    }
}
