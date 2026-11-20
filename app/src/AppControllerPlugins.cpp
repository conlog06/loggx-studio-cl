// Audio-Plugins (CLAP, VST3, AU): Suche, auf Spuren einsetzen, Parameter,
// eigene Oberfläche des Plugins in einem Fenster.
#include <cstdio>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>
#include <QWindow>

#include "AppController.h"
#include "loggx/engine/Plugins.h"

using namespace loggx;

namespace {
QString qstr(const std::string& s) { return QString::fromStdString(s); }
std::string str(const QString& s) { return s.toStdString(); }

PluginInfo infoFromMap(const QVariantMap& m) {
    PluginInfo i;
    i.format = m.value(QStringLiteral("format")).toString().toStdString();
    i.id = m.value(QStringLiteral("id")).toString().toStdString();
    i.name = m.value(QStringLiteral("name")).toString().toStdString();
    i.vendor = m.value(QStringLiteral("vendor")).toString().toStdString();
    i.version = m.value(QStringLiteral("version")).toString().toStdString();
    i.path = m.value(QStringLiteral("path")).toString().toStdString();
    i.instrument = m.value(QStringLiteral("instrument")).toBool();
    return i;
}
QVariantMap mapFromInfo(const PluginInfo& i) {
    return {{QStringLiteral("format"), QString::fromStdString(i.format)},
            {QStringLiteral("id"), QString::fromStdString(i.id)},
            {QStringLiteral("name"), QString::fromStdString(i.name)},
            {QStringLiteral("vendor"), QString::fromStdString(i.vendor)},
            {QStringLiteral("version"), QString::fromStdString(i.version)},
            {QStringLiteral("path"), QString::fromStdString(i.path)},
            {QStringLiteral("instrument"), i.instrument}};
}
}  // namespace

// Ergebnis der letzten Suche (gespeichert, damit nicht bei jedem Start gesucht wird)
QVariantList AppController::availablePlugins() const {
    QVariantList list;
    const QJsonArray arr = QJsonDocument::fromJson(QSettings().value(QStringLiteral("plugins/cache")).toByteArray()).array();
    for (const auto& v : arr) {
        QVariantMap m = v.toObject().toVariantMap();
        if (m.value(QStringLiteral("instrument")).toBool()) continue;   // nur Effekte
        list << m;
    }
    return list;
}

QStringList AppController::pluginFolders() const { return QSettings().value(QStringLiteral("plugins/dirs")).toStringList(); }

void AppController::setPluginFolders(const QStringList& dirs) {
    QSettings().setValue(QStringLiteral("plugins/dirs"), dirs);
    emit pluginsChanged();
}

QStringList AppController::defaultPluginFolders() const {
    QStringList out;
    for (const auto& d : defaultPluginDirs()) out << QString::fromStdString(d);
    return out;
}

bool AppController::scanPluginsNow() {
    if (m_task) return false;
    std::vector<std::string> extra;
    for (const QString& d : pluginFolders()) extra.push_back(d.toStdString());
    auto found = std::make_shared<std::vector<PluginInfo>>();
    auto crashed = std::make_shared<QStringList>();
    const QString exe = QCoreApplication::applicationFilePath();
    runTask(tr("Suche Plugins …"),
            [extra, found, crashed, exe](const std::function<void(double)>&, const std::atomic<bool>& cancel) {
                // In einem eigenen Prozess: stürzt ein Plugin ab, wird es übersprungen
                QTemporaryDir tmp;
                for (int attempt = 0; attempt < 12 && !cancel.load(); ++attempt) {
                    QStringList args{QStringLiteral("--scan-plugins"), tmp.filePath(QStringLiteral("plugins.json"))};
                    for (const auto& d : extra) args << QStringLiteral("--plugin-dir") << QString::fromStdString(d);
                    for (const QString& c : *crashed) args << QStringLiteral("--plugin-skip") << c;
                    QProcess proc;
                    proc.start(exe, args);
                    proc.waitForFinished(5 * 60 * 1000);
                    QFile f(tmp.filePath(QStringLiteral("plugins.json")));
                    if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 && f.open(QIODevice::ReadOnly)) {
                        for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) found->push_back(infoFromMap(v.toObject().toVariantMap()));
                        return;
                    }
                    // Zuletzt gemeldete Datei war schuld
                    const QStringList lines = QString::fromUtf8(proc.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                    QString last;
                    for (const QString& l : lines)
                        if (l.startsWith(QLatin1String("SCAN "))) last = l.mid(5).trimmed();
                    if (last.isEmpty() || crashed->contains(last)) break;
                    crashed->append(last);
                }
                // Notfalls im eigenen Prozess (z. B. wenn der Hilfsprozess nicht startet)
                if (found->empty() && crashed->isEmpty()) *found = scanPlugins(extra);
            },
            [this, found, crashed] {
                QJsonArray arr;
                for (const auto& f : *found) arr.append(QJsonObject::fromVariantMap(mapFromInfo(f)));
                QSettings().setValue(QStringLiteral("plugins/cache"), QJsonDocument(arr).toJson(QJsonDocument::Compact));
                int effects = 0;
                for (const auto& f : *found) effects += !f.instrument;
                if (!crashed->isEmpty()) {
                    setStatus(tr("%1 Effekt-Plugins gefunden – %2 übersprungen, weil sie abstürzen: %3")
                                  .arg(effects).arg(crashed->size()).arg(crashed->join(QStringLiteral(", "))));
                    emit pluginsChanged();
                    return;
                }
                setStatus(tr("%1 Effekt-Plugins gefunden (CLAP, VST3%2)").arg(effects).arg(
#ifdef Q_OS_MACOS
                    tr(", Audio Units")
#else
                    QString()
#endif
                    ));
                emit pluginsChanged();
            });
    return true;
}

