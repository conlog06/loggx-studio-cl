#include "DjMidi.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QSettings>

#include "DjController.h"

#ifdef LOGGX_WITH_RTMIDI
#if __has_include(<RtMidi.h>)
#include <RtMidi.h>
#else
#include <rtmidi/RtMidi.h>
#endif
#endif

using namespace loggx;

namespace {

// Pioneer DDJ-FLX4 (MIDI-Nummern aus der Zuordnung des freien DJ-Programms
// Mixxx, eigene Umsetzung). Kanal 1/2 = Deck 1/2, Kanal 7 = Mixer/Browser,
// Pads: Deck 1 = Kanal 8 (Shift 9), Deck 2 = Kanal 10 (Shift 11).
constexpr int kNote = 0x90, kCc = 0xB0;
constexpr unsigned char kKeepAlive[] = {0xF0, 0x00, 0x40, 0x05, 0x00, 0x00, 0x04, 0x05, 0x00, 0x50, 0x02, 0xF7};
constexpr double kJogSecondsPerTick = 1.8 / 720.0;   // 33⅓ U/min, 720 Schritte je Umdrehung
const double kBeatJumpPads[8] = {-1, 1, -2, 2, -4, 4, -8, 8};
const double kLoopPads[8] = {0.25, 0.5, 1, 2, 4, 8, 16, 32};
const double kTempoRanges[4] = {6, 10, 16, 25};

bool isFlx4Name(const QString& name) { return name.contains(QLatin1String("DDJ-FLX4"), Qt::CaseInsensitive); }

// Gemeinsamer Teil des Portnamens (z. B. „DDJ-FLX4 MIDI 1“ ↔ „DDJ-FLX4“)
QString portKey(QString name) {
    name = name.trimmed();
    const int colon = name.indexOf(QLatin1Char(':'));   // ALSA: „DDJ-FLX4:DDJ-FLX4 MIDI 1 24:0“
    if (colon > 0) name = name.left(colon);
    return name;
}

}  // namespace

struct DjMidi::Io {
#ifdef LOGGX_WITH_RTMIDI
    std::unique_ptr<RtMidiIn> in;
    std::unique_ptr<RtMidiOut> out;
#endif
};

DjMidi::DjMidi(DjController* dj, QObject* parent) : QObject(parent), m_dj(dj), m_io(std::make_unique<Io>()) {
    QSettings s;
    m_enabled = s.value(QStringLiteral("controller/enabled"), true).toBool();
    m_invertTempo = s.value(QStringLiteral("controller/invertTempo"), false).toBool();
    loadMappings();
    // Controller einstecken genügt: regelmäßig nach Geräten schauen
    connect(&m_scan, &QTimer::timeout, this, [this] {
        if (!m_connected && m_enabled) refresh();
    });
    connect(&m_leds, &QTimer::timeout, this, [this] { updateLeds(); });
    connect(&m_keepAlive, &QTimer::timeout, this, [this] { sendKeepAlive(); });
    if (available()) {
        m_scan.start(3000);
        QTimer::singleShot(500, this, [this] {
            if (m_enabled) refresh();
        });
    }
}

DjMidi::~DjMidi() { disconnectPort(); }

bool DjMidi::available() {
#ifdef LOGGX_WITH_RTMIDI
    return true;
#else
    return false;
#endif
}

QString DjMidi::preset() const {
    if (!m_connected) return {};
    return m_flx4 ? QStringLiteral("Pioneer DDJ-FLX4") : tr("Eigene Zuordnung (MIDI-Learn)");
}

void DjMidi::setEnabled(bool on) {
    m_enabled = on;
    QSettings().setValue(QStringLiteral("controller/enabled"), on);
    if (!on) disconnectPort();
    else refresh();
    emit stateChanged();
}

void DjMidi::setInvertTempo(bool on) {
    m_invertTempo = on;
    QSettings().setValue(QStringLiteral("controller/invertTempo"), on);
    emit stateChanged();
}

