#include "MidiInput.h"

#ifdef LOGGX_WITH_RTMIDI
#if __has_include(<RtMidi.h>)
#include <RtMidi.h>
#else
#include <rtmidi/RtMidi.h>
#endif
#endif

struct MidiInput::Impl {
#ifdef LOGGX_WITH_RTMIDI
    std::unique_ptr<RtMidiIn> in;
#endif
    Callback callback;
};

MidiInput::MidiInput() : d(std::make_unique<Impl>()) {}
MidiInput::~MidiInput() { close(); }

bool MidiInput::available() {
#ifdef LOGGX_WITH_RTMIDI
    return true;
#else
    return false;
#endif
}

QStringList MidiInput::ports() {
    QStringList list;
#ifdef LOGGX_WITH_RTMIDI
    try {
        RtMidiIn probe;
        for (unsigned i = 0; i < probe.getPortCount(); ++i)
            list << QString::fromStdString(probe.getPortName(i));
    } catch (...) {
        // Kein MIDI-System (z. B. Server ohne Soundkarte): keine Geräte.
    }
#endif
    return list;
}

bool MidiInput::open(int index, Callback callback, QString* error) {
    close();
#ifdef LOGGX_WITH_RTMIDI
    try {
        d->in = std::make_unique<RtMidiIn>();
        d->callback = std::move(callback);
        d->in->setCallback(
            [](double, std::vector<unsigned char>* msg, void* user) {
                auto* self = static_cast<Impl*>(user);
                if (!msg || msg->empty() || !self->callback) return;
                const int status = (*msg)[0];
                const int d1 = msg->size() > 1 ? (*msg)[1] : 0;
                const int d2 = msg->size() > 2 ? (*msg)[2] : 0;
                self->callback(status, d1, d2);
            },
            d.get());
        d->in->ignoreTypes(true, true, true);   // SysEx, Timing, Active Sensing
        d->in->openPort(static_cast<unsigned>(index), "LoggX Studio");
        m_index = index;
        return true;
    } catch (const std::exception& e) {
        if (error) *error = QString::fromStdString(e.what());
        d->in.reset();
        return false;
    }
#else
    (void)index;
    (void)callback;
    if (error) *error = QStringLiteral("Ohne MIDI-Unterstützung gebaut");
    return false;
#endif
}

void MidiInput::close() {
#ifdef LOGGX_WITH_RTMIDI
    if (d->in) {
        try {
            d->in->cancelCallback();
            d->in->closePort();
        } catch (...) {
        }
        d->in.reset();
    }
#endif
    m_index = -1;
}
