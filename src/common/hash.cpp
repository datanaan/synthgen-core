#include "common/hash.h"

#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

namespace synthgen {

std::string sha256_hex(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()),
           input.size(), hash);

    std::ostringstream oss;
    for (unsigned char c : hash) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(c);
    }
    return oss.str();
}

}  // namespace synthgen