void DjMidi::refresh() {
    QStringList list;
#ifdef LOGGX_WITH_RTMIDI
    try {
        RtMidiIn probe;
        for (unsigned i = 0; i < probe.getPortCount(); ++i) {
            const QString name = QString::fromStdString(probe.getPortName(i));
            // Eigene virtuelle Ports und Systemports ausblenden
            if (name.contains(QLatin1String("Midi Through"), Qt::CaseInsensitive)) continue;
            list << name;
        }
    } catch (...) {
        // Kein MIDI-System (z. B. Server ohne Soundkarte): nicht weiter suchen
        m_scan.stop();
    }
#endif
    if (list != m_ports) {
        m_ports = list;
        emit portsChanged();
    }
    if (m_connected || !m_enabled) return;
    // Bevorzugt: zuletzt benutztes Gerät, sonst ein bekannter Controller
    const QString preferred = QSettings().value(QStringLiteral("controller/port")).toString();
    for (const QString& p : m_ports)
        if (!preferred.isEmpty() && portKey(p) == portKey(preferred)) {
            connectPort(p);
            return;
        }
    for (const QString& p : m_ports)
        if (isFlx4Name(p)) {
            connectPort(p);
            return;
        }
}

bool DjMidi::connectPort(const QString& name) {
    disconnectPort();
#ifdef LOGGX_WITH_RTMIDI
    try {
        auto in = std::make_unique<RtMidiIn>();
        int index = -1;
        for (unsigned i = 0; i < in->getPortCount(); ++i)
            if (QString::fromStdString(in->getPortName(i)) == name) index = static_cast<int>(i);
        if (index < 0) return false;
        in->setCallback(
            [](double, std::vector<unsigned char>* msg, void* user) {
                if (!msg || msg->empty() || (*msg)[0] >= 0xF0) return;
                auto* self = static_cast<DjMidi*>(user);
                const int status = (*msg)[0];
                const int d1 = msg->size() > 1 ? (*msg)[1] : 0;
                const int d2 = msg->size() > 2 ? (*msg)[2] : 0;
                QMetaObject::invokeMethod(self, [self, status, d1, d2] { self->handle(status, d1, d2); }, Qt::QueuedConnection);
            },
            this);
        in->ignoreTypes(true, true, true);
        in->openPort(static_cast<unsigned>(index), "LXStudio DJ");
        m_io->in = std::move(in);
        // Passenden Ausgang suchen (LEDs, Keep-alive)
        auto out = std::make_unique<RtMidiOut>();
        for (unsigned i = 0; i < out->getPortCount(); ++i)
            if (portKey(QString::fromStdString(out->getPortName(i))) == portKey(name)) {
                out->openPort(i, "LXStudio DJ");
                m_io->out = std::move(out);
                break;
            }
    } catch (const std::exception& e) {
        m_lastMessage = tr("Controller kann nicht geöffnet werden: %1").arg(QString::fromStdString(e.what()));
        emit activity();
        m_io->in.reset();
        m_io->out.reset();
        return false;
    }
#else
    (void)name;
    return false;
#endif
    m_device = name;
    m_connected = true;
    m_flx4 = isFlx4Name(name);
    m_ledState.clear();
    QSettings().setValue(QStringLiteral("controller/port"), name);
    if (m_flx4) {
        sendKeepAlive();   // fragt zugleich die Reglerstellungen ab
        m_keepAlive.start(200);
    }
    m_leds.start(50);
    emit stateChanged();
    return true;
}

void DjMidi::disconnectPort() {
    m_leds.stop();
    m_keepAlive.stop();
    if (m_connected) lightsOff();
#ifdef LOGGX_WITH_RTMIDI
    try {
        if (m_io->in) {
            m_io->in->cancelCallback();
            m_io->in->closePort();
        }
        if (m_io->out) m_io->out->closePort();
    } catch (...) {
    }
    m_io->in.reset();
    m_io->out.reset();
#endif
    const bool was = m_connected;
    m_connected = false;
    m_device.clear();
    if (was) emit stateChanged();
}

void DjMidi::send(std::vector<unsigned char> msg) {
    if (m_hook) m_hook(msg);
#ifdef LOGGX_WITH_RTMIDI
    if (m_io->out) {
        try {
            m_io->out->sendMessage(&msg);
        } catch (...) {
        }
    }
#endif
}

void DjMidi::sendKeepAlive() { send(std::vector<unsigned char>(std::begin(kKeepAlive), std::end(kKeepAlive))); }

// ------------------------------------------------------------ Eingang

