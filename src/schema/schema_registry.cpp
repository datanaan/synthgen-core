#include "schema/schema_registry.h"

namespace synthgen::schema {

Result<void> SchemaRegistry::register_schema(Schema schema) {
    if (schemas_.count(schema.type_name)) {
        return Error(ErrorCode::kDuplicateTypeName,
                     "Type already registered: " + schema.type_name);
    }
    std::string name = schema.type_name;
    schemas_.emplace(name, std::move(schema));
    return {};
}

Result<const Schema*> SchemaRegistry::get_schema(const std::string& type_name) const {
    auto it = schemas_.find(type_name);
    if (it == schemas_.end()) {
        return Error(ErrorCode::kNotFound,
                     "Type not found: " + type_name);
    }
    return &it->second;
}

bool SchemaRegistry::has_schema(const std::string& type_name) const {
    return schemas_.count(type_name) > 0;
}

}  // namespace synthgen::schema
