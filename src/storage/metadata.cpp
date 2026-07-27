#include "storage/metadata.h"
#include "storage/storage_error.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

namespace synthgen::storage {

// ---------------------------------------------------------------------------
// Simple JSON serialization helpers
// ---------------------------------------------------------------------------

namespace {

std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

std::string serialize_version_meta(const VersionMeta& v) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"version_id\":\"" << escape_json(v.version_id) << "\",";
    oss << "\"table_id\":\"" << escape_json(v.table_id) << "\",";
    oss << "\"created_at\":" << v.created_at << ",";
    oss << "\"row_count\":" << v.row_count << ",";
    oss << "\"schema_hash\":\"" << escape_json(v.schema_hash) << "\"";
    oss << "}";
    return oss.str();
}

std::string serialize_snapshot_ref(const SnapshotRef& s) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"snapshot_id\":\"" << escape_json(s.snapshot_id) << "\",";
    oss << "\"row_count\":" << s.row_count << ",";
    oss << "\"created_at\":" << s.created_at;
    oss << "}";
    return oss.str();
}

std::string serialize_table_metadata(const TableMetadata& t) {
    std::ostringstream oss;
    oss << "{";
    oss << "\"table_id\":\"" << escape_json(t.table_id) << "\",";
    oss << "\"schema_json\":\"" << escape_json(t.schema_json) << "\",";
    oss << "\"schema_hash\":\"" << escape_json(t.schema_hash) << "\",";
    oss << "\"created_at\":" << t.created_at << ",";
    oss << "\"versions\":[";
    for (size_t i = 0; i < t.versions.size(); ++i) {
        if (i > 0) oss << ",";
        oss << serialize_version_meta(t.versions[i]);
    }
    oss << "],";
    oss << "\"snapshots\":[";
    for (size_t i = 0; i < t.snapshots.size(); ++i) {
        if (i > 0) oss << ",";
        oss << serialize_snapshot_ref(t.snapshots[i]);
    }
    oss << "]";
    oss << "}";
    return oss.str();
}

// Simple JSON string parser
class JsonParser {
public:
    explicit JsonParser(const std::string& s) : src_(s), pos_(0) {}

    bool parse_object(std::unordered_map<std::string, std::string>& out) {
        skip_ws();
        if (!expect('{')) return false;
        skip_ws();
        if (peek() == '}') { advance(); return true; }
        while (true) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (!expect(':')) return false;
            skip_ws();
            std::string value;
            if (!parse_value(value)) return false;
            out[key] = value;
            skip_ws();
            if (peek() == '}') { advance(); return true; }
            if (!expect(',')) return false;
        }
    }

private:
    char peek() const {
        return pos_ < src_.size() ? src_[pos_] : '\0';
    }
    char advance() {
        return pos_ < src_.size() ? src_[pos_++] : '\0';
    }
    void skip_ws() {
        while (pos_ < src_.size() &&
               (src_[pos_] == ' ' || src_[pos_] == '\n' ||
                src_[pos_] == '\r' || src_[pos_] == '\t')) {
            ++pos_;
        }
    }
    bool expect(char c) {
        if (peek() == c) { advance(); return true; }
        return false;
    }
    bool parse_string(std::string& out) {
        if (!expect('"')) return false;
        out.clear();
        while (pos_ < src_.size()) {
            char c = advance();
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= src_.size()) return false;
                char e = advance();
                switch (e) {
                    case '"':  out += '"'; break;
                    case '\\': out += '\\'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    default:   out += e; break;
                }
            } else {
                out += c;
            }
        }
        return false;
    }
    bool parse_value(std::string& out) {
        skip_ws();
        if (peek() == '"') {
            return parse_string(out);
        }
        // Array or nested object — capture balanced brackets
        char open = peek();
        if (open == '[' || open == '{') {
            char close = (open == '[') ? ']' : '}';
            int depth = 0;
            out.clear();
            while (pos_ < src_.size()) {
                char c = src_[pos_];
                out += c;
                ++pos_;
                if (c == open) ++depth;
                if (c == close) {
                    --depth;
                    if (depth == 0) return true;
                }
            }
            return false;
        }
        // Number or keyword (true, false, null)
        out.clear();
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ',' || c == '}' || c == ']' || c == ' ' ||
                c == '\n' || c == '\r' || c == '\t') {
                break;
            }
            out += c;
            ++pos_;
        }
        return !out.empty();
    }

    const std::string& src_;
    size_t pos_;
};