void DjMidi::handle(int status, int d1, int d2) {
    m_lastMessage = QStringLiteral("%1 %2 %3")
                        .arg(status, 2, 16, QLatin1Char('0'))
                        .arg(d1, 2, 16, QLatin1Char('0'))
                        .arg(d2, 2, 16, QLatin1Char('0'))
                        .toUpper();
    emit activity();
    const int type = status & 0xF0;
    if (type != 0x90 && type != 0x80 && type != 0xB0) return;
    if (type == 0x80) {   // Note Off = Note On mit 0
        status = 0x90 | (status & 0x0F);
        d2 = 0;
    }

    // MIDI-Learn: nächste Bewegung/Taste wird zugeordnet
    if (!m_learnAction.isEmpty() && (type == 0xB0 || d2 > 0)) {
        m_custom.erase(std::remove_if(m_custom.begin(), m_custom.end(),
                                      [&](const Mapping& m) { return m.status == status && m.data1 == d1; }),
                       m_custom.end());
        m_custom.push_back({status, d1, m_learnAction, m_learnDeck});
        m_learnAction.clear();
        saveMappings();
        emit mappingsChanged();
        emit stateChanged();
        return;
    }
    for (const Mapping& m : m_custom) {
        if (m.status != status || m.data1 != d1) continue;
        const bool button = (status & 0xF0) == 0x90;
        const int rel = d2 >= 64 ? d2 - 128 : d2;   // relative Regler (Zweierkomplement)
        runAction(m.action, m.deck, d2 / 127.0, button ? d2 > 0 : d2 > 63, rel);
        return;
    }
    if (m_flx4) handleFlx4(status, d1, d2);
}

void DjMidi::runAction(const QString& a, int deck, double v, bool press, int relative) {
    DjController& dj = *m_dj;
    if (a == QLatin1String("play")) { if (press) dj.togglePlay(deck); }
    else if (a == QLatin1String("cue")) { if (press) dj.cue(deck); }
    else if (a == QLatin1String("sync")) { if (press) dj.sync(deck); }
    else if (a == QLatin1String("pfl")) { if (press) dj.setPfl(deck, !dj.mixer().pfl(deck)); }
    else if (a == QLatin1String("load")) { if (press) dj.loadCursor(deck); }
    else if (a.startsWith(QLatin1String("hotcue"))) { if (press) dj.hotCue(deck, a.mid(6).toInt() - 1); }
    else if (a == QLatin1String("loop4")) { if (press) dj.setLoop(deck, 4); }
    else if (a == QLatin1String("reloop")) { if (press) dj.reloop(deck); }
    else if (a == QLatin1String("jumpBack")) { if (press) dj.beatJump(deck, -4); }
    else if (a == QLatin1String("jumpForward")) { if (press) dj.beatJump(deck, 4); }
    else if (a == QLatin1String("tempo")) {
        const double range = (deck == 0 ? dj.deckA() : dj.deckB()).value(QStringLiteral("range")).toDouble();
        dj.setTempo(deck, (v * 2.0 - 1.0) * range * (m_invertTempo ? -1.0 : 1.0));
    }
    else if (a == QLatin1String("eqHigh")) dj.setEq(deck, 2, v * 2.0 - 1.0);
    else if (a == QLatin1String("eqMid")) dj.setEq(deck, 1, v * 2.0 - 1.0);
    else if (a == QLatin1String("eqLow")) dj.setEq(deck, 0, v * 2.0 - 1.0);
    else if (a == QLatin1String("filter")) dj.setFilter(deck, std::abs(v * 2.0 - 1.0) < 0.03 ? 0.0 : v * 2.0 - 1.0);
    else if (a == QLatin1String("trim")) dj.setTrim(deck, v * 2.0);
    else if (a == QLatin1String("fader")) dj.setFader(deck, v);
    else if (a == QLatin1String("crossfader")) dj.setCrossfader(v * 2.0 - 1.0);
    else if (a == QLatin1String("headMix")) dj.setHeadMix(v);
    else if (a == QLatin1String("master")) dj.setMasterVolume(v * 1.5);
    else if (a == QLatin1String("browse")) dj.moveCursor(relative);
    else if (a == QLatin1String("jog")) {
        if (dj.mixer().playing(deck)) dj.jogBend(deck, relative * 0.004);
        else dj.nudge(deck, relative * 0.01);
    } else if (a == QLatin1String("fx")) { if (press) dj.setFxOn(!dj.fxOn()); }
}

