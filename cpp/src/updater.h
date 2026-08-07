#pragma once
// 后台检查 GitHub Releases 最新版本（不阻塞 UI）；失败时发出空 info。
// ExeDownloader：后台多线程下载最新版 exe（HTTP Range 分块，默认 5 线程），
// 服务器不支持 Range 时自动回退单线程；支持进度与取消。

#include <QObject>
#include <QThread>
#include <QVariantMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QElapsedTimer>
#include <atomic>

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    void check(bool manual);   // manual=true 时失败会提示（由 UI 层决定）

signals:
    // info 为空 → 检查失败（网络不可用等）
    void resultReady(const QVariantMap& info, bool manual);

private slots:
    void onFinished();

private:
    QNetworkAccessManager m_nam;
    QNetworkReply* m_reply = nullptr;
    bool m_manual = false;
};

class ExeDownloader : public QThread {
    Q_OBJECT
public:
    explicit ExeDownloader(const QString& url, int threads = 5, QObject* parent = nullptr);
    ~ExeDownloader() override;

    void cancel();

signals:
    void finishedOk(const QString& tmpPath);          // 临时文件路径
    void failed(const QString& err);                  // 错误信息
    void progress(qint64 done, qint64 total, int speedKbps); // 已下载/总字节(-1 未知)/速度

protected:
    void run() override;

private:
    qint64 checkRangeSupport();
    void downloadSingle(const QString& tmp, qint64 total);
    void downloadMulti(const QString& tmp, qint64 total);

    QString m_url;
    int m_threads = 5;
    std::atomic<bool> m_cancel{false};
};
