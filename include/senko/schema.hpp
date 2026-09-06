#pragma once

#include "fwd.hpp"
#include "error.hpp"
#include "value.hpp"

#include <string>
#include <vector>
#include <regex>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace senko {

/**
 * @brief Represents the result of a JSON Schema validation.
 */
struct validation_result {
    bool is_valid = true;
    std::string error_message;
    std::string instance_path;

    explicit operator bool() const noexcept { return is_valid; }
};

/**
 * @brief Exception thrown for JSON Schema validation failures when strict mode is used.
 */
class schema_error : public exception {
public:
    explicit schema_error(std::string msg)
        : exception("[senko::schema_error] " + std::move(msg)) {}
};

namespace detail {
inline size_t count_utf8_codepoints(std::string_view s) noexcept {
    size_t count = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0) != 0x80) {
            count++;
        }
    }
    return count;
}
} // namespace detail

/**
 * @brief High-performance, zero-dependency JSON Schema (Draft-07) Validator.
 */
class schema {
public:
    schema() = default;
    explicit schema(value schema_doc) : m_schema(std::move(schema_doc)) {}

    schema(const schema& other) : m_schema(other.m_schema) {}
    schema(schema&& other) noexcept : m_schema(std::move(other.m_schema)) {}
    schema& operator=(const schema& other) {
        if (this != &other) {
            std::lock_guard<std::mutex> lock(m_regex_mutex);
            m_schema = other.m_schema;
            m_regex_cache.clear();
        }
        return *this;
    }
    schema& operator=(schema&& other) noexcept {
        if (this != &other) {
            std::lock_guard<std::mutex> lock(m_regex_mutex);
            m_schema = std::move(other.m_schema);
            m_regex_cache.clear();
        }
        return *this;
    }

    static schema from_json(const value& doc) {
        return schema(doc);
    }

    const value& doc() const noexcept { return m_schema; }

    /**
     * @brief Validates a JSON instance document against this schema.
     * @param instance The JSON value to validate.
     * @param result_out Optional pointer to receive detailed validation diagnostics.
     * @return true if valid, false otherwise.
     */
    bool validate(const value& instance, validation_result* result_out = nullptr) const {
        std::string err;
        std::string path = "#";
        bool ok = validate_internal(m_schema, instance, path, err, 0);

        if (result_out) {
            result_out->is_valid = ok;
            result_out->error_message = err;
            result_out->instance_path = path;
        }
        return ok;
    }

private:
    static constexpr size_t max_depth = 64;
    value m_schema;
    mutable std::mutex m_regex_mutex;
    mutable std::unordered_map<std::string, std::shared_ptr<std::regex>> m_regex_cache;

    const std::regex* get_cached_regex(const std::string& pat) const {
        std::lock_guard<std::mutex> lock(m_regex_mutex);
        auto it = m_regex_cache.find(pat);
        if (it != m_regex_cache.end()) {
            return it->second.get();
        }
        try {
            auto re = std::make_shared<std::regex>(pat);
            m_regex_cache.emplace(pat, re);
            return re.get();
        } catch (...) {
            return nullptr;
        }
    }

    static bool check_type_match(std::string_view expected_type, const value& instance) {
        if (expected_type == "null") return instance.is_null();
        if (expected_type == "boolean") return instance.is_boolean();
        if (expected_type == "integer") {
            if (instance.is_number_integer() || instance.is_number_unsigned()) return true;
            if (instance.is_number_float()) {
                double d = instance.get<double>();
                return std::isfinite(d) && std::floor(d) == d;
            }
            return false;
        }
        if (expected_type == "number") return instance.is_number();
        if (expected_type == "string") return instance.is_string();
        if (expected_type == "array") return instance.is_array();
        if (expected_type == "object") return instance.is_object();
        return true;
    }