VersionMeta parse_version_meta(const std::string& json) {
    VersionMeta v;
    std::unordered_map<std::string, std::string> obj;
    JsonParser p(json);
    p.parse_object(obj);
    v.version_id = obj.count("version_id") ? obj["version_id"] : "";
    v.table_id = obj.count("table_id") ? obj["table_id"] : "";
    v.created_at = obj.count("created_at") ? std::stoll(obj["created_at"]) : 0;
    v.row_count = obj.count("row_count") ? std::stoll(obj["row_count"]) : 0;
    v.schema_hash = obj.count("schema_hash") ? obj["schema_hash"] : "";
    return v;
}

SnapshotRef parse_snapshot_ref(const std::string& json) {
    SnapshotRef s;
    std::unordered_map<std::string, std::string> obj;
    JsonParser p(json);
    p.parse_object(obj);
    s.snapshot_id = obj.count("snapshot_id") ? obj["snapshot_id"] : "";
    s.row_count = obj.count("row_count") ? std::stoll(obj["row_count"]) : 0;
    s.created_at = obj.count("created_at") ? std::stoll(obj["created_at"]) : 0;
    return s;
}

// Parse a JSON array string like [{"a":"b"},{"c":"d"}]
std::vector<std::string> parse_json_array(const std::string& json) {
    std::vector<std::string> items;
    size_t pos = json.find('[');
    if (pos == std::string::npos) return items;
    ++pos;
    int depth = 0;
    std::string current;
    bool in_string = false;
    while (pos < json.size()) {
        char c = json[pos++];
        if (in_string) {
            current += c;
            if (c == '\\' && pos < json.size()) {
                current += json[pos++];
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            current += c;
        } else if (c == '{') {
            if (depth == 0) { current.clear(); current += '{'; }
            else current += c;
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                items.push_back(current + "}");
            } else {
                current += c;
            }
        } else if (c == ',' && depth == 0) {
            // separator between top-level objects
        } else if (depth > 0) {
            current += c;
        }
    }
    return items;
}

TableMetadata parse_table_metadata(const std::string& json) {
    TableMetadata t;
    std::unordered_map<std::string, std::string> obj;
    JsonParser p(json);
    p.parse_object(obj);
    t.table_id = obj.count("table_id") ? obj["table_id"] : "";
    t.schema_json = obj.count("schema_json") ? obj["schema_json"] : "";
    t.schema_hash = obj.count("schema_hash") ? obj["schema_hash"] : "";
    t.created_at = obj.count("created_at") ? std::stoll(obj["created_at"]) : 0;

    if (obj.count("versions")) {
        auto items = parse_json_array(obj["versions"]);
        for (const auto& item : items) {
            t.versions.push_back(parse_version_meta(item));
        }
    }
    if (obj.count("snapshots")) {
        auto items = parse_json_array(obj["snapshots"]);
        for (const auto& item : items) {
            t.snapshots.push_back(parse_snapshot_ref(item));
        }
    }
    return t;
}

Timestamp now_micros() {
    auto now = std::chrono::system_clock::now();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch());
    return micros.count();
}

std::string compute_schema_hash(const std::string& schema_json) {
    // Simple hash: sum of char values * 31, as a hex string.
    // Not cryptographically secure, but sufficient for change detection.
    uint64_t h = 0xcbf29ce484222325ULL;
    for (char c : schema_json) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= 0x100000001b3ULL;
    }
    std::ostringstream oss;
    oss << std::hex << h;
    return oss.str();
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// MetadataManager
// ---------------------------------------------------------------------------

MetadataManager::MetadataManager(const std::filesystem::path& table_dir)
    : table_dir_(table_dir) {
    std::error_code ec;
    std::filesystem::create_directories(table_dir_, ec);
    // ignore errors — will be caught on flush/reload
}

Result<void> MetadataManager::create_table(const std::string& table_id,
                                            const std::string& schema_json) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tables_.count(table_id)) {
        return MakeStorageError(ErrorCode::kTableAlreadyExists,
                                "Table already exists: " + table_id);
    }

    TableMetadata meta;
    meta.table_id = table_id;
    meta.schema_json = schema_json;
    meta.schema_hash = compute_schema_hash(schema_json);
    meta.created_at = now_micros();

    // Create table directory
    auto tbl_dir = table_dir_ / table_id;
    std::error_code ec;
    if (!std::filesystem::create_directories(tbl_dir, ec) && ec) {
        return MakeStorageError(ErrorCode::kWriteFailed,
                                "Cannot create table directory: " +
                                    tbl_dir.string() + " (" + ec.message() + ")");
    }

    tables_[table_id] = std::move(meta);
    return {};
}

