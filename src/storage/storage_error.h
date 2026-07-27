#pragma once

#include "common/result.h"

namespace synthgen::storage {

using StorageError = Error;

inline StorageError MakeStorageError(ErrorCode code, const std::string& msg) {
    return StorageError(code, msg, "storage");
}

}  // namespace synthgen::storage
