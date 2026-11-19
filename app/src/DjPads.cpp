// Performance-Pads wie bei rekordbox: Pad-Modi, 16 Pad-FX (2 Seiten),
// Stems auf Pads (Mute + Solo zum Halten) und Stems auf dem CFX-Regler.
#include <QSettings>

#include "DjController.h"

using namespace loggx;

namespace {
struct PadFxDefault {
    DjFx fx;
    double beats;
    double wet;
};
// Seite 1 und 2 wie in rekordbox (Slip Loop = Roll, V.Brake = Bremse)
const PadFxDefault kPadFxDefaults[DjController::kPadFxSlots] = {
    {DjFx::Roll, 0.125, 1.0}, {DjFx::Roll, 0.25, 1.0}, {DjFx::Roll, 0.5, 1.0}, {DjFx::Roll, 1, 1.0},
    {DjFx::Reverb, 1, 0.5},   {DjFx::Echo, 0.75, 0.6}, {DjFx::Flanger, 8, 0.7}, {DjFx::Brake, 0.75, 1.0},
    {DjFx::Trans, 0.5, 1.0},  {DjFx::Crush, 1, 0.6},   {DjFx::Filter, 4, 0.9},  {DjFx::Backspin, 4, 1.0},
    {DjFx::PingPong, 0.125, 0.6}, {DjFx::Echo, 0.5, 0.8}, {DjFx::Phaser, 4, 0.8}, {DjFx::Echo, 1, 0.6},
};
}  // namespace

const QStringList& DjController::fxNames() {
    static const QStringList names = {tr("Echo"), tr("Hall"), tr("Bremse"), tr("Ping-Pong"), tr("Flanger"), tr("Phaser"),
                                      tr("Trans"), tr("Slip Loop"), tr("Filter LFO"), tr("Rauschen"), tr("Crush"), tr("Backspin")};
    return names;
}

QStringList DjController::fxNameList() const { return fxNames(); }

void DjController::loadPadFx() {
    QSettings s;
    for (int i = 0; i < kPadFxSlots; ++i) {
        const auto& d = kPadFxDefaults[i];
        m_padFx[static_cast<std::size_t>(i)] = {
            std::clamp(s.value(QStringLiteral("dj/padFx/%1/type").arg(i), static_cast<int>(d.fx)).toInt(), 0, kDjFxCount - 1),
            s.value(QStringLiteral("dj/padFx/%1/beats").arg(i), d.beats).toDouble(),
            s.value(QStringLiteral("dj/padFx/%1/wet").arg(i), d.wet).toDouble()};
    }
    m_stemPads = std::clamp(s.value(QStringLiteral("dj/flx4StemPads"), 0).toInt(), 0, 3);
    m_cfxMode = std::clamp(s.value(QStringLiteral("dj/cfxMode"), 0).toInt(), 0, 1);
}

QVariantList DjController::padFxSlots() const {
    QVariantList list;
    for (int i = 0; i < kPadFxSlots; ++i) {
        const PadFx& p = m_padFx[static_cast<std::size_t>(i)];
        QString beats;
        if (p.type == static_cast<int>(DjFx::Reverb) || p.type == static_cast<int>(DjFx::Noise) || p.type == static_cast<int>(DjFx::Crush))
            beats = QString::number(qRound(p.wet * 100));
        else if (p.beats < 1)
            beats = QStringLiteral("1/%1").arg(qRound(1.0 / p.beats));
        else if (std::abs(p.beats - 0.75) < 1e-6)
            beats = QStringLiteral("3/4");
        else
            beats = QString::number(p.beats);
        list << QVariantMap{{QStringLiteral("type"), p.type},
                            {QStringLiteral("name"), fxNames().value(p.type).toUpper()},
                            {QStringLiteral("beats"), p.beats},
                            {QStringLiteral("wet"), p.wet},
                            {QStringLiteral("value"), beats},
                            {QStringLiteral("release"), p.type == static_cast<int>(DjFx::Brake) || p.type == static_cast<int>(DjFx::Backspin)},
                            {QStringLiteral("active"), m_padFxHeld == i}};
    }
    return list;
}

void DjController::setPadFxSlot(int slot, int type, double beats, double wet) {
    if (slot < 0 || slot >= kPadFxSlots) return;
    PadFx& p = m_padFx[static_cast<std::size_t>(slot)];
    p = {std::clamp(type, 0, kDjFxCount - 1), std::clamp(beats, 0.125, 8.0), std::clamp(wet, 0.0, 1.0)};
    QSettings s;
    s.setValue(QStringLiteral("dj/padFx/%1/type").arg(slot), p.type);
    s.setValue(QStringLiteral("dj/padFx/%1/beats").arg(slot), p.beats);
    s.setValue(QStringLiteral("dj/padFx/%1/wet").arg(slot), p.wet);
    emit padsChanged();
}

