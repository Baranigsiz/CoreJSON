#pragma once

#include "fwd.hpp"
#include "error.hpp"
#include "value.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <sstream>

namespace senko {

/**
 * @brief RFC 6901 JSON Pointer implementation.
 */
class json_pointer {
public:
    json_pointer() = default;

    explicit json_pointer(std::string_view s) {
        if (s.empty()) return;
        if (s[0] != '/') {
            throw pointer_error("JSON Pointer must start with '/' or be empty");
        }

        size_t start = 1;
        while (start <= s.size()) {
            size_t next_slash = s.find('/', start);
            if (next_slash == std::string_view::npos) {
                next_slash = s.size();
            }
            m_tokens.push_back(unescape(s.substr(start, next_slash - start)));
            start = next_slash + 1;
        }
    }

    const std::vector<std::string>& tokens() const noexcept { return m_tokens; }
    bool empty() const noexcept { return m_tokens.empty(); }

    std::string to_string() const {
        if (m_tokens.empty()) return "";
        std::string result;
        for (const auto& tok : m_tokens) {
            result += '/' + escape(tok);
        }
        return result;
    }

    void push_back(std::string token) {
        m_tokens.push_back(std::move(token));
    }

    void pop_back() {
        if (!m_tokens.empty()) m_tokens.pop_back();
    }

    json_pointer parent_pointer() const {
        if (empty()) {
            throw pointer_error("Cannot get parent_pointer() of root pointer");
        }
        json_pointer res = *this;
        res.pop_back();
        return res;
    }

    const std::string& back() const {
        if (empty()) {
            throw pointer_error("Cannot call back() on empty JSON Pointer");
        }
        return m_tokens.back();
    }

    json_pointer operator/(std::string_view token) const {
        json_pointer res = *this;
        res.push_back(std::string(token));
        return res;
    }

    json_pointer operator/(size_t index) const {
        json_pointer res = *this;
        res.push_back(std::to_string(index));
        return res;
    }

    json_pointer& operator/=(std::string_view token) {
        push_back(std::string(token));
        return *this;
    }

    json_pointer& operator/=(size_t index) {
        push_back(std::to_string(index));
        return *this;
    }

    bool operator==(const json_pointer& other) const noexcept {
        return m_tokens == other.m_tokens;
    }

    bool operator!=(const json_pointer& other) const noexcept {
        return m_tokens != other.m_tokens;
    }

    bool operator<(const json_pointer& other) const noexcept {
        return m_tokens < other.m_tokens;
    }

    bool contains(const value& root) const noexcept {
        const value* cur = &root;
        for (const auto& token : m_tokens) {
            if (cur->is_object()) {
                const auto* ptr = cur->find(token);
                if (!ptr) return false;
                cur = ptr;
            } else if (cur->is_array()) {
                if (token.empty() || token == "-") return false;
                if (token.size() > 1 && token[0] == '0') return false;
                size_t idx = 0;
                for (char c : token) {
                    if (c < '0' || c > '9') return false;
                    if (idx > (std::numeric_limits<size_t>::max() - (c - '0')) / 10) return false;
                    idx = idx * 10 + (c - '0');
                }
                if (idx >= cur->size()) return false;
                cur = &(*cur)[idx];
            } else {
                return false;
            }
        }
        return true;
    }

    static size_t parse_array_index(const std::string& token) {
        if (token.empty()) {
            throw pointer_error("Empty array index in JSON Pointer");
        }
        if (token == "-") {
            throw pointer_error("Cannot resolve '-' array index token for reading");
        }
        if (token.size() > 1 && token[0] == '0') {
            throw pointer_error("Leading zeros not permitted in JSON Pointer array index: '" + token + "'");
        }
        for (char c : token) {
            if (c < '0' || c > '9') {
                throw pointer_error("Invalid array index in JSON Pointer: '" + token + "'");
            }
        }
        try {
            return std::stoull(token);
        } catch (...) {
            throw pointer_error("Array index overflow in JSON Pointer: '" + token + "'");
        }
    }

    // Resolves pointer against a root JSON value (throws out_of_range / type_error / pointer_error)
    value& resolve(value& root) const {
        value* cur = &root;
        for (const auto& token : m_tokens) {
            if (cur->is_object()) {
                if (!cur->contains(token)) {
                    throw out_of_range("JSON Pointer token not found in object: '" + token + "'");
                }
                cur = &(*cur)[token];
            } else if (cur->is_array()) {
                size_t idx = parse_array_index(token);
                cur = &cur->at(idx);
            } else {
                throw type_error("Cannot navigate through primitive JSON value with token: '" + token + "'");
            }
        }
        return *cur;
    }

    const value& resolve(const value& root) const {
        const value* cur = &root;
        for (const auto& token : m_tokens) {
            if (cur->is_object()) {
                if (!cur->contains(token)) {
                    throw out_of_range("JSON Pointer token not found in object: '" + token + "'");
                }
                cur = &cur->at(token);
            } else if (cur->is_array()) {
                size_t idx = parse_array_index(token);
                cur = &cur->at(idx);
            } else {
                throw type_error("Cannot navigate through primitive JSON value with token: '" + token + "'");
            }
        }
        return *cur;
    }