bool AppController::addPlugin(int trackIndex, const QVariantMap& plugin) {
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_project.tracks.size())) return false;
    const PluginInfo info = infoFromMap(plugin);
    PluginRef ref;
    ref.uid = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    ref.format = info.format;
    ref.pluginId = info.id;
    ref.path = info.path;
    ref.name = info.name;
    ref.vendor = info.vendor;
    const bool ok = apply([&](Project& p) {
        p.tracks[static_cast<std::size_t>(trackIndex)].plugins.push_back(ref);
        return true;
    });
    const auto errors = m_mixer.pluginErrors();
    if (auto it = errors.find(ref.uid); it != errors.end()) setStatus(tr("%1: %2").arg(qstr(ref.name), qstr(it->second)));
    else if (ok) setStatus(tr("%1 eingesetzt").arg(qstr(ref.name)));
    return ok;
}

bool AppController::removePlugin(int trackIndex, const QString& uid) {
    closePluginEditor(uid);
    return apply([&](Project& p) {
        if (trackIndex < 0 || trackIndex >= static_cast<int>(p.tracks.size())) return false;
        auto& v = p.tracks[static_cast<std::size_t>(trackIndex)].plugins;
        const auto before = v.size();
        v.erase(std::remove_if(v.begin(), v.end(), [&](const PluginRef& r) { return r.uid == str(uid); }), v.end());
        return v.size() != before;
    });
}

bool AppController::setPluginBypass(int trackIndex, const QString& uid, bool bypass) {
    return apply([&](Project& p) {
        if (trackIndex < 0 || trackIndex >= static_cast<int>(p.tracks.size())) return false;
        for (auto& r : p.tracks[static_cast<std::size_t>(trackIndex)].plugins)
            if (r.uid == str(uid)) return r.bypass = bypass, true;
        return false;
    });
}

bool AppController::movePlugin(int trackIndex, const QString& uid, int delta) {
    return apply([&](Project& p) {
        if (trackIndex < 0 || trackIndex >= static_cast<int>(p.tracks.size())) return false;
        auto& v = p.tracks[static_cast<std::size_t>(trackIndex)].plugins;
        for (std::size_t i = 0; i < v.size(); ++i)
            if (v[i].uid == str(uid)) {
                const long j = static_cast<long>(i) + delta;
                if (j < 0 || j >= static_cast<long>(v.size())) return false;
                std::swap(v[i], v[static_cast<std::size_t>(j)]);
                return true;
            }
        return false;
    });
}

QVariantList AppController::pluginParams(const QString& uid) const {
    QVariantList list;
    auto pl = m_mixer.plugin(str(uid));
    if (!pl) return list;
    for (const auto& p : pl->params())
        list << QVariantMap{{QStringLiteral("id"), qstr(p.id)},    {QStringLiteral("name"), qstr(p.name)},
                            {QStringLiteral("min"), p.min},        {QStringLiteral("max"), p.max},
                            {QStringLiteral("def"), p.def},        {QStringLiteral("value"), p.value},
                            {QStringLiteral("stepped"), p.stepped}, {QStringLiteral("unit"), qstr(p.unit)}};
    return list;
}