    bool validate_internal(const value& sch, const value& inst, std::string& path, std::string& err, size_t depth) const {
        if (depth > max_depth) {
            err = "Maximum schema nesting depth exceeded at " + path;
            return false;
        }

        if (!sch.is_object()) {
            if (sch.is_boolean()) {
                if (!sch.get<bool>()) {
                    err = "Schema boolean is false at " + path;
                    return false;
                }
                return true;
            }
            return true;
        }

        // 1. "type" keyword
        if (sch.contains("type")) {
            const auto& t_node = sch.at("type");
            if (t_node.is_string()) {
                std::string exp = t_node.get<std::string>();
                if (!check_type_match(exp, inst)) {
                    err = "Expected type '" + exp + "', got '" + std::string(inst.type_name()) + "' at " + path;
                    return false;
                }
            } else if (t_node.is_array()) {
                bool any_match = false;
                for (const auto& item : t_node.get_ref_array()) {
                    if (item.is_string() && check_type_match(item.get_ref_string(), inst)) {
                        any_match = true;
                        break;
                    }
                }
                if (!any_match) {
                    err = "Instance type '" + std::string(inst.type_name()) + "' does not match any allowed types at " + path;
                    return false;
                }
            }
        }

        // 2. "const" keyword
        if (sch.contains("const")) {
            if (inst != sch.at("const")) {
                err = "Value does not match 'const' definition at " + path;
                return false;
            }
        }

        // 3. "enum" keyword
        if (sch.contains("enum") && sch.at("enum").is_array()) {
            const auto& arr = sch.at("enum").get_ref_array();
            bool found = false;
            for (const auto& val : arr) {
                if (val == inst) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                err = "Value is not in 'enum' list at " + path;
                return false;
            }
        }

        // 4. Number constraints
        if (inst.is_number()) {
            double v = inst.get<double>();

            if (sch.contains("minimum") && sch.at("minimum").is_number()) {
                double min_val = sch.at("minimum").get<double>();
                if (v < min_val) {
                    err = "Value " + std::to_string(v) + " is less than minimum " + std::to_string(min_val) + " at " + path;
                    return false;
                }
            }
            if (sch.contains("maximum") && sch.at("maximum").is_number()) {
                double max_val = sch.at("maximum").get<double>();
                if (v > max_val) {
                    err = "Value " + std::to_string(v) + " is greater than maximum " + std::to_string(max_val) + " at " + path;
                    return false;
                }
            }
            if (sch.contains("exclusiveMinimum") && sch.at("exclusiveMinimum").is_number()) {
                double ex_min = sch.at("exclusiveMinimum").get<double>();
                if (v <= ex_min) {
                    err = "Value " + std::to_string(v) + " must be strictly greater than " + std::to_string(ex_min) + " at " + path;
                    return false;
                }
            }
            if (sch.contains("exclusiveMaximum") && sch.at("exclusiveMaximum").is_number()) {
                double ex_max = sch.at("exclusiveMaximum").get<double>();
                if (v >= ex_max) {
                    err = "Value " + std::to_string(v) + " must be strictly less than " + std::to_string(ex_max) + " at " + path;
                    return false;
                }
            }
            if (sch.contains("multipleOf") && sch.at("multipleOf").is_number()) {
                double mult = sch.at("multipleOf").get<double>();
                if (mult > 0) {
                    double rem = std::remainder(v, mult);
                    if (std::abs(rem) > 1e-9 && std::abs(std::abs(rem) - mult) > 1e-9) {
                        err = "Value is not a multiple of " + std::to_string(mult) + " at " + path;
                        return false;
                    }
                }
            }
        }

        // 5. String constraints
        if (inst.is_string()) {
            const std::string& s = inst.get_ref_string();
            size_t char_count = detail::count_utf8_codepoints(s);

            if (sch.contains("minLength") && sch.at("minLength").is_number()) {
                size_t min_l = static_cast<size_t>(sch.at("minLength").get<int64_t>());
                if (char_count < min_l) {
                    err = "String length " + std::to_string(char_count) + " is less than minLength " + std::to_string(min_l) + " at " + path;
                    return false;
                }
            }
            if (sch.contains("maxLength") && sch.at("maxLength").is_number()) {
                size_t max_l = static_cast<size_t>(sch.at("maxLength").get<int64_t>());
                if (char_count > max_l) {
                    err = "String length " + std::to_string(char_count) + " is greater than maxLength " + std::to_string(max_l) + " at " + path;
                    return false;
                }
            }
            if (sch.contains("pattern") && sch.at("pattern").is_string()) {
                std::string pat = sch.at("pattern").get<std::string>();
                try {
                    auto re_ptr = get_cached_regex(pat);
                    if (re_ptr && !std::regex_search(s, *re_ptr)) {
                        err = "String does not match regex pattern '" + pat + "' at " + path;
                        return false;
                    }
                } catch (...) {
                    // Ignore regex syntax errors if pattern is invalid
                }
            }
        }

        // 6. Array constraints
        if (inst.is_array()) {
            const auto& arr = inst.get_ref_array();

            if (sch.contains("minItems") && sch.at("minItems").is_number()) {
                size_t min_i = static_cast<size_t>(sch.at("minItems").get<int64_t>());
                if (arr.size() < min_i) {
                    err = "Array size " + std::to_string(arr.size()) + " is less than minItems " + std::to_string(min_i) + " at " + path;
                    return false;
                }
            }
            if (sch.contains("maxItems") && sch.at("maxItems").is_number()) {
                size_t max_i = static_cast<size_t>(sch.at("maxItems").get<int64_t>());
                if (arr.size() > max_i) {
                    err = "Array size " + std::to_string(arr.size()) + " is greater than maxItems " + std::to_string(max_i) + " at " + path;
                    return false;
                }
            }
            if (sch.contains("uniqueItems") && sch.at("uniqueItems").is_boolean() && sch.at("uniqueItems").get<bool>()) {
                for (size_t a = 0; a < arr.size(); ++a) {
                    for (size_t b = a + 1; b < arr.size(); ++b) {
                        if (arr[a] == arr[b]) {
                            err = "Array items are not unique at " + path;
                            return false;
                        }
                    }
                }
            }
            if (sch.contains("items")) {
                const auto& items_sch = sch.at("items");
                if (items_sch.is_object() || items_sch.is_boolean()) {
                    for (size_t idx = 0; idx < arr.size(); ++idx) {
                        std::string sub_path = path + "/" + std::to_string(idx);
                        if (!validate_internal(items_sch, arr[idx], sub_path, err, depth + 1)) {
                            return false;
                        }
                    }
                }
            }
            if (sch.contains("contains")) {
                const auto& contains_sch = sch.at("contains");
                bool found = false;
                for (size_t idx = 0; idx < arr.size(); ++idx) {
                    std::string dummy_err;
                    std::string sub_path = path + "/" + std::to_string(idx);
                    if (validate_internal(contains_sch, arr[idx], sub_path, dummy_err, depth + 1)) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    err = "Array does not contain any item matching 'contains' schema at " + path;
                    return false;
                }
            }
        }

        // 7. Object constraints
        if (inst.is_object()) {
            const auto& obj = inst.get_ref_object();

            if (sch.contains("minProperties") && sch.at("minProperties").is_number()) {
                size_t min_p = static_cast<size_t>(sch.at("minProperties").get<int64_t>());
                if (obj.size() < min_p) {
                    err = "Object has fewer properties (" + std::to_string(obj.size()) + ") than minProperties (" + std::to_string(min_p) + ") at " + path;
                    return false;
                }
            }
            if (sch.contains("maxProperties") && sch.at("maxProperties").is_number()) {
                size_t max_p = static_cast<size_t>(sch.at("maxProperties").get<int64_t>());
                if (obj.size() > max_p) {
                    err = "Object has more properties (" + std::to_string(obj.size()) + ") than maxProperties (" + std::to_string(max_p) + ") at " + path;
                    return false;
                }
            }
            if (sch.contains("required") && sch.at("required").is_array()) {
                for (const auto& req_key : sch.at("required").get_ref_array()) {
                    if (req_key.is_string()) {
                        std::string k = req_key.get<std::string>();
                        if (!inst.contains(k)) {
                            err = "Required property '" + k + "' is missing at " + path;
                            return false;
                        }
                    }
                }
            }
            if (sch.contains("properties") && sch.at("properties").is_object()) {
                const auto& prop_schemas = sch.at("properties").get_ref_object();
                for (const auto& [prop_name, prop_sch] : prop_schemas) {
                    if (inst.contains(prop_name)) {
                        std::string sub_path = path + "/" + prop_name;
                        if (!validate_internal(prop_sch, inst.at(prop_name), sub_path, err, depth + 1)) {
                            return false;
                        }
                    }
                }
            }
            if (sch.contains("additionalProperties")) {
                const auto& add_prop = sch.at("additionalProperties");
                const value* prop_schemas = sch.contains("properties") ? &sch.at("properties") : nullptr;

                for (const auto& [prop_name, val] : obj) {
                    bool is_defined = prop_schemas && prop_schemas->is_object() && prop_schemas->contains(prop_name);
                    if (!is_defined) {
                        if (add_prop.is_boolean() && !add_prop.get<bool>()) {
                            err = "Disallowed additional property '" + prop_name + "' at " + path;
                            return false;
                        } else if (add_prop.is_object()) {
                            std::string sub_path = path + "/" + prop_name;
                            if (!validate_internal(add_prop, val, sub_path, err, depth + 1)) {
                                return false;
                            }
                        }
                    }
                }
            }
        }

        // 8. Combinators: allOf, anyOf, oneOf, not
        if (sch.contains("allOf") && sch.at("allOf").is_array()) {
            for (const auto& sub_sch : sch.at("allOf").get_ref_array()) {
                if (!validate_internal(sub_sch, inst, path, err, depth + 1)) {
                    return false;
                }
            }
        }
        if (sch.contains("anyOf") && sch.at("anyOf").is_array()) {
            bool any_ok = false;
            std::string dummy_err;
            for (const auto& sub_sch : sch.at("anyOf").get_ref_array()) {
                if (validate_internal(sub_sch, inst, path, dummy_err, depth + 1)) {
                    any_ok = true;
                    break;
                }
            }
            if (!any_ok) {
                err = "Instance does not match any sub-schema in 'anyOf' at " + path;
                return false;
            }
        }
        if (sch.contains("oneOf") && sch.at("oneOf").is_array()) {
            int match_count = 0;
            std::string dummy_err;
            for (const auto& sub_sch : sch.at("oneOf").get_ref_array()) {
                if (validate_internal(sub_sch, inst, path, dummy_err, depth + 1)) {
                    match_count++;
                }
            }
            if (match_count != 1) {
                err = "Instance matched " + std::to_string(match_count) + " sub-schemas in 'oneOf' (expected exactly 1) at " + path;
                return false;
            }
        }
        if (sch.contains("not")) {
            std::string dummy_err;
            if (validate_internal(sch.at("not"), inst, path, dummy_err, depth + 1)) {
                err = "Instance matched schema specified in 'not' at " + path;
                return false;
            }
        }

        return true;
    }
};

// Inline helper implementation for value::validate
inline bool value::validate(const value& schema_doc, std::string* error_out) const {
    schema s(schema_doc);
    validation_result res;
    bool ok = s.validate(*this, &res);
    if (!ok && error_out) {
        *error_out = res.error_message;
    }
    return ok;
}

} // namespace senko