bool DjMidi::handleFlx4(int status, int d1, int d2) {
    DjController& dj = *m_dj;
    DjMixer& mx = dj.mixer();
    const int type = status & 0xF0, ch = status & 0x0F;
    const bool press = d2 > 0;

    // 14-Bit-Regler: MSB merken, beim LSB (= MSB + 0x20) auswerten
    auto fine = [&](int msbNumber, double& out) -> bool {
        if (d1 == msbNumber) {
            m_msb[(status << 8) | msbNumber] = d2;
            return false;
        }
        if (d1 != msbNumber + 0x20) return false;
        out = ((m_msb[(status << 8) | msbNumber] << 7) | d2) / 16383.0;
        return true;
    };
    auto eqValue = [](double v) { return std::abs(v - 0.5) < 0.01 ? 0.0 : (v - 0.5) * 2.0; };

    if (type == kCc && ch <= 1) {
        const int deck = ch;
        double v = 0;
        if (fine(0x00, v)) {   // Tempo-Fader: oben = minus, unten = plus
            const double range = (deck == 0 ? dj.deckA() : dj.deckB()).value(QStringLiteral("range")).toDouble();
            double percent = (v * 2.0 - 1.0) * range;
            if (m_invertTempo) percent = -percent;
            dj.setTempo(deck, std::abs(percent) < range * 0.002 ? 0.0 : percent);
            return true;
        }
        if (fine(0x04, v)) { dj.setTrim(deck, v < 0.5 ? v * 2.0 : 1.0 + (v - 0.5) * 2.0); return true; }
        if (fine(0x07, v)) { dj.setEq(deck, 2, eqValue(v)); return true; }
        if (fine(0x0B, v)) { dj.setEq(deck, 1, eqValue(v)); return true; }
        if (fine(0x0F, v)) { dj.setEq(deck, 0, eqValue(v)); return true; }
        if (fine(0x13, v)) { dj.setFader(deck, v); return true; }
        const int ticks = d2 - 64;
        if (d1 == 0x22 || d1 == 0x21 || d1 == 0x23) {
            // Jog: oben berührt = Scratchen, sonst Bend (läuft) bzw. Suchen (steht)
            if (d1 == 0x22 && mx.scratching(deck)) dj.jogScratch(deck, ticks * kJogSecondsPerTick);
            else if (mx.playing(deck)) dj.jogBend(deck, ticks * 0.004);
            else dj.nudge(deck, ticks * kJogSecondsPerTick);
            return true;
        }
        if (d1 == 0x29) {   // Shift + Jog: schnell suchen
            dj.nudge(deck, ticks * 0.15);
            return true;
        }
        return false;
    }
    if (type == kCc && ch == 6) {   // Mixer, Browser
        double v = 0;
        if (fine(0x1F, v)) { dj.setCrossfader(v * 2.0 - 1.0); return true; }
        if (fine(0x0C, v)) { dj.setHeadMix(v); return true; }
        if (fine(0x17, v)) { dj.setFilter(0, std::abs(v - 0.5) < 0.015 ? 0.0 : v * 2.0 - 1.0); return true; }
        if (fine(0x18, v)) { dj.setFilter(1, std::abs(v - 0.5) < 0.015 ? 0.0 : v * 2.0 - 1.0); return true; }
        if (d1 == 0x40) {   // Browse-Regler (relativ)
            dj.moveCursor(d2 >= 64 ? d2 - 128 : d2);
            return true;
        }
        return false;
    }
    if (type == kCc && ch == 4) {   // Beat FX: Level/Depth
        if (d1 == 0x02) {
            dj.setFx(dj.fxType(), dj.fxBeats(), d2 / 127.0, dj.fxTarget());
            return true;
        }
        return false;
    }
    if (type != kNote) return false;

    if (ch <= 1) {
        const int deck = ch;
        if (d1 == 0x3F) { m_shift[static_cast<std::size_t>(deck)] = press; return true; }
        if (d1 == 0x36 || d1 == 0x67) { dj.setScratch(deck, press); return true; }
        if (!press) return true;
        switch (d1) {
        case 0x0B: dj.togglePlay(deck); return true;
        case 0x0C: dj.cue(deck); return true;
        case 0x48: dj.seek(deck, 0); return true;                       // Shift + Cue: zum Anfang
        case 0x58: dj.sync(deck); return true;
        case 0x5C: dj.setSyncLock(deck, !mx.syncLock(deck)); return true;   // Shift + Sync: Sync dauerhaft
        case 0x60: {                                                    // Tempo-Bereich wechseln
            const double r = (deck == 0 ? dj.deckA() : dj.deckB()).value(QStringLiteral("range")).toDouble();
            double next = kTempoRanges[0];
            for (double x : kTempoRanges)
                if (x > r + 0.1) { next = x; break; }
            dj.setTempoRange(deck, next);
            return true;
        }
        case 0x54: dj.setPfl(deck, !mx.pfl(deck)); return true;
        case 0x10: dj.setLoopInOut(deck, true); return true;
        case 0x11: dj.setLoopInOut(deck, false); return true;
        case 0x4C: dj.setLoop(deck, 4); return true;                    // Shift + Loop In: 4-Beat-Loop
        case 0x4D: dj.reloop(deck); return true;                        // Reloop/Exit
        case 0x4E: case 0x50: dj.exitLoop(deck); return true;
        case 0x51:                                                      // Cue/Loop Call ◀: Loop halbieren oder zurück
            if (mx.looping(deck)) dj.setLoop(deck, std::max(0.125, mx.loopBeats(deck) / 2));
            else dj.beatJump(deck, -4);
            return true;
        case 0x53:
            if (mx.looping(deck)) dj.setLoop(deck, std::min(64.0, mx.loopBeats(deck) * 2));
            else dj.beatJump(deck, 4);
            return true;
        case 0x3E: dj.beatJump(deck, -16); return true;
        case 0x3D: dj.beatJump(deck, 16); return true;
        case 0x68: dj.setQuantize(!dj.quantize()); return true;
        default: return false;
        }
    }
    if (ch >= 7 && ch <= 10) {   // Pads
        const int deck = ch <= 8 ? 0 : 1;
        const bool shift = ch == 8 || ch == 10;
        if (d1 >= 0x44 && d1 <= 0x46) {   // Pad-FX: Effekt, solange gedrückt
            dj.setFx(d1 - 0x44, dj.fxBeats(), dj.fxWet() > 0 ? dj.fxWet() : 0.5, deck);
            dj.setFxOn(press);
            return true;
        }
        if (!press) return true;
        if (d1 <= 0x07) {
            if (shift) dj.clearHotCue(deck, d1);
            else dj.hotCue(deck, d1);
        } else if (d1 >= 0x20 && d1 <= 0x27) {
            dj.beatJump(deck, kBeatJumpPads[d1 - 0x20]);
        } else if (d1 >= 0x30 && d1 <= 0x33) {   // Sampler-Modus → Stems (Gesang, Drums, Bass, Rest)
            dj.setStem(deck, d1 - 0x30, !mx.stem(deck, static_cast<DjStem>(d1 - 0x30)));
        } else if (d1 >= 0x40 && d1 <= 0x43) {
            dj.setStem(deck, d1 - 0x40, !mx.stem(deck, static_cast<DjStem>(d1 - 0x40)));
        } else if (d1 == 0x47) {
            dj.setFxOn(false);
        } else if (d1 >= 0x60 && d1 <= 0x67) {
            dj.setLoop(deck, kLoopPads[d1 - 0x60]);
        } else if (d1 >= 0x70 && d1 <= 0x77) {   // Key-Shift-Pads: Key Lock an/aus
            dj.setKeyLock(deck, !mx.keyLock(deck));
        } else {
            return false;
        }
        return true;
    }
    if (ch == 6) {   // Browser
        if (!press) return true;
        if (d1 == 0x46) { dj.loadCursor(0); return true; }
        if (d1 == 0x47) { dj.loadCursor(1); return true; }
        return false;
    }
    if (ch == 4 || ch == 5) {   // Beat FX
        if (!press) return true;
        if (d1 == 0x47) dj.setFxOn(!dj.fxOn());
        else if (d1 == 0x43) dj.setFxOn(false);
        else if (d1 == 0x63) dj.setFx((dj.fxType() + 1) % 3, dj.fxBeats(), dj.fxWet(), dj.fxTarget());
        else if (d1 == 0x4A) dj.setFx(dj.fxType(), std::max(0.125, dj.fxBeats() / 2), dj.fxWet(), dj.fxTarget());
        else if (d1 == 0x4B) dj.setFx(dj.fxType(), std::min(8.0, dj.fxBeats() * 2), dj.fxWet(), dj.fxTarget());
        else if (d1 == 0x10) dj.setFx(dj.fxType(), dj.fxBeats(), dj.fxWet(), 0);
        else if (d1 == 0x11) dj.setFx(dj.fxType(), dj.fxBeats(), dj.fxWet(), 1);
        else if (d1 == 0x14) dj.setFx(dj.fxType(), dj.fxBeats(), dj.fxWet(), 2);
        else return false;
        return true;
    }
    return false;
}

