// SynthGen Core v1 - Component Template Engine v0.1
// Reads a JSON component description and generates .h/.cpp scaffolding
// Usage: scaffold_generator <input.json> <output_dir>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <cstdlib>

// Minimal JSON parser (no external dependency)
struct JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue {
    enum Type { NUL, STRING, NUMBER, ARRAY, OBJECT, BOOL };
    Type type = NUL;
    std::string str;
    double num = 0;
    JsonArray arr;
    JsonObject obj;
    bool boolean = false;

    const JsonValue& operator[](const std::string& key) const {
        static JsonValue null_val;
        auto it = obj.find(key);
        return it != obj.end() ? it->second : null_val;
    }
    const JsonValue& operator[](size_t idx) const {
        static JsonValue null_val;
        return idx < arr.size() ? arr[idx] : null_val;
    }
    bool has(const std::string& key) const {
        return obj.find(key) != obj.end();
    }
};

// Simple JSON parser
class JsonParser {
public:
    explicit JsonParser(const std::string& s) : src_(s), pos_(0) {}

    JsonValue parse() {
        skip_ws();
        return parse_value();
    }

private:
    std::string src_;
    size_t pos_;

    void skip_ws() {
        while (pos_ < src_.size() && std::isspace(src_[pos_])) ++pos_;
    }

    char peek() { return pos_ < src_.size() ? src_[pos_] : '\0'; }
    char next() { return pos_ < src_.size() ? src_[pos_++] : '\0'; }

    JsonValue parse_value() {
        skip_ws();
        if (peek() == '"') return parse_string();
        if (peek() == '{') return parse_object();
        if (peek() == '[') return parse_array();
        if (peek() == 't' || peek() == 'f') return parse_bool();
        if (peek() == 'n') { pos_ += 4; return JsonValue(); }
        return parse_number();
    }

    JsonValue parse_string() {
        next(); // skip "
        std::string s;
        while (peek() != '"') {
            if (peek() == '\\') { next(); s += next(); }
            else s += next();
        }
        next(); // skip "
        JsonValue v; v.type = JsonValue::STRING; v.str = s;
        return v;
    }

    JsonValue parse_number() {
        std::string s;
        while (std::isdigit(peek()) || peek() == '.' || peek() == '-' || peek() == 'e' || peek() == 'E')
            s += next();
        JsonValue v; v.type = JsonValue::NUMBER; v.num = std::stod(s);
        return v;
    }

    JsonValue parse_bool() {
        JsonValue v; v.type = JsonValue::BOOL;
        if (src_.substr(pos_, 4) == "true") { v.boolean = true; pos_ += 4; }
        else { v.boolean = false; pos_ += 5; }
        return v;
    }

    JsonValue parse_array() {
        next(); // skip [
        JsonValue v; v.type = JsonValue::ARRAY;
        skip_ws();
        if (peek() != ']') {
            v.arr.push_back(parse_value());
            skip_ws();
            while (peek() == ',') { next(); v.arr.push_back(parse_value()); skip_ws(); }
        }
        next(); // skip ]
        return v;
    }

    JsonValue parse_object() {
        next(); // skip {
        JsonValue v; v.type = JsonValue::OBJECT;
        skip_ws();
        if (peek() != '}') {
            auto [k, val] = parse_kv();
            v.obj[k] = val;
            skip_ws();
            while (peek() == ',') {
                next();
                auto [k2, val2] = parse_kv();
                v.obj[k2] = val2;
                skip_ws();
            }
        }
        next(); // skip }
        return v;
    }

    std::pair<std::string, JsonValue> parse_kv() {
        skip_ws();
        auto key = parse_string().str;
        skip_ws(); next(); // skip :
        skip_ws();
        return {key, parse_value()};
    }
};

// Template engine: replaces {{VAR}} placeholders
std::string render_template(const std::string& tmpl,
                            const std::map<std::string, std::string>& vars) {
    std::string result = tmpl;
    for (const auto& [key, val] : vars) {
        std::string placeholder = "{{" + key + "}}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.size(), val);
            pos += val.size();
        }
    }
    return result;
}

// PascalCase → snake_case
std::string to_snake_case(const std::string& s) {
    std::string result;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i > 0 && std::isupper(s[i]) && std::islower(s[i-1]))
            result += '_';
        result += std::tolower(s[i]);
    }
    return result;
}

