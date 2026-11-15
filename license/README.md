# Lizenzschlüssel

Solange `public_key.txt` fehlt, läuft LoggX Studio als **Entwicklerversion**: alle Funktionen frei, Admin-Bereich sichtbar.

So aktivierst du das Lizenzsystem:

1. Schlüsselpaar erzeugen (einmalig, auf deinem Rechner):
   `loggx-license keygen ~/loggx-privat.key`
   Die Datei `loggx-privat.key` ist geheim: **nie** ins Repository, nie weitergeben, sicher sichern.
2. Den ausgegebenen öffentlichen Schlüssel (64 Hex-Zeichen) in `license/public_key.txt` eintragen und die App neu bauen.
3. Lizenzen ausstellen:
   `loggx-license sign ~/loggx-privat.key --name "Max Muster" --email max@example.de --edition pro`
   `loggx-license sign ~/loggx-privat.key --name "Constantin" --email ... --edition admin`
4. Den ausgegebenen Schlüssel (`LGX1-…`) in der App unter **Lizenz** eintragen.

Editionen: **Free** (Export bis 1080p, H.264), **Pro** (4K und 8K, H.265), **Admin** (alles plus Admin-Bereich).

Hinweis: Die Signatur verhindert gefälschte Schlüssel. Gegen ein Verändern des Programms selbst hilft nur eine serverseitige Prüfung (siehe Konzept, Web-Admin-Dashboard).