// ------------------------------------------------------------ LEDs

void DjMidi::updateLeds(bool force) {
    if (!m_flx4 || (!m_connected && !m_hook)) return;
    DjMixer& mx = m_dj->mixer();
    auto led = [&](int status, int d1, int value) {
        const int key = (status << 8) | d1;
        auto it = m_ledState.find(key);
        if (!force && it != m_ledState.end() && it->second == value) return;
        m_ledState[key] = value;
        send({static_cast<unsigned char>(status), static_cast<unsigned char>(d1), static_cast<unsigned char>(value)});
    };
    for (int deck = 0; deck < 2; ++deck) {
        const bool loaded = mx.track(deck) != nullptr;
        const bool playing = mx.playing(deck);
        const int ch = kNote | deck;
        led(ch, 0x0B, playing ? 0x7F : 0);
        // Cue blinkt wie am CDJ, wenn angehalten und nicht auf dem Cue-Punkt
        const bool atCue = std::abs(mx.position(deck) - mx.cuePoint(deck)) < 0.05;
        const bool blink = (QDateTime::currentMSecsSinceEpoch() / 400) % 2 == 0;
        led(ch, 0x0C, loaded && !playing ? (atCue || blink ? 0x7F : 0) : 0);
        led(ch, 0x58, mx.syncLock(deck) ? 0x7F : 0);
        led(ch, 0x54, mx.pfl(deck) ? 0x7F : 0);
        led(ch, 0x10, mx.looping(deck) ? 0x7F : 0);
        led(ch, 0x11, mx.looping(deck) ? 0x7F : 0);
        led(ch, 0x4D, mx.looping(deck) ? 0x7F : 0);
        const int pads = 0x97 + deck * 2;
        for (int i = 0; i < 8; ++i) {
            const int on = mx.hotCue(deck, i) >= 0 ? 0x7F : 0;
            led(pads, i, on);
            led(pads + 1, i, on);
            led(pads, 0x60 + i, mx.looping(deck) && std::abs(mx.loopBeats(deck) - kLoopPads[i]) < 1e-6 ? 0x7F : 0);
        }
        for (int s = 0; s < 4; ++s) {
            const int on = loaded && mx.stem(deck, static_cast<DjStem>(s)) ? 0x7F : 0;
            led(pads, 0x30 + s, on);
            led(pads, 0x40 + s, on);
        }
        // Pegelanzeige am Kanal
        led(kCc | deck, 0x02, static_cast<int>(std::clamp(mx.level(deck), 0.0, 1.0) * 127.0));
    }
    led(0x94, 0x47, m_dj->fxOn() ? 0x7F : 0);
}

