// Lizenzschlüssel mit Ed25519-Signatur (offline prüfbar).
//
// Format: LGX1-<Nutzdaten base64url>.<Signatur base64url>
// Nutzdaten: JSON {"v":1,"id","name","email","edition","issued","expires"}
//
// Nur wer den privaten Schlüssel hat, kann gültige Lizenzen ausstellen;
// die App enthält ausschließlich den öffentlichen Schlüssel.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace loggx {

enum class Edition { Free, Pro, Admin };
const char* editionId(Edition e);       // "free", "pro", "admin"
const char* editionName(Edition e);     // "Free", "Pro", "Admin"

using PublicKey = std::array<std::uint8_t, 32>;
using SecretKey = std::array<std::uint8_t, 64>;

struct License {
    std::string id;
    std::string name;
    std::string email;
    Edition edition = Edition::Free;
    std::string issued;    // JJJJ-MM-TT
    std::string expires;   // leer = unbefristet
};

// Erzeugt ein Schlüsselpaar aus 32 Zufallsbytes (seed wird überschrieben).
void makeKeyPair(std::array<std::uint8_t, 32>& seed, SecretKey& secret, PublicKey& pub);
std::string signLicense(const License& license, const SecretKey& secret);
// today: JJJJ-MM-TT für die Ablaufprüfung (leer = nicht prüfen).
bool verifyLicense(const std::string& key, const PublicKey& pub, License& out,
                   std::string* error = nullptr, const std::string& today = {});

std::string toHex(const std::uint8_t* data, std::size_t size);
bool fromHex(const std::string& hex, std::uint8_t* out, std::size_t size);

}  // namespace loggx
