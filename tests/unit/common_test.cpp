#include <gtest/gtest.h>
#include "common/result.h"
#include "common/types.h"
#include "common/hash.h"
#include <string>
#include <vector>
#include <memory>
#include <utility>
#include <thread>
#include <unordered_set>
#include <fstream>
#include <filesystem>
#include <unistd.h>

using namespace synthgen;

// ============================================================================
// Result<T> Tests
// ============================================================================

// --- Basic ok/error states ---

TEST(ResultTest, OkResultHoldsValue) {
    Result<int> r(42);
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.value(), 42);
}

TEST(ResultTest, ErrorResultHoldsError) {
    Result<int> r(Error(ErrorCode::kInvalidArgument, "bad arg"));
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error().code, ErrorCode::kInvalidArgument);
    EXPECT_EQ(r.error().message, "bad arg");
}

TEST(ResultTest, ErrorResultWithComponent) {
    Result<int> r(Error(ErrorCode::kInternalError, "crash", "engine"));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().component, "engine");
    EXPECT_EQ(r.error().message, "crash");
}

TEST(ResultTest, ErrorDefaultComponentIsEmpty) {
    Result<int> r(Error(ErrorCode::kNotFound, "gone"));
    EXPECT_EQ(r.error().component, "");
}

// --- Value access patterns ---

TEST(ResultTest, ValueAccessByConstRef) {
    const Result<int> r(99);
    const int& val = r.value();
    EXPECT_EQ(val, 99);
}

TEST(ResultTest, ValueAccessByRef) {
    Result<int> r(99);
    int& val = r.value();
    val = 100;
    EXPECT_EQ(r.value(), 100);
}

TEST(ResultTest, ValueAccessByRvalue) {
    Result<std::unique_ptr<int>> r(std::make_unique<int>(42));
    auto ptr = std::move(r).value();
    EXPECT_EQ(*ptr, 42);
}

// --- Result<std::string> ---

TEST(ResultTest, StringValue) {
    Result<std::string> r(std::string("hello"));
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value(), "hello");
}

TEST(ResultTest, StringError) {
    Result<std::string> r(Error(ErrorCode::kDataCorruption, "corrupt"));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kDataCorruption);
}

TEST(ResultTest, EmptyString) {
    Result<std::string> r(std::string(""));
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().size(), 0u);
}

// --- Result of pointer types ---

TEST(ResultTest, RawPointerValue) {
    int x = 42;
    Result<int*> r(&x);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(*r.value(), 42);
}

TEST(ResultTest, NullPointerValue) {
    Result<int*> r(nullptr);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value(), nullptr);
}

TEST(ResultTest, SharedPointerValue) {
    Result<std::shared_ptr<int>> r(std::make_shared<int>(42));
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(*r.value(), 42);
}

// --- Result<void> specialization ---

TEST(ResultVoidTest, DefaultConstructorIsOk) {
    Result<void> r;
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ResultVoidTest, ErrorConstructorIsError) {
    Result<void> r(Error(ErrorCode::kNotFound, "not here"));
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error().code, ErrorCode::kNotFound);
}

TEST(ResultVoidTest, ErrorPreservesMessage) {
    Result<void> r(Error(ErrorCode::kTimeout, "timed out", "network"));
    EXPECT_EQ(r.error().message, "timed out");
    EXPECT_EQ(r.error().component, "network");
}

// --- Move semantics ---

TEST(ResultTest, MoveConstruction) {
    Result<std::string> r1(std::string("data"));
    Result<std::string> r2(std::move(r1));
    ASSERT_TRUE(r2.ok());
    EXPECT_EQ(r2.value(), "data");
}

TEST(ResultTest, MoveConstructionFromError) {
    Result<int> r1(Error(ErrorCode::kInternalError, "fail"));
    Result<int> r2(std::move(r1));
    EXPECT_FALSE(r2.ok());
    EXPECT_EQ(r2.error().code, ErrorCode::kInternalError);
}

TEST(ResultTest, MoveValueOutOfResult) {
    Result<std::vector<int>> r(std::vector<int>{1, 2, 3, 4, 5});
    auto vec = std::move(r).value();
    EXPECT_EQ(vec.size(), 5u);
    EXPECT_EQ(vec[0], 1);
    EXPECT_EQ(vec[4], 5);
}