QString AppController::pluginError(const QString& uid) const {
    const auto e = m_mixer.pluginErrors();
    auto it = e.find(str(uid));
    return it == e.end() ? QString() : qstr(it->second);
}

bool AppController::pluginHasEditor(const QString& uid) const {
    auto pl = m_mixer.plugin(str(uid));
    return pl && pl->hasEditor();
}

bool AppController::setPluginParam(int trackIndex, const QString& uid, const QString& paramId, double value) {
    return apply([&](Project& p) {
        if (trackIndex < 0 || trackIndex >= static_cast<int>(p.tracks.size())) return false;
        for (auto& r : p.tracks[static_cast<std::size_t>(trackIndex)].plugins)
            if (r.uid == str(uid)) return r.params[str(paramId)] = value, true;
        return false;
    });
}

// Zustand aller laufenden Plugins ins Projekt übernehmen (vor dem Speichern
// und nach Änderungen in der Plugin-Oberfläche) – ohne Rückgängig-Schritt
void AppController::capturePluginStates() {
    bool changed = false;
    for (auto& t : m_project.tracks)
        for (auto& r : t.plugins) {
            auto pl = m_mixer.plugin(r.uid);
            if (!pl) continue;
            std::string st = pl->saveState();
            if (st.empty() || st == r.state) continue;
            r.state = std::move(st);
            r.params.clear();   // der Zustand enthält alle Werte
            changed = true;
        }
    if (changed) {
        m_shared = std::make_shared<const Project>(m_project);
        m_mixer.setProject(m_shared);   // gleicher Zustand → kein Neuladen
    }
}

bool AppController::openPluginEditor(const QString& uid) {
    auto pl = m_mixer.plugin(str(uid));
    if (!pl || !pl->hasEditor()) return false;
    if (auto it = m_pluginWindows.find(uid); it != m_pluginWindows.end() && it.value()) {
        it.value()->raise();
        it.value()->requestActivate();
        return true;
    }
    auto* win = new QWindow;
    win->setTitle(qstr(pl->info().name) + QStringLiteral(" – LXStudio"));
    win->setFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowTitleHint | Qt::WindowMinimizeButtonHint);
    int w = 640, h = 400;
    win->create();
    if (!pl->openEditor(reinterpret_cast<void*>(win->winId()), w, h)) {
        delete win;
        setStatus(tr("%1 hat keine eigene Oberfläche – Parameter im Mixer").arg(qstr(pl->info().name)));
        return false;
    }
    win->resize(w, h);
    QPointer<QWindow> guard(win);
    pl->onResize = [guard](int nw, int nh) {
        if (guard) guard->resize(nw, nh);
    };
    pl->onEdited = [this] { setDirty(true); };
    connect(win, &QWindow::visibleChanged, this, [this, uid, win](bool visible) {
        if (visible) return;
        if (auto p = m_mixer.plugin(str(uid))) {
            p->closeEditor();
            p->onResize = nullptr;
        }
        capturePluginStates();
        m_pluginWindows.remove(uid);
        win->deleteLater();
    });
    m_pluginWindows[uid] = win;
    win->show();
    return true;
}

void AppController::closePluginEditor(const QString& uid) {
    if (auto it = m_pluginWindows.find(uid); it != m_pluginWindows.end() && it.value()) it.value()->hide();
}

// Hilfsprozess: „LXStudio --scan-plugins datei.json [--plugin-dir d]… [--plugin-skip p]…“
int runPluginScanProcess(const QStringList& args) {
    QString out;
    std::vector<std::string> dirs, skip;
    for (int i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == QLatin1String("--scan-plugins")) out = args[i + 1];
        else if (args[i] == QLatin1String("--plugin-dir")) dirs.push_back(args[i + 1].toStdString());
        else if (args[i] == QLatin1String("--plugin-skip")) skip.push_back(args[i + 1].toStdString());
    }
    const auto found = scanPlugins(dirs, [](const std::string& path) {
        std::printf("SCAN %s\n", path.c_str());
        std::fflush(stdout);
    }, skip);
    QJsonArray arr;
    for (const auto& f : found) arr.append(QJsonObject::fromVariantMap(mapFromInfo(f)));
    QFile f(out);
    if (!f.open(QIODevice::WriteOnly)) return 2;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return 0;
}