    static std::string unescape(std::string_view s) {
        std::string res;
        res.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '~' && i + 1 < s.size()) {
                if (s[i + 1] == '0') {
                    res.push_back('~');
                    i++;
                } else if (s[i + 1] == '1') {
                    res.push_back('/');
                    i++;
                } else {
                    res.push_back(s[i]);
                }
            } else {
                res.push_back(s[i]);
            }
        }
        return res;
    }

    static std::string escape(std::string_view s) {
        std::string res;
        for (char c : s) {
            if (c == '~') res += "~0";
            else if (c == '/') res += "~1";
            else res.push_back(c);
        }
        return res;
    }

private:
    std::vector<std::string> m_tokens;
};

// Implement value pointer helper methods
inline value& value::at_ptr(const json_pointer& ptr) {
    return ptr.resolve(*this);
}

inline const value& value::at_ptr(const json_pointer& ptr) const {
    return ptr.resolve(*this);
}

inline value& value::operator[](const json_pointer& ptr) {
    return ptr.resolve(*this);
}

inline const value& value::operator[](const json_pointer& ptr) const {
    return ptr.resolve(*this);
}

inline bool value::contains(const json_pointer& ptr) const noexcept {
    return ptr.contains(*this);
}

namespace detail {
inline void flatten_recursive(const value& current, const std::string& current_path, value& result) {
    if (current.is_object()) {
        if (current.empty()) {
            result[current_path] = value::object();
        } else {
            for (const auto& [k, v] : current.get_ref_object()) {
                std::string next_path = current_path + "/" + json_pointer::escape(k);
                flatten_recursive(v, next_path, result);
            }
        }
    } else if (current.is_array()) {
        if (current.empty()) {
            result[current_path] = value::array();
        } else {
            for (size_t i = 0; i < current.size(); ++i) {
                std::string next_path = current_path + "/" + std::to_string(i);
                flatten_recursive(current[i], next_path, result);
            }
        }
    } else {
        result[current_path] = current;
    }
}
} // namespace detail

inline value value::flatten() const {
    value result = value::object();
    if (is_null() || is_boolean() || is_number() || is_string()) {
        result[""] = *this;
        return result;
    }
    if ((is_object() || is_array()) && empty()) {
        return value::object();
    }
    detail::flatten_recursive(*this, "", result);
    return result;
}

inline value value::unflatten() const {
    if (!is_object()) {
        throw type_error("unflatten() requires an object with JSON Pointer keys");
    }
    if (empty()) {
        return value::object();
    }

    // Check if it's a root primitive
    if (contains("")) {
        return at("");
    }

    value result;
    bool is_root_array = false;

    // First check if all root tokens are numbers
    bool first = true;
    for (const auto& [k, v] : get_ref_object()) {
        json_pointer ptr(k);
        if (!ptr.tokens().empty()) {
            const std::string& first_tok = ptr.tokens()[0];
            bool is_num = !first_tok.empty() && std::all_of(first_tok.begin(), first_tok.end(), [](char c){ return std::isdigit(static_cast<unsigned char>(c)); });
            if (first) {
                is_root_array = is_num;
                first = false;
            }
        }
    }

    result = is_root_array ? value::array() : value::object();

    for (const auto& [k, v] : get_ref_object()) {
        json_pointer ptr(k);
        const auto& tokens = ptr.tokens();
        if (tokens.empty()) continue;

        value* cur = &result;
        for (size_t i = 0; i < tokens.size(); ++i) {
            const std::string& tok = tokens[i];
            bool is_last = (i + 1 == tokens.size());
            bool next_is_num = false;
            if (!is_last) {
                const std::string& next_tok = tokens[i + 1];
                next_is_num = !next_tok.empty() && std::all_of(next_tok.begin(), next_tok.end(), [](char c){ return std::isdigit(static_cast<unsigned char>(c)); });
            }

            if (cur->is_object()) {
                if (is_last) {
                    (*cur)[tok] = v;
                } else {
                    if (!cur->contains(tok)) {
                        (*cur)[tok] = next_is_num ? value::array() : value::object();
                    }
                    cur = &(*cur)[tok];
                }
            } else if (cur->is_array()) {
                size_t idx = json_pointer::parse_array_index(tok);
                if (idx > 100000) {
                    throw pointer_error("Array index in unflatten exceeds maximum allowed limit: " + tok);
                }
                if (idx >= cur->size()) {
                    while (cur->size() <= idx) {
                        cur->push_back(value(nullptr));
                    }
                }
                if (is_last) {
                    (*cur)[idx] = v;
                } else {
                    if ((*cur)[idx].is_null()) {
                        (*cur)[idx] = next_is_num ? value::array() : value::object();
                    }
                    cur = &(*cur)[idx];
                }
            }
        }
    }

    return result;
}

} // namespace senko

namespace std {
template <>
struct hash<senko::json_pointer> {
    size_t operator()(const senko::json_pointer& ptr) const noexcept {
        size_t h = 0;
        for (const auto& tok : ptr.tokens()) {
            h ^= std::hash<std::string>{}(tok) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};
} // namespace std