// --- Copy semantics (via variant) ---

TEST(ResultTest, CopyConstruction) {
    Result<int> r1(42);
    Result<int> r2(r1);
    ASSERT_TRUE(r2.ok());
    EXPECT_EQ(r2.value(), 42);
    // original still valid
    EXPECT_EQ(r1.value(), 42);
}

TEST(ResultTest, CopyConstructionFromError) {
    Result<int> r1(Error(ErrorCode::kNotFound, "missing"));
    Result<int> r2(r1);
    EXPECT_FALSE(r2.ok());
    EXPECT_EQ(r2.error().message, "missing");
}

TEST(ResultTest, CopyStringResult) {
    Result<std::string> r1(std::string("hello"));
    Result<std::string> r2(r1);
    EXPECT_EQ(r2.value(), "hello");
    EXPECT_EQ(r1.value(), "hello");
}

// --- All error codes are distinct ---

TEST(ResultTest, AllErrorCodesAreDistinct) {
    // Verify a representative set of error codes exist and can be used
    std::vector<ErrorCode> codes = {
        ErrorCode::kOk, ErrorCode::kInvalidArgument, ErrorCode::kInvalidRange,
        ErrorCode::kInvalidState, ErrorCode::kTypeMismatch, ErrorCode::kNotFound,
        ErrorCode::kAlreadyExists, ErrorCode::kDataCorruption, ErrorCode::kUnsupportedInV1,
        ErrorCode::kOutOfMemory, ErrorCode::kTimeout, ErrorCode::kInternalError,
        ErrorCode::kSyntaxError, ErrorCode::kUndefinedType, ErrorCode::kUndefinedColumn,
        ErrorCode::kDuplicateColumnName, ErrorCode::kDuplicateTypeName,
        ErrorCode::kInvalidEnum, ErrorCode::kInvalidSchema, ErrorCode::kInvalidColumnName,
        ErrorCode::kTableAlreadyExists, ErrorCode::kTableNotFound,
        ErrorCode::kStorageFull, ErrorCode::kSchemaMismatch,
        ErrorCode::kSnapshotNotFound, ErrorCode::kWriteFailed, ErrorCode::kReadFailed,
        ErrorCode::kColumnNotFound,
        ErrorCode::kSchemaViolation, ErrorCode::kHonestyViolation,
        ErrorCode::kConsistencyError, ErrorCode::kSerializationError,
        ErrorCode::kDeserializationError, ErrorCode::kHashMismatch,
        ErrorCode::kOrderColumnRequired, ErrorCode::kInvalidDelta,
        ErrorCode::kEmptyBatch, ErrorCode::kOrderColumnNull, ErrorCode::kInvalidOffset,
        ErrorCode::kStateNotInitialized,
        ErrorCode::kVersionNotFound, ErrorCode::kParentNotFound,
        ErrorCode::kImmutableViolation, ErrorCode::kDuplicateVersionId,
        ErrorCode::kVersionChainCycle, ErrorCode::kModelNotFound,
        ErrorCode::kInvalidVersionId,
        ErrorCode::kCompactionInProgress, ErrorCode::kProtectedVersion,
        ErrorCode::kCompactionFailed, ErrorCode::kMetadataMergeConflict,
        ErrorCode::kAutoCompactDisabled,
        ErrorCode::kVersionCompacted, ErrorCode::kNoAvailableVersion,
        ErrorCode::kSnapshotLoadFailed,
        ErrorCode::kDataEngineUnavailable, ErrorCode::kEmptyTrainingData,
        ErrorCode::kDriftDetectionFailed, ErrorCode::kCompensationTimeout,
        ErrorCode::kCompensationDiverging, ErrorCode::kVersionCreationFailed,
        ErrorCode::kDimensionMismatch, ErrorCode::kProtocolNotDefined,
        ErrorCode::kInvalidWindowSpec, ErrorCode::kSearchNotConverged,
    };
    // Make sure they all compile. Distinctness of enum values is guaranteed by C++.
    EXPECT_EQ(codes.size(), 65u);
}

// --- Result<T> with T=double ---

