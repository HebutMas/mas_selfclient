#include "ui/ActionOverlay.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "robomaster.pb.h"
#include "core/UserActions.h"

namespace rm {
namespace {

QByteArray pack(const google::protobuf::Message& msg)
{
    std::string s;
    msg.SerializeToString(&s);
    return QByteArray(s.data(), static_cast<int>(s.size()));
}

QFrame* makePanel()
{
    auto* panel = new QFrame;
    panel->setStyleSheet(QStringLiteral(
        "QFrame{background:rgba(20,20,26,235);border:1px solid rgba(255,255,255,60);"
        "border-radius:12px;}"
        "QLabel{color:#f0f0f0;}"
        "QPushButton{padding:6px 18px;}"));
    return panel;
}

} // namespace

ActionOverlay::ActionOverlay(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);

    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);
    auto* mid = new QHBoxLayout;
    mid->addStretch(1);

    // 指令界面
    m_actionPanel = makePanel();
    auto* al = new QVBoxLayout(m_actionPanel);
    al->setContentsMargins(24, 20, 24, 20);
    m_actionTitle = new QLabel;
    QFont tf = m_actionTitle->font();
    tf.setPixelSize(22);
    tf.setBold(true);
    m_actionTitle->setFont(tf);
    al->addWidget(m_actionTitle);
    auto* paramRow = new QHBoxLayout;
    m_paramLabel = new QLabel;
    m_paramSpin = new QSpinBox;
    m_paramCombo = new QComboBox;
    paramRow->addWidget(m_paramLabel);
    paramRow->addWidget(m_paramSpin);
    paramRow->addWidget(m_paramCombo);
    paramRow->addStretch(1);
    al->addLayout(paramRow);
    auto* hint = new QLabel(tr("回车确认 / Esc 取消"));
    hint->setStyleSheet(QStringLiteral("color:#9aa0a6;"));
    al->addWidget(hint);
    auto* abtns = new QHBoxLayout;
    auto* ok = new QPushButton(tr("确认发送"));
    auto* cancel = new QPushButton(tr("取消"));
    ok->setDefault(true);
    abtns->addStretch(1);
    abtns->addWidget(ok);
    abtns->addWidget(cancel);
    al->addLayout(abtns);
    connect(ok, &QPushButton::clicked, this, &ActionOverlay::confirm);
    connect(cancel, &QPushButton::clicked, this, &ActionOverlay::cancel);
    m_actionPanel->hide();

    // 死亡界面
    m_deathPanel = makePanel();
    auto* dl = new QVBoxLayout(m_deathPanel);
    dl->setContentsMargins(32, 24, 32, 24);
    auto* deathTitle = new QLabel(tr("已 被 击 毁"));
    QFont df = deathTitle->font();
    df.setPixelSize(30);
    df.setBold(true);
    deathTitle->setFont(df);
    deathTitle->setStyleSheet(QStringLiteral("color:#e23a3a;"));
    deathTitle->setAlignment(Qt::AlignCenter);
    dl->addWidget(deathTitle);
    m_deathBar = new QProgressBar;
    m_deathBar->setMinimumWidth(360);
    m_deathBar->setTextVisible(true);
    dl->addWidget(m_deathBar);
    m_deathInfo = new QLabel;
    m_deathInfo->setAlignment(Qt::AlignCenter);
    dl->addWidget(m_deathInfo);
    auto* dbtns = new QHBoxLayout;
    m_respawnFree = new QPushButton(tr("确认复活"));
    m_respawnPay = new QPushButton(tr("花费金币复活"));
    dbtns->addStretch(1);
    dbtns->addWidget(m_respawnFree);
    dbtns->addWidget(m_respawnPay);
    dbtns->addStretch(1);
    dl->addLayout(dbtns);
    connect(m_respawnFree, &QPushButton::clicked, this, [this] { doRespawn(3); });
    connect(m_respawnPay, &QPushButton::clicked, this, [this] { doRespawn(4); });
    m_deathPanel->hide();

    mid->addWidget(m_actionPanel);
    mid->addWidget(m_deathPanel);
    mid->addStretch(1);
    outer->addLayout(mid);
    outer->addStretch(1);

    hide();
}