Result<const TableMetadata*> MetadataManager::get_table(
    const std::string& table_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tables_.find(table_id);
    if (it == tables_.end()) {
        return MakeStorageError(ErrorCode::kTableNotFound,
                                "Table not found: " + table_id);
    }
    return &it->second;
}

Result<void> MetadataManager::add_version(const std::string& table_id,
                                           VersionMeta version) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tables_.find(table_id);
    if (it == tables_.end()) {
        return MakeStorageError(ErrorCode::kTableNotFound,
                                "Table not found: " + table_id);
    }
    it->second.versions.push_back(std::move(version));
    return {};
}

Result<void> MetadataManager::add_snapshot(const std::string& table_id,
                                            SnapshotRef snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tables_.find(table_id);
    if (it == tables_.end()) {
        return MakeStorageError(ErrorCode::kTableNotFound,
                                "Table not found: " + table_id);
    }
    it->second.snapshots.push_back(std::move(snapshot));
    return {};
}

int64_t MetadataManager::next_part_number() {
    std::lock_guard<std::mutex> lock(mutex_);
    return next_part_++;
}

Result<void> MetadataManager::flush() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Write all table metadata to a single metadata.json per table
    for (const auto& [table_id, meta] : tables_) {
        auto tbl_dir = table_dir_ / table_id;
        std::error_code ec;
        std::filesystem::create_directories(tbl_dir, ec);
        if (ec) {
            return MakeStorageError(
                ErrorCode::kWriteFailed,
                "Cannot create table directory: " + tbl_dir.string());
        }

        std::string json = serialize_table_metadata(meta);
        auto meta_path = tbl_dir / "metadata.json";
        auto tmp_path = tbl_dir / "metadata.json.tmp";

        {
            std::ofstream ofs(tmp_path, std::ios::trunc);
            if (!ofs) {
                return MakeStorageError(
                    ErrorCode::kWriteFailed,
                    "Cannot open temp file: " + tmp_path.string());
            }
            ofs << json;
            ofs.close();
            if (ofs.fail()) {
                return MakeStorageError(
                    ErrorCode::kWriteFailed,
                    "Failed to write metadata temp file: " + tmp_path.string());
            }
        }

        std::filesystem::rename(tmp_path, meta_path, ec);
        if (ec) {
            return MakeStorageError(
                ErrorCode::kWriteFailed,
                "Cannot rename metadata file: " + meta_path.string() +
                    " (" + ec.message() + ")");
        }
    }
    return {};
}

Result<void> MetadataManager::reload() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Clear existing tables and reload from disk
    tables_.clear();
    next_part_ = 1;

    std::error_code ec;
    if (!std::filesystem::exists(table_dir_, ec) || ec) {
        return {};  // no data directory yet, that's fine
    }

    for (const auto& entry : std::filesystem::directory_iterator(table_dir_, ec)) {
        if (!entry.is_directory()) continue;
        auto meta_path = entry.path() / "metadata.json";
        if (!std::filesystem::exists(meta_path, ec) || ec) continue;

        std::ifstream ifs(meta_path);
        if (!ifs) {
            return MakeStorageError(
                ErrorCode::kReadFailed,
                "Cannot open metadata file: " + meta_path.string());
        }
        std::string json((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
        if (json.empty()) continue;

        auto meta = parse_table_metadata(json);
        std::string tid = meta.table_id;
        tables_[tid] = std::move(meta);

        // Update next_part_ to account for existing versions
        auto& t = tables_[tid];
        int64_t max_part = 0;
        for (const auto& v : t.versions) {
            // Extract part number from version_id like "v_N"
            auto pos = v.version_id.find('_');
            if (pos != std::string::npos) {
                try {
                    int64_t pn = std::stoll(v.version_id.substr(pos + 1));
                    if (pn >= max_part) max_part = pn;
                } catch (...) {}
            }
        }
        if (max_part >= next_part_) next_part_ = max_part + 1;
    }
    return {};
}

}  // namespace synthgen::storage
