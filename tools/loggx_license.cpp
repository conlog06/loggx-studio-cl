// Lizenzwerkzeug für den Hersteller (nicht für Kunden ausliefern).
//
//   loggx-license keygen privat.key
//       Erzeugt ein Schlüsselpaar. privat.key geheim halten (nicht ins Repo!),
//       den ausgegebenen öffentlichen Schlüssel in license/public_key.txt
//       eintragen und die App neu bauen.
//   loggx-license sign privat.key --name "Max Muster" --email max@example.de
//       --edition pro|admin [--expires 2027-12-31]
//   loggx-license verify <öffentlicher Schlüssel hex> <Lizenzschlüssel>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include "loggx/core/License.h"

using namespace loggx;

namespace {

int usage() {
    std::fprintf(stderr,
                 "Aufruf:\n"
                 "  loggx-license keygen <privat.key>\n"
                 "  loggx-license sign <privat.key> --name NAME --email MAIL --edition pro|admin [--expires JJJJ-MM-TT]\n"
                 "  loggx-license verify <public-hex> <LGX1-...>\n");
    return 2;
}

std::string today() {
    const std::time_t t = std::time(nullptr);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&t));
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string cmd = argv[1];

    if (cmd == "keygen") {
        if (std::filesystem::exists(argv[2])) {
            std::fprintf(stderr, "%s existiert schon – wird nicht überschrieben.\n", argv[2]);
            return 1;
        }
        std::random_device rd;   // Zufall des Betriebssystems
        std::array<std::uint8_t, 32> seed;
        for (auto& b : seed) b = static_cast<std::uint8_t>(rd());
        SecretKey secret;
        PublicKey pub;
        makeKeyPair(seed, secret, pub);
        std::ofstream f(argv[2]);
        f << toHex(secret.data(), secret.size()) << "\n";
        if (!f) {
            std::fprintf(stderr, "Datei kann nicht geschrieben werden.\n");
            return 1;
        }
        std::printf("Privater Schlüssel gespeichert: %s (GEHEIM HALTEN, nicht ins Repository)\n", argv[2]);
        std::printf("Öffentlicher Schlüssel:\n%s\n", toHex(pub.data(), pub.size()).c_str());
        std::printf("-> In license/public_key.txt eintragen und die App neu bauen.\n");
        return 0;
    }

    if (cmd == "sign") {
        std::ifstream f(argv[2]);
        std::string hex;
        std::getline(f, hex);
        SecretKey secret;
        if (!fromHex(hex, secret.data(), secret.size())) {
            std::fprintf(stderr, "Privater Schlüssel kann nicht gelesen werden.\n");
            return 1;
        }
        License l;
        l.issued = today();
        std::string edition;
        for (int i = 3; i + 1 < argc; i += 2) {
            const std::string k = argv[i];
            if (k == "--name") l.name = argv[i + 1];
            else if (k == "--email") l.email = argv[i + 1];
            else if (k == "--edition") edition = argv[i + 1];
            else if (k == "--expires") l.expires = argv[i + 1];
            else return usage();
        }
        if (edition == "pro") l.edition = Edition::Pro;
        else if (edition == "admin") l.edition = Edition::Admin;
        else {
            std::fprintf(stderr, "--edition muss pro oder admin sein.\n");
            return 2;
        }
        std::random_device rd;
        std::ostringstream id;
        for (int i = 0; i < 4; ++i) id << std::hex << rd();
        l.id = id.str();
        std::printf("%s\n", signLicense(l, secret).c_str());
        return 0;
    }

    if (cmd == "verify" && argc >= 4) {
        PublicKey pub;
        if (!fromHex(argv[2], pub.data(), pub.size())) {
            std::fprintf(stderr, "Öffentlicher Schlüssel ist kein 64-stelliger Hex-Wert.\n");
            return 1;
        }
        License l;
        std::string error;
        if (!verifyLicense(argv[3], pub, l, &error, today())) {
            std::printf("UNGÜLTIG: %s\n", error.c_str());
            return 1;
        }
        std::printf("Gültig: %s <%s>, Edition %s, ausgestellt %s%s%s\n", l.name.c_str(), l.email.c_str(),
                    editionName(l.edition), l.issued.c_str(), l.expires.empty() ? "" : ", gültig bis ",
                    l.expires.c_str());
        return 0;
    }
    return usage();
}
