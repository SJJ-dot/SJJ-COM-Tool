#include "themechkbox.h"

#include <QPainter>
#include <QStyle>
#include <QStyleOptionButton>

ThemeCheckBox::ThemeCheckBox(const QString& text, QWidget* parent)
    : QCheckBox(text, parent) {}

void ThemeCheckBox::setThemeColors(const sjj::ThemeColors& t) {
    m_tc = t;
    refreshStyle();
}

void ThemeCheckBox::refreshStyle() {
    if (m_tc.isEmpty())
        return;
    const QString& primary = m_tc.value(QStringLiteral("text_primary"));
    const QString& border = m_tc.value(QStringLiteral("input_border"));
    const QString& bg = m_tc.value(QStringLiteral("input_bg"));
    const QString& accent = m_tc.value(QStringLiteral("accent"));
    // checked 状态背景 = accent；unchecked = input_bg / input_border
    setStyleSheet(QStringLiteral(
        "QCheckBox{color:%1;background:transparent;spacing:4px;}"
        "QCheckBox::indicator{width:18px;height:18px;border:1px solid %2;"
        "border-radius:3px;background:%3;}"
        "QCheckBox::indicator:hover{border-color:%4;}"
        "QCheckBox::indicator:checked{background-color:%4;border-color:%4;}")
        .arg(primary, border, bg, accent));
}

void ThemeCheckBox::paintEvent(QPaintEvent* event) {
    // QSS ::indicator 渲染 indicator 容器（背景+边框），再手动绘制白色对勾
    QCheckBox::paintEvent(event);
    if (!isChecked() || m_tc.isEmpty())
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QStyleOptionButton opt;
    initStyleOption(&opt);
    const QRect ir = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, this);
    QPen pen(Qt::white);
    pen.setWidth(2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    const qreal x = ir.x(), y = ir.y(), w = ir.width(), h = ir.height();
    painter.drawLine(QPointF(x + w * 0.2, y + h * 0.55),
                     QPointF(x + w * 0.45, y + h * 0.75));
    painter.drawLine(QPointF(x + w * 0.45, y + h * 0.75),
                     QPointF(x + w * 0.8, y + h * 0.25));
}
