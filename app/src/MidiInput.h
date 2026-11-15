// MIDI-Keyboards über RtMidi (optional). Ohne RtMidi gebaut: keine Geräte.
#pragma once

#include <functional>
#include <memory>

#include <QStringList>

class MidiInput {
public:
    // status, data1, data2 – Aufruf im MIDI-Thread!
    using Callback = std::function<void(int status, int data1, int data2)>;

    MidiInput();
    ~MidiInput();
    static bool available();
    QStringList ports();
    bool open(int index, Callback callback, QString* error = nullptr);
    void close();
    int openIndex() const { return m_index; }

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    int m_index = -1;
};
