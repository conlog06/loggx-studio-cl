#include "loggx/core/License.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include <nlohmann/json.hpp>

extern "C" {
#include "monocypher-ed25519.h"
}

namespace loggx {

namespace {

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string b64url(const std::uint8_t* data, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; i += 3) {
        const std::uint32_t v = (data[i] << 16) | (i + 1 < n ? data[i + 1] << 8 : 0) | (i + 2 < n ? data[i + 2] : 0);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        if (i + 1 < n) out += kB64[(v >> 6) & 63];
        if (i + 2 < n) out += kB64[v & 63];
    }
    return out;
}

bool unb64url(const std::string& s, std::vector<std::uint8_t>& out) {
    out.clear();
    std::uint32_t buf = 0;
    int bits = 0;
    for (char c : s) {
        const char* p = std::find(kB64, kB64 + 64, c);
        if (p == kB64 + 64) return false;
        buf = (buf << 6) | static_cast<std::uint32_t>(p - kB64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return true;
}

Edition editionFrom(const std::string& s) {
    if (s == "admin") return Edition::Admin;
    if (s == "pro") return Edition::Pro;
    return Edition::Free;
}

}  // namespace

const char* editionId(Edition e) {
    switch (e) {
        case Edition::Pro: return "pro";
        case Edition::Admin: return "admin";
        default: return "free";
    }
}

const char* editionName(Edition e) {
    switch (e) {
        case Edition::Pro: return "Pro";
        case Edition::Admin: return "Admin";
        default: return "Free";
    }
}

std::string toHex(const std::uint8_t* data, std::size_t size) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}

bool fromHex(const std::string& hex, std::uint8_t* out, std::size_t size) {
    std::string clean;
    for (char c : hex)
        if (!std::isspace(static_cast<unsigned char>(c))) clean += c;
    if (clean.size() != size * 2) return false;
    for (std::size_t i = 0; i < size; ++i) {
        unsigned v = 0;
        for (int k = 0; k < 2; ++k) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(clean[i * 2 + k])));
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else return false;
        }
        out[i] = static_cast<std::uint8_t>(v);
    }
    return true;
}

void makeKeyPair(std::array<std::uint8_t, 32>& seed, SecretKey& secret, PublicKey& pub) {
    crypto_ed25519_key_pair(secret.data(), pub.data(), seed.data());
}

std::string signLicense(const License& l, const SecretKey& secret) {
    const nlohmann::json j = {{"v", 1},          {"id", l.id},          {"name", l.name},
                              {"email", l.email}, {"edition", editionId(l.edition)},
                              {"issued", l.issued}, {"expires", l.expires}};
    const std::string payload = j.dump();
    std::uint8_t sig[64];
    crypto_ed25519_sign(sig, secret.data(), reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());
    return "LGX1-" + b64url(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()) + "." +
           b64url(sig, 64);
}

bool verifyLicense(const std::string& rawKey, const PublicKey& pub, License& out, std::string* error,
                   const std::string& today) {
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    std::string key;
    for (char c : rawKey)
        if (!std::isspace(static_cast<unsigned char>(c))) key += c;
    if (key.rfind("LGX1-", 0) != 0) return fail("Kein LoggX-Lizenzschlüssel");
    const auto dot = key.find('.', 5);
    if (dot == std::string::npos) return fail("Lizenzschlüssel ist unvollständig");
    std::vector<std::uint8_t> payload, sig;
    if (!unb64url(key.substr(5, dot - 5), payload) || !unb64url(key.substr(dot + 1), sig) || sig.size() != 64)
        return fail("Lizenzschlüssel ist beschädigt");
    if (crypto_ed25519_check(sig.data(), pub.data(), payload.data(), payload.size()) != 0)
        return fail("Lizenzschlüssel ist ungültig");
    try {
        const auto j = nlohmann::json::parse(payload.begin(), payload.end());
        if (j.value("v", 0) != 1) return fail("Lizenz stammt aus einer neueren Version");
        License l;
        l.id = j.value("id", "");
        l.name = j.value("name", "");
        l.email = j.value("email", "");
        l.edition = editionFrom(j.value("edition", "free"));
        l.issued = j.value("issued", "");
        l.expires = j.value("expires", "");
        if (!today.empty() && !l.expires.empty() && l.expires < today) return fail("Lizenz ist abgelaufen");
        out = l;
        return true;
    } catch (...) {
        return fail("Lizenzdaten sind beschädigt");
    }
}

}  // namespace loggx