void ActionOverlay::promptAction(const UserAction& action)
{
    m_action = &action;
    m_actionTitle->setText(QStringLiteral("%1 · %2").arg(action.group, action.label));

    const ActionParam& p = action.param;
    const bool hasParam = !p.label.isEmpty();
    m_paramLabel->setVisible(hasParam);
    m_paramSpin->setVisible(hasParam && p.choices.isEmpty());
    m_paramCombo->setVisible(hasParam && !p.choices.isEmpty());
    if (hasParam) {
        m_paramLabel->setText(p.label);
        if (p.choices.isEmpty()) {
            m_paramSpin->setRange(p.min, p.max);
            m_paramSpin->setSingleStep(qMax(1, p.step));
            m_paramSpin->setValue(p.def);
        } else {
            m_paramCombo->clear();
            for (int i = 0; i < p.choices.size(); ++i)
                m_paramCombo->addItem(p.choices[i], p.min + i);
            m_paramCombo->setCurrentIndex(qBound(0, p.def - p.min, p.choices.size() - 1));
        }
    }

    m_deathPanel->hide();
    m_actionPanel->show();
    updateVisibility();
    raise();
    setFocus(Qt::OtherFocusReason);
}

void ActionOverlay::confirm()
{
    if (!m_action)
        return;
    const UserAction& a = *m_action;
    const int value = !m_paramCombo->isHidden() ? m_paramCombo->currentData().toInt()
                                               : m_paramSpin->value();
    const QByteArray payload = buildActionPayload(a, a.param.label.isEmpty() ? 0 : value);
    m_action = nullptr;
    m_actionPanel->hide();
    updateVisibility();
    emit confirmed(a.topic, payload);
}

void ActionOverlay::cancel()
{
    m_action = nullptr;
    m_actionPanel->hide();
    updateVisibility();
}

void ActionOverlay::setData(const HudData& data)
{
    const bool dead = data.valid && (!data.self.alive || data.bottom.respawnPending);
    if (dead) {
        m_deathBar->setRange(0, qMax(1, data.bottom.respawnTotal));
        m_deathBar->setValue(data.bottom.respawnProgress);
        m_deathBar->setFormat(QStringLiteral("%1 / %2")
                                  .arg(data.bottom.respawnProgress)
                                  .arg(data.bottom.respawnTotal));
        // 免费"确认复活"必须等复活读条走完（协议 can_free_respawn 应已体现，这里再做一次保险）。
        const bool barDone = data.bottom.respawnTotal <= 0
                             || data.bottom.respawnProgress >= data.bottom.respawnTotal;
        m_respawnFree->setVisible(true);
        m_respawnFree->setEnabled(data.bottom.canFreeRespawn && barDone);
        m_respawnFree->setText(barDone ? tr("确认复活") : tr("确认复活（等待复活读条）"));
        m_deathInfo->setText(barDone ? tr("复活读条已完成，可确认复活")
                                     : tr("复活读条中 %1 / %2 …")
                                           .arg(data.bottom.respawnProgress)
                                           .arg(data.bottom.respawnTotal));
        // 花费金币复活可在读条期间跳过等待（协议 can_pay_for_respawn）。
        m_respawnPay->setVisible(data.bottom.canPayForRespawn && data.bottom.goldCostForRespawn > 0);
        m_respawnPay->setText(tr("花费 %1 金币立即复活").arg(data.bottom.goldCostForRespawn));
        m_respawnPay->setEnabled(data.bottom.canPayForRespawn);
        // 死亡界面只在没有指令界面时显示（指令界面优先）。
        if (!m_action)
            m_deathPanel->show();
    } else {
        m_deathPanel->hide();
    }
    m_dead = dead;
    updateVisibility();
}

void ActionOverlay::doRespawn(int cmdType)
{
    robomaster::CommonCommand m;
    m.set_cmd_type(static_cast<uint32_t>(cmdType));
    emit confirmed(QStringLiteral("CommonCommand"), pack(m));
}

void ActionOverlay::updateVisibility()
{
    // 注意：子控件的 isVisible() 在父控件隐藏时也为 false，这里要用 isHidden()。
    setVisible(!m_actionPanel->isHidden() || !m_deathPanel->isHidden());
}

void ActionOverlay::keyPressEvent(QKeyEvent* event)
{
    if (m_action) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
            || event->key() == Qt::Key_Space) {
            confirm();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

} // namespace rm
