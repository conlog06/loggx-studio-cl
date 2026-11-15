// DJ-Controller über MIDI: eingebaute Zuordnung für den Pioneer DDJ-FLX4
// (wird automatisch erkannt, mit LEDs und Keep-alive) und „MIDI-Learn“ für
// beliebige andere Controller. Für QML als „djmidi“ verfügbar.
#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

class DjController;

class DjMidi : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QStringList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(QString device READ device NOTIFY stateChanged)
    Q_PROPERTY(QString preset READ preset NOTIFY stateChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY stateChanged)
    Q_PROPERTY(bool invertTempo READ invertTempo WRITE setInvertTempo NOTIFY stateChanged)
    Q_PROPERTY(QString learning READ learning NOTIFY stateChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY activity)
    Q_PROPERTY(QVariantList mappings READ mappings NOTIFY mappingsChanged)
    Q_PROPERTY(QVariantList actions READ actions CONSTANT)

public:
    explicit DjMidi(DjController* dj, QObject* parent = nullptr);
    ~DjMidi() override;

    static bool available();
    QStringList ports() const { return m_ports; }
    bool connected() const { return m_connected; }
    QString device() const { return m_device; }
    QString preset() const;
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    bool invertTempo() const { return m_invertTempo; }
    void setInvertTempo(bool on);
    QString learning() const { return m_learnAction; }
    QString lastMessage() const { return m_lastMessage; }
    QVariantList mappings() const;
    QVariantList actions() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool connectPort(const QString& name);
    Q_INVOKABLE void disconnectPort();
    Q_INVOKABLE void learn(const QString& action, int deck);
    Q_INVOKABLE void cancelLearn();
    Q_INVOKABLE void removeMapping(int index);
    Q_INVOKABLE void clearMappings();

    // Eine MIDI-Nachricht verarbeiten (Haupt-Thread). Öffentlich für Tests.
    void handle(int status, int data1, int data2);
    // Für Tests: Zuordnung erzwingen und ausgehende Nachrichten mitlesen
    void usePreset(bool flx4) { m_flx4 = flx4; }
    void setOutputHook(std::function<void(const std::vector<unsigned char>&)> hook) { m_hook = std::move(hook); }
    void updateLeds(bool force = false);

signals:
    void portsChanged();
    void stateChanged();
    void activity();
    void mappingsChanged();

private:
    struct Mapping {
        int status = 0, data1 = 0;
        QString action;
        int deck = 0;
    };
    struct Io;
    void send(std::vector<unsigned char> msg);
    void sendKeepAlive();
    bool handleFlx4(int status, int d1, int d2);
    void runAction(const QString& action, int deck, double value, bool press, int relative);
    void loadMappings();
    void saveMappings() const;
    void lightsOff();

    DjController* m_dj;
    std::unique_ptr<Io> m_io;
    QStringList m_ports;
    QString m_device, m_lastMessage, m_learnAction;
    int m_learnDeck = 0;
    bool m_connected = false, m_enabled = true, m_invertTempo = false, m_flx4 = false;
    std::array<bool, 2> m_shift{false, false};
    std::map<int, int> m_msb;                 // (status<<8 | data1) → letztes MSB (14-Bit-Regler)
    std::map<int, int> m_ledState;            // zuletzt gesendeter LED-Zustand
    std::vector<Mapping> m_custom;
    std::function<void(const std::vector<unsigned char>&)> m_hook;
    QTimer m_scan, m_leds, m_keepAlive;
};
