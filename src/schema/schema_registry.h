#pragma once

#include <string>
#include <unordered_map>

#include "common/result.h"
#include "schema/schema.h"

namespace synthgen::schema {

class SchemaRegistry {
public:
    Result<void> register_schema(Schema schema);
    Result<const Schema*> get_schema(const std::string& type_name) const;
    bool has_schema(const std::string& type_name) const;

private:
    std::unordered_map<std::string, Schema> schemas_;
};

}  // namespace synthgen::schema
