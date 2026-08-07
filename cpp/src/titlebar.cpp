#include "titlebar.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QScreen>

#include "utils.h"

TitleBar::TitleBar(QWidget* window) : QWidget(window), m_win(window) {
    // QWidget 子类必须设置 WA_StyledBackground，QSS 背景色才会绘制
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(30);
    setObjectName(QStringLiteral("titleBar"));
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(8, 0, 2, 0);
    h->setSpacing(2);

    // 图标
    m_lblIcon = new QLabel(this);
    const QIcon ic = window->windowIcon();
    if (!ic.isNull())
        m_lblIcon->setPixmap(ic.pixmap(18, 18));
    m_lblIcon->setFixedSize(18, 18);
    h->addWidget(m_lblIcon);

    // 标题 + 版本号（剥掉前导 v 再拼，避免 "vv1.0.3"）
    m_lblTitle = new QLabel(this);
    QString ver = QString::fromUtf8(APP_VERSION);
    while (ver.startsWith(QLatin1Char('v')) || ver.startsWith(QLatin1Char('V')))
        ver.remove(0, 1);
    m_lblTitle->setText(QStringLiteral("%1  v%2").arg(sjj::APP_TITLE, ver));
    m_lblTitle->setStyleSheet(QStringLiteral("padding-left:4px;"));
    m_lblTitle->setCursor(Qt::PointingHandCursor);
    m_lblTitle->setToolTip(QStringLiteral("点击检查更新"));
    h->addWidget(m_lblTitle);
    h->addStretch(1);

    // 按钮：主题切换 → 最小化 → 最大化 → 关闭
    m_btnTheme = new QToolButton(this);
    m_btnTheme->setObjectName(QStringLiteral("btn_theme"));
    m_btnTheme->setIconSize(QSize(14, 14));
    m_btnTheme->setToolTip(QStringLiteral("切换到深色模式"));
    connect(m_btnTheme, &QToolButton::clicked, this, &TitleBar::themeClicked);
    m_btnMin = new QToolButton(this);
    m_btnMin->setText(QStringLiteral("\u2014"));
    m_btnMin->setToolTip(QStringLiteral("最小化"));
    m_btnMax = new QToolButton(this);
    m_btnMax->setText(QStringLiteral("\u25A1"));
    m_btnMax->setToolTip(QStringLiteral("最大化"));
    m_btnClose = new QToolButton(this);
    m_btnClose->setText(QStringLiteral("\u2715"));
    m_btnClose->setObjectName(QStringLiteral("btn_close"));
    m_btnClose->setToolTip(QStringLiteral("关闭"));
    for (auto* b : {m_btnTheme, m_btnMin, m_btnMax, m_btnClose}) {
        b->setFixedSize(30, 30);
        h->addWidget(b);
    }
    // 窗口控制按钮
    connect(m_btnMin, &QToolButton::clicked, m_win, &QWidget::showMinimized);
    connect(m_btnMax, &QToolButton::clicked, this, &TitleBar::toggleMax);
    connect(m_btnClose, &QToolButton::clicked, m_win, &QWidget::close);
}

void TitleBar::applyTheme(const sjj::ThemeColors& t, const QString& themeName) {
    // 标题栏配色由全局 QSS #titleBar 选择器控制；此处仅更新主题切换按钮图标
    const QColor iconColor(t.value(QStringLiteral("titlebar_fg")));
    if (themeName == QStringLiteral("dark")) {   // 当前深色 → 显示亮色太阳（点击切回浅色）
        m_btnTheme->setIcon(sjj::makeSunIcon(iconColor));
        m_btnTheme->setToolTip(QStringLiteral("切换到浅色模式"));
    } else {                                     // 当前浅色 → 显示深色月亮（点击切到深色）
        m_btnTheme->setIcon(sjj::makeMoonIcon(iconColor));
        m_btnTheme->setToolTip(QStringLiteral("切换到深色模式"));
    }
}

void TitleBar::toggleMax() {
    if (m_win->isMaximized()) {
        m_win->showNormal();
        m_btnMax->setText(QStringLiteral("\u25A1"));
        m_btnMax->setToolTip(QStringLiteral("最大化"));
    } else {
        m_win->showMaximized();
        m_btnMax->setText(QStringLiteral("\u2750"));
        m_btnMax->setToolTip(QStringLiteral("还原"));
    }
}

void TitleBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        QWidget* child = childAt(e->position().toPoint());
        // 版本号/图标区域不启动拖动（点击版本号用于检查更新）
        if (child == m_lblTitle || child == m_lblIcon) {
            QWidget::mousePressEvent(e);
            return;
        }
        m_dragPos = e->globalPosition().toPoint() - m_win->frameGeometry().topLeft();
        m_pressed = true;
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void TitleBar::mouseMoveEvent(QMouseEvent* e) {
    if (m_pressed && !m_dragPos.isNull() && !m_win->isMaximized()) {
        QPoint newPos = e->globalPosition().toPoint() - m_dragPos;
        // 限制拖动范围：标题栏必须在屏幕内（否则无法抓住拖回）
        const QRect screen = m_win->screen()->availableGeometry();
        const QRect frame = m_win->frameGeometry();
        if (newPos.y() < screen.top())
            newPos.setY(screen.top());
        const int minX = screen.left() - frame.width() + 120;  // 至少保留 120px 可见
        const int maxX = screen.right() - 120;
        if (newPos.x() < minX)
            newPos.setX(minX);
        if (newPos.x() > maxX)
            newPos.setX(maxX);
        m_win->move(newPos);
        e->accept();
        return;
    }
    QWidget::mouseMoveEvent(e);
}

void TitleBar::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        // 版本号区域点击且本次未拖动 → 手动检查更新
        if (!m_pressed && m_lblTitle->geometry().contains(e->position().toPoint())) {
            emit versionClicked();
            e->accept();
            return;
        }
        m_pressed = false;
        m_dragPos = QPoint();
        QWidget::mouseReleaseEvent(e);
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        toggleMax();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void TitleBar::contextMenuEvent(QContextMenuEvent* e) {
    QMenu m(this);
    m.addAction(QStringLiteral("最小化"), m_win, &QWidget::showMinimized);
    m.addAction(m_win->isMaximized() ? QStringLiteral("还原") : QStringLiteral("最大化"),
                this, &TitleBar::toggleMax);
    m.addSeparator();
    m.addAction(QStringLiteral("关闭"), m_win, &QWidget::close);
    m.exec(e->globalPos());
}
