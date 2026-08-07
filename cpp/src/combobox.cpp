#include "combobox.h"

#include <QResizeEvent>
#include <QStyleOptionViewItem>
#include <QStyleOptionComboBox>

StyledComboBox::StyledComboBox(QWidget* parent) : QComboBox(parent) {
    m_arrow = new QLabel(QStringLiteral("\u25BE"), this);
    m_arrow->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_arrow->setAlignment(Qt::AlignCenter);
    m_arrow->setStyleSheet(QStringLiteral("background:transparent;color:#4C4F69;font-size:14px;"));
    m_arrow->resize(18, height());
    m_arrow->move(width() - 18, 0);
    m_arrow->show();
}

void StyledComboBox::setArrowColor(const QColor& c) {
    m_arrow->setStyleSheet(QStringLiteral("background:transparent;color:%1;font-size:14px;")
                               .arg(c.name()));
    m_arrow->update();
}

void StyledComboBox::resizeEvent(QResizeEvent* e) {
    QComboBox::resizeEvent(e);
    m_arrow->resize(18, height());
    m_arrow->move(width() - 18, 0);
}

void ElideRightDelegate::initStyleOption(QStyleOptionViewItem* option,
                                         const QModelIndex& index) const {
    QStyledItemDelegate::initStyleOption(option, index);
    option->textElideMode = Qt::ElideRight;
}

PortComboBox::PortComboBox(QWidget* parent) : StyledComboBox(parent) {
    setItemDelegate(new ElideRightDelegate(this));
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
}