void DjMidi::lightsOff() {
    for (int deck = 0; deck < 2; ++deck) {
        for (int n : {0x0B, 0x0C, 0x58, 0x54, 0x10, 0x11, 0x4D}) send({static_cast<unsigned char>(kNote | deck), static_cast<unsigned char>(n), 0});
        const int pads = 0x97 + deck * 2;
        for (int i = 0; i < 8; ++i)
            for (int base : {0x00, 0x30, 0x40, 0x60}) {
                send({static_cast<unsigned char>(pads), static_cast<unsigned char>(base + i), 0});
                send({static_cast<unsigned char>(pads + 1), static_cast<unsigned char>(base + i), 0});
            }
        send({static_cast<unsigned char>(kCc | deck), 0x02, 0});
    }
    m_ledState.clear();
}

// ------------------------------------------------------------ MIDI-Learn

QVariantList DjMidi::actions() const {
    const std::pair<const char*, QString> list[] = {
        {"play", tr("Play/Pause")}, {"cue", tr("Cue")}, {"sync", tr("Sync")}, {"pfl", tr("Vorhören (Kopfhörer)")},
        {"load", tr("Ausgewählten Track laden")}, {"hotcue1", tr("Hot Cue A")}, {"hotcue2", tr("Hot Cue B")},
        {"hotcue3", tr("Hot Cue C")}, {"hotcue4", tr("Hot Cue D")}, {"hotcue5", tr("Hot Cue E")}, {"hotcue6", tr("Hot Cue F")},
        {"hotcue7", tr("Hot Cue G")}, {"hotcue8", tr("Hot Cue H")}, {"loop4", tr("4-Beat-Loop")}, {"reloop", tr("Reloop/Exit")},
        {"jumpBack", tr("Beat Jump zurück")}, {"jumpForward", tr("Beat Jump vor")}, {"tempo", tr("Tempo-Fader")},
        {"eqHigh", tr("EQ Höhen")}, {"eqMid", tr("EQ Mitten")}, {"eqLow", tr("EQ Tiefen")}, {"filter", tr("Filter")},
        {"trim", tr("Trim/Gain")}, {"fader", tr("Kanal-Fader")}, {"crossfader", tr("Crossfader")},
        {"headMix", tr("Kopfhörer-Mix")}, {"master", tr("Master-Lautstärke")}, {"browse", tr("Browse-Regler")},
        {"jog", tr("Jog-Rad")}, {"fx", tr("Effekt an/aus")}};
    QVariantList out;
    for (const auto& [id, name] : list) out << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(id)}, {QStringLiteral("name"), name}};
    return out;
}