// Build method declarations
std::string build_method_decls(const JsonValue& methods) {
    std::ostringstream oss;
    for (size_t i = 0; i < methods.arr.size(); ++i) {
        const auto& m = methods[i];
        std::string ret = m.has("return") ? m["return"].str : "Result<void>";
        std::string name = m["name"].str;
        oss << "    " << ret << " " << name << "(";

        if (m.has("params")) {
            for (size_t j = 0; j < m["params"].arr.size(); ++j) {
                if (j > 0) oss << ", ";
                oss << m["params"][j]["type"].str << " " << m["params"][j]["name"].str;
            }
        }
        oss << ");\n";
    }
    return oss.str();
}

// Build method implementations with span/metrics scaffolding
std::string build_method_impls(const JsonValue& methods, const std::string& classname,
                                const std::string& spans_str, const std::string& metrics_str) {
    std::ostringstream oss;
    for (size_t i = 0; i < methods.arr.size(); ++i) {
        const auto& m = methods[i];
        std::string ret = m.has("return") ? m["return"].str : "Result<void>";
        std::string name = m["name"].str;

        oss << ret << " " << classname << "::" << name << "(";
        if (m.has("params")) {
            for (size_t j = 0; j < m["params"].arr.size(); ++j) {
                if (j > 0) oss << ", ";
                oss << m["params"][j]["type"].str << " " << m["params"][j]["name"].str;
            }
        }
        oss << ") {\n";
        oss << "    scaffold::SpanGuard span(\"" << to_snake_case(classname)
            << "\", \"" << name << "\", \"trace_id\");\n";

        // Add metrics increment for relevant methods
        if (!metrics_str.empty()) {
            oss << "    scaffold::MetricsRegistry::instance().counter(\""
                << to_snake_case(classname) << "_" << name << "_total\").increment();\n";
        }
        oss << "\n    // TODO: 核心逻辑\n\n";

        // Return statement
        if (ret.find("void") != std::string::npos && ret.find("Result<void>") != std::string::npos) {
            oss << "    return {};\n";
        }
        oss << "}\n\n";
    }
    return oss.str();
}

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "Cannot open file: " << path << std::endl;
        return "";
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "Cannot write file: " << path << std::endl;
        return false;
    }
    f << content;
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.json> <output_dir>" << std::endl;
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_dir = argv[2];

    // Read and parse JSON input
    std::string json_str = read_file(input_path);
    if (json_str.empty()) return 1;

    JsonParser parser(json_str);
    JsonValue desc = parser.parse();

    std::string name = desc["name"].str;
    std::string ns = desc["namespace"].str;

    // Read templates - check SYNTHGEN_TEMPLATES env var, then relative to executable
    std::string tmpl_dir;
    const char* env_tmpl = std::getenv("SYNTHGEN_TEMPLATES");
    if (env_tmpl && env_tmpl[0]) {
        tmpl_dir = std::string(env_tmpl) + "/";
    } else {
        tmpl_dir = std::string(argv[0]);
        auto last_slash = tmpl_dir.find_last_of('/');
        if (last_slash != std::string::npos) tmpl_dir = tmpl_dir.substr(0, last_slash);
        else tmpl_dir = ".";
        tmpl_dir += "/../scaffold_templates/";
    }

    std::string h_tmpl = read_file(tmpl_dir + "component.h.template");
    std::string cpp_tmpl = read_file(tmpl_dir + "component.cpp.template");

    if (h_tmpl.empty() || cpp_tmpl.empty()) {
        std::cerr << "Cannot read templates from " << tmpl_dir << std::endl;
        return 1;
    }

    // Build template variables
    std::map<std::string, std::string> vars;
    vars["NAME"] = name;
    vars["NAME_SNAKE"] = to_snake_case(name);
    vars["NAMESPACE"] = ns;
    vars["INCLUDES"] = "";
    vars["CONSTRUCTOR"] = "    explicit " + name + "(); // TODO: 添加参数";
    vars["CONSTRUCTOR_IMPL"] = name + "::" + name + "() {\n    // TODO: 初始化逻辑\n}";
    vars["METHODS"] = desc.has("methods") ? build_method_decls(desc["methods"]) : "";
    vars["METHOD_IMPLS"] = desc.has("methods")
        ? build_method_impls(desc["methods"], name,
                             desc.has("spans") ? desc["spans"].str : "",
                             desc.has("metrics") ? desc["metrics"].str : "")
        : "";

    // Render
    std::string h_content = render_template(h_tmpl, vars);
    std::string cpp_content = render_template(cpp_tmpl, vars);

    // Write output
    std::string h_path = output_dir + "/" + to_snake_case(name) + ".h";
    std::string cpp_path = output_dir + "/" + to_snake_case(name) + ".cpp";

    if (!write_file(h_path, h_content)) return 1;
    if (!write_file(cpp_path, cpp_content)) return 1;

    std::cout << "Generated: " << h_path << ", " << cpp_path << std::endl;
    return 0;
}