void DjController::resetPadFx() {
    QSettings().remove(QStringLiteral("dj/padFx"));
    loadPadFx();
    emit padsChanged();
}

void DjController::padFx(int deck, int slot, bool press) {
    if (slot < 0 || slot >= kPadFxSlots) return;
    ensureAudio();
    if (press) {
        // Beat-FX merken und nach dem Loslassen wiederherstellen
        if (m_padFxHeld < 0) m_padFxPrev = {m_fxType, m_fxBeats, m_fxWet, m_fxTarget, m_mixer.fxOn()};
        if (m_padFxHeld >= 0) m_mixer.setFxOn(false);
        const PadFx& p = m_padFx[static_cast<std::size_t>(slot)];
        m_padFxHeld = slot;
        m_mixer.setFx(static_cast<DjFx>(p.type), p.beats, p.wet, std::clamp(deck, 0, 1));
        m_mixer.setFxOn(true);
    } else if (m_padFxHeld == slot) {
        m_padFxHeld = -1;
        // Bremse/Backspin laufen zu Ende (Deck stoppt), alles andere endet sofort
        const int t = m_padFx[static_cast<std::size_t>(slot)].type;
        if (t != static_cast<int>(DjFx::Brake) && t != static_cast<int>(DjFx::Backspin)) m_mixer.setFxOn(false);
        m_mixer.setFx(static_cast<DjFx>(m_padFxPrev.type), m_padFxPrev.beats, m_padFxPrev.wet, m_padFxPrev.target);
        if (m_padFxPrev.on) m_mixer.setFxOn(true);
    }
    emit padsChanged();
    emit decksChanged();
}

// ------------------------------------------------------------ Stems

void DjController::stemSolo(int deck, int stem, bool press) {
    deck = std::clamp(deck, 0, 1);
    stem = std::clamp(stem, 0, 3);
    auto& saved = m_stemSolo[static_cast<std::size_t>(deck)];
    if (press) {
        if (saved.stem < 0)
            for (int s = 0; s < 4; ++s) saved.on[static_cast<std::size_t>(s)] = m_mixer.stem(deck, static_cast<DjStem>(s));
        saved.stem = stem;
        for (int s = 0; s < 4; ++s) m_mixer.setStem(deck, static_cast<DjStem>(s), s == stem);
    } else if (saved.stem == stem) {
        for (int s = 0; s < 4; ++s) m_mixer.setStem(deck, static_cast<DjStem>(s), saved.on[static_cast<std::size_t>(s)]);
        saved.stem = -1;
    }
    emit decksChanged();
}

void DjController::setStemFx(int deck, double x) {
    // −1 … 0: Richtung Acapella (Schlagzeug, Bass, Instrumente leiser)
    //  0 … 1: Richtung Instrumental (Gesang leiser)
    deck = std::clamp(deck, 0, 1);
    x = std::abs(x) < 0.03 ? 0.0 : std::clamp(x, -1.0, 1.0);
    const double others = x < 0 ? 1.0 + x : 1.0;
    const double vocal = x > 0 ? 1.0 - x : 1.0;
    m_mixer.setStemLevel(deck, DjStem::Vocals, vocal);
    for (DjStem s : {DjStem::Drums, DjStem::Bass, DjStem::Inst}) m_mixer.setStemLevel(deck, s, others);
    m_stemFx[static_cast<std::size_t>(deck)] = x;
    emit mixerChanged();
}

double DjController::stemFx(int deck) const { return m_stemFx[static_cast<std::size_t>(std::clamp(deck, 0, 1))]; }

void DjController::setStemPads(int mode) {
    m_stemPads = std::clamp(mode, 0, 3);
    QSettings().setValue(QStringLiteral("dj/flx4StemPads"), m_stemPads);
    emit settingsChanged();
}

void DjController::setCfxMode(int mode) {
    m_cfxMode = std::clamp(mode, 0, 1);
    QSettings().setValue(QStringLiteral("dj/cfxMode"), m_cfxMode);
    if (m_cfxMode == 0)
        for (int d = 0; d < 2; ++d) setStemFx(d, 0.0);
    emit settingsChanged();
}

void DjController::showPadMode(int deck, int mode) { emit padModeHint(std::clamp(deck, 0, 1), mode); }

bool DjController::stemsReady(int deck) const {
    const auto t = m_mixer.track(std::clamp(deck, 0, 1));
    return t && t->hasStems;
}

int DjController::savedPadMode(int deck) const {
    return QSettings().value(QStringLiteral("dj/padMode%1").arg(deck), 0).toInt();
}

void DjController::savePadMode(int deck, int mode, int page) {
    QSettings().setValue(QStringLiteral("dj/padMode%1").arg(deck), mode + (page > 0 ? 10 : 0));
}