QVariantList DjMidi::mappings() const {
    QVariantList out;
    const QVariantList names = actions();
    for (const Mapping& m : m_custom) {
        QString label = m.action;
        for (const QVariant& v : names)
            if (v.toMap().value(QStringLiteral("id")).toString() == m.action) label = v.toMap().value(QStringLiteral("name")).toString();
        out << QVariantMap{{QStringLiteral("action"), m.action},
                           {QStringLiteral("label"), label},
                           {QStringLiteral("deck"), m.deck},
                           {QStringLiteral("midi"), QStringLiteral("%1 %2").arg(m.status, 2, 16, QLatin1Char('0')).arg(m.data1, 2, 16, QLatin1Char('0')).toUpper()}};
    }
    return out;
}

void DjMidi::learn(const QString& action, int deck) {
    m_learnAction = action;
    m_learnDeck = std::clamp(deck, 0, 1);
    emit stateChanged();
}

void DjMidi::cancelLearn() {
    m_learnAction.clear();
    emit stateChanged();
}

void DjMidi::removeMapping(int index) {
    if (index < 0 || index >= static_cast<int>(m_custom.size())) return;
    m_custom.erase(m_custom.begin() + index);
    saveMappings();
    emit mappingsChanged();
}

void DjMidi::clearMappings() {
    m_custom.clear();
    saveMappings();
    emit mappingsChanged();
}

void DjMidi::loadMappings() {
    for (const QVariant& v : QSettings().value(QStringLiteral("controller/mappings")).toList()) {
        const QVariantMap m = v.toMap();
        m_custom.push_back({m.value(QStringLiteral("status")).toInt(), m.value(QStringLiteral("data1")).toInt(),
                            m.value(QStringLiteral("action")).toString(), m.value(QStringLiteral("deck")).toInt()});
    }
}

void DjMidi::saveMappings() const {
    QVariantList list;
    for (const Mapping& m : m_custom)
        list << QVariantMap{{QStringLiteral("status"), m.status}, {QStringLiteral("data1"), m.data1},
                            {QStringLiteral("action"), m.action}, {QStringLiteral("deck"), m.deck}};
    QSettings().setValue(QStringLiteral("controller/mappings"), list);
}
