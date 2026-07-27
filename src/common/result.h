#pragma once

#include <string>
#include <variant>
#include <optional>

namespace synthgen {

enum class ErrorCode {
    // Common
    kOk,
    kInvalidArgument,
    kInvalidRange,
    kInvalidState,
    kTypeMismatch,
    kNotFound,
    kAlreadyExists,
    kDataCorruption,
    kUnsupportedInV1,
    kOutOfMemory,
    kTimeout,
    kInternalError,

    // Parser-specific
    kSyntaxError,
    kUndefinedType,
    kUndefinedColumn,
    kDuplicateColumnName,
    kDuplicateTypeName,
    kInvalidEnum,
    kInvalidSchema,
    kInvalidColumnName,

    // Storage-specific
    kTableAlreadyExists,
    kTableNotFound,
    kStorageFull,
    kSchemaMismatch,
    kSnapshotNotFound,
    kWriteFailed,
    kReadFailed,
    kColumnNotFound,

    // EvidencePackage-specific
    kSchemaViolation,
    kHonestyViolation,
    kConsistencyError,
    kSerializationError,
    kDeserializationError,
    kHashMismatch,

    // v2: Inter-row constraint
    kOrderColumnRequired,
    kInvalidDelta,
    kEmptyBatch,
    kOrderColumnNull,
    kInvalidOffset,
    kStateNotInitialized,

    // v3: Version chain
    kVersionNotFound,
    kParentNotFound,
    kImmutableViolation,
    kDuplicateVersionId,
    kVersionChainCycle,
    kModelNotFound,
    kInvalidVersionId,

    // v3: GC compaction
    kCompactionInProgress,
    kProtectedVersion,
    kCompactionFailed,
    kMetadataMergeConflict,
    kAutoCompactDisabled,

    // v3: Time travel
    kVersionCompacted,
    kNoAvailableVersion,
    kSnapshotLoadFailed,

    // v3: Alignment
    kDataEngineUnavailable,
    kEmptyTrainingData,
    kDriftDetectionFailed,
    kCompensationTimeout,
    kCompensationDiverging,
    kVersionCreationFailed,
    kDimensionMismatch,
    kProtocolNotDefined,

    // v3: Completeness (v4 prep)
    kInvalidWindowSpec,
    kSearchNotConverged,
};

struct Error {
    ErrorCode code;
    std::string message;
    std::string component;

    Error(ErrorCode c, std::string msg, std::string comp = "")
        : code(c), message(std::move(msg)), component(std::move(comp)) {}
};

template<typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}   // NOLINT
    Result(Error err) : data_(std::move(err)) {}    // NOLINT

    bool ok() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return ok(); }

    const T& value() const& { return std::get<T>(data_); }
    T& value() & { return std::get<T>(data_); }
    T&& value() && { return std::get<T>(std::move(data_)); }

    const Error& error() const { return std::get<Error>(data_); }

private:
    std::variant<T, Error> data_;
};

template<>
class Result<void> {
public:
    Result() : error_(std::nullopt) {}
    Result(Error err) : error_(std::move(err)) {}  // NOLINT

    bool ok() const { return !error_.has_value(); }
    explicit operator bool() const { return ok(); }

    const Error& error() const { return error_.value(); }

private:
    std::optional<Error> error_;
};

}  // namespace synthgen