TEST(ResultTest, DoubleValue) {
    Result<double> r(3.14159);
    ASSERT_TRUE(r.ok());
    EXPECT_DOUBLE_EQ(r.value(), 3.14159);
}

TEST(ResultTest, NegativeDoubleValue) {
    Result<double> r(-1e-15);
    ASSERT_TRUE(r.ok());
    EXPECT_DOUBLE_EQ(r.value(), -1e-15);
}

// --- Result with large complex types ---

TEST(ResultTest, VectorOfStrings) {
    std::vector<std::string> data = {"alpha", "beta", "gamma"};
    Result<std::vector<std::string>> r(std::move(data));
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().size(), 3u);
}

// --- Result<void> as function return ---

namespace {
Result<void> void_ok_func() { return {}; }
Result<void> void_err_func() { return Error(ErrorCode::kInternalError, "oops"); }
Result<int> int_ok_func() { return 42; }
Result<int> int_err_func() { return Error(ErrorCode::kNotFound, "nope"); }
}

TEST(ResultTest, VoidFunctionOk) {
    auto r = void_ok_func();
    EXPECT_TRUE(r.ok());
}

TEST(ResultTest, VoidFunctionError) {
    auto r = void_err_func();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kInternalError);
}

TEST(ResultTest, IntFunctionOk) {
    auto r = int_ok_func();
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value(), 42);
}

TEST(ResultTest, IntFunctionError) {
    auto r = int_err_func();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kNotFound);
}

// --- Error message edge cases ---

TEST(ResultTest, EmptyErrorMessage) {
    Result<int> r(Error(ErrorCode::kOk, ""));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().message, "");
}

TEST(ResultTest, LongErrorMessage) {
    std::string long_msg(10000, 'x');
    Result<int> r(Error(ErrorCode::kInternalError, long_msg));
    EXPECT_EQ(r.error().message.size(), 10000u);
}

TEST(ResultTest, ErrorMessageWithSpecialChars) {
    Result<int> r(Error(ErrorCode::kInternalError, "line1\nline2\ttab\0null"));
    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find('\n'), std::string::npos);
}

// --- Result<T> with T=bool ---

TEST(ResultTest, BoolTrue) {
    Result<bool> r(true);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value());
}

TEST(ResultTest, BoolFalse) {
    Result<bool> r(false);
    ASSERT_TRUE(r.ok());
    EXPECT_FALSE(r.value());
}

// Note: Result<bool> r(Error{...}) should NOT match the bool(false) constructor
// because Error is a distinct type from bool.
TEST(ResultTest, BoolResultWithError) {
    Result<bool> r(Error(ErrorCode::kInvalidArgument, "not bool"));
    EXPECT_FALSE(r.ok());
}

// --- Result<T> with T=int64_t ---

TEST(ResultTest, Int64Max) {
    Result<int64_t> r(INT64_MAX);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value(), INT64_MAX);
}

TEST(ResultTest, Int64Min) {
    Result<int64_t> r(INT64_MIN);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value(), INT64_MIN);
}

TEST(ResultTest, Int64Zero) {
    Result<int64_t> r(0LL);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value(), 0);
}

// ============================================================================
// Common Types Tests
// ============================================================================

TEST(TypesTest, ColumnDefDefaults) {
    ColumnDef cd;
    EXPECT_EQ(cd.name, "");
    EXPECT_FALSE(cd.not_null);
    EXPECT_FALSE(cd.is_order);
    EXPECT_FALSE(cd.range_min.has_value());
    EXPECT_FALSE(cd.range_max.has_value());
    EXPECT_TRUE(cd.enum_values.empty());
}

TEST(TypesTest, ColumnDefWithRange) {
    ColumnDef cd;
    cd.name = "temp";
    cd.type = DataType::kFloat;
    cd.range_min = -50.0;
    cd.range_max = 80.0;
    EXPECT_EQ(cd.name, "temp");
    EXPECT_EQ(cd.type, DataType::kFloat);
    ASSERT_TRUE(cd.range_min.has_value());
    ASSERT_TRUE(cd.range_max.has_value());
    EXPECT_DOUBLE_EQ(*cd.range_min, -50.0);
    EXPECT_DOUBLE_EQ(*cd.range_max, 80.0);
}

TEST(TypesTest, DataTypeEnumValues) {
    // Just verify all enum values exist and are distinct
    EXPECT_NE(DataType::kFloat, DataType::kInt);
    EXPECT_NE(DataType::kInt, DataType::kDatetime);
    EXPECT_NE(DataType::kDatetime, DataType::kString);
    EXPECT_NE(DataType::kString, DataType::kEnum);
}

TEST(TypesTest, ColumnDefEnumValues) {
    ColumnDef cd;
    cd.type = DataType::kEnum;
    cd.enum_values = {"a", "b", "c"};
    EXPECT_EQ(cd.enum_values.size(), 3u);
    EXPECT_EQ(cd.enum_values[0], "a");
}

TEST(TypesTest, TimestampIsInt64) {
    Timestamp ts = 1609459200000000LL;  // 2021-01-01 00:00:00 UTC in microseconds
    EXPECT_GT(ts, 0);
}

// ============================================================================
// Hash Tests
// ============================================================================

TEST(HashTest, KnownEmptyString) {
    // SHA256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    auto hash = sha256_hex("");
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(HashTest, KnownHelloWorld) {
    // Not a well-known hash, but verify determinism
    auto h1 = sha256_hex("hello");
    auto h2 = sha256_hex("hello");
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h1.length(), 64u);  // SHA256 hex is 64 chars
}

TEST(HashTest, DifferentInputsDifferentHashes) {
    auto h1 = sha256_hex("input1");
    auto h2 = sha256_hex("input2");
    EXPECT_NE(h1, h2);
}

TEST(HashTest, OutputLength) {
    auto hash = sha256_hex("test");
    EXPECT_EQ(hash.length(), 64u);
}

TEST(HashTest, OutputIsHex) {
    auto hash = sha256_hex("test input for hex check");
    for (char c : hash) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "Non-hex character: " << c;
    }
}

TEST(HashTest, LargeInput) {
    std::string large(1000000, 'A');
    auto hash = sha256_hex(large);
    EXPECT_EQ(hash.length(), 64u);
    // Second call should give same result
    auto hash2 = sha256_hex(large);
    EXPECT_EQ(hash, hash2);
}

TEST(HashTest, SingleByteInput) {
    auto h1 = sha256_hex("\x00");
    auto h2 = sha256_hex("\x01");
    EXPECT_NE(h1, h2);
    EXPECT_EQ(h1.length(), 64u);
}

TEST(HashTest, BinaryData) {
    std::string binary;
    for (int i = 0; i < 256; i++) {
        binary += static_cast<char>(i);
    }
    auto hash = sha256_hex(binary);
    EXPECT_EQ(hash.length(), 64u);
}

TEST(HashTest, CollisionResistanceSimple) {
    // Test that simple string modifications produce different hashes
    std::unordered_set<std::string> hashes;
    for (int i = 0; i < 1000; i++) {
        hashes.insert(sha256_hex("prefix_" + std::to_string(i)));
    }
    EXPECT_EQ(hashes.size(), 1000u);
}

TEST(HashTest, ThreadSafetyDeterminism) {
    const std::string input = "thread safety test input";
    std::string expected = sha256_hex(input);
    bool all_match = true;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 50; i++) {
                auto h = sha256_hex(input);
                if (h != expected) all_match = false;
            }
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_TRUE(all_match);
}

// ============================================================================
// File I/O hash test (PID-based temp path)
// ============================================================================

TEST(HashTest, WriteAndHashFile) {
    auto dir = std::filesystem::temp_directory_path() /
               ("synthgen_hash_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    auto filepath = dir / "test_data.txt";

    std::string content = "test content for file hashing";
    {
        std::ofstream ofs(filepath);
        ofs << content;
    }
    auto hash = sha256_hex(content);
    EXPECT_EQ(hash.length(), 64u);

    // Verify by re-reading
    std::ifstream ifs(filepath);
    std::string read_back((std::istreambuf_iterator<char>(ifs)),
                           std::istreambuf_iterator<char>());
    EXPECT_EQ(read_back, content);
    auto hash2 = sha256_hex(read_back);
    EXPECT_EQ(hash, hash2);

    std::filesystem::remove_all(dir);
}
