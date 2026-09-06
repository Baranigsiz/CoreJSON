#pragma once

#include "fwd.hpp"
#include "error.hpp"
#include "value.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <sstream>
#include <functional>
#include <cctype>
#include <algorithm>

namespace senko {

class jsonpath_error : public exception {
public:
    explicit jsonpath_error(std::string msg) : exception("[senko::jsonpath_error] " + std::move(msg)) {}
};

namespace detail {

enum class segment_type {
    root,               // $
    child_key,          // .key or ['key']
    child_wildcard,     // .* or [*]
    array_index,        // [0], [-1]
    array_slice,        // [start:end:step]
    descendant_key,     // ..key
    descendant_wildcard,// ..*
    filter              // [?(@.field == val)]
};

struct slice_params {
    bool has_start = false;
    int start = 0;
    bool has_end = false;
    int end = 0;
    int step = 1;
};

struct filter_expr {
    std::string key;
    std::string op; // "==", "!=", "<", "<=", ">", ">="
    std::string value_str;
    bool is_number = false;
    double number_val = 0.0;
    bool is_bool = false;
    bool bool_val = false;

    bool evaluate(const value& item) const {
        if (!item.is_object() || !item.contains(key)) return false;
        const value& field = item.at(key);

        if (is_number && field.is_number()) {
            double v = field.get<double>();
            if (op == "==") return v == number_val;
            if (op == "!=") return v != number_val;
            if (op == "<") return v < number_val;
            if (op == "<=") return v <= number_val;
            if (op == ">") return v > number_val;
            if (op == ">=") return v >= number_val;
        } else if (is_bool && field.is_boolean()) {
            bool b = field.get<bool>();
            if (op == "==") return b == bool_val;
            if (op == "!=") return b != bool_val;
        } else if (field.is_string()) {
            std::string s = field.get<std::string>();
            if (op == "==") return s == value_str;
            if (op == "!=") return s != value_str;
        }
        return false;
    }
};

struct path_segment {
    segment_type type;
    std::string key;
    int index = 0;
    slice_params slice;
    filter_expr filter;
};

template <typename ValueType>
inline void collect_descendant_refs(ValueType& current, std::string_view target_key, std::vector<ValueType*>& results) {
    if (current.is_object()) {
        auto& obj = current.get_ref_object();
        for (auto& pair : obj) {
            if (pair.first == target_key) {
                results.push_back(&(pair.second));
            }
            collect_descendant_refs(pair.second, target_key, results);
        }
    } else if (current.is_array()) {
        auto& arr = current.get_ref_array();
        for (auto& elem : arr) {
            collect_descendant_refs(elem, target_key, results);
        }
    }
}

template <typename ValueType>
inline void collect_all_descendant_refs(ValueType& current, std::vector<ValueType*>& results) {
    if (current.is_object()) {
        auto& obj = current.get_ref_object();
        for (auto& pair : obj) {
            results.push_back(&(pair.second));
            collect_all_descendant_refs(pair.second, results);
        }
    } else if (current.is_array()) {
        auto& arr = current.get_ref_array();
        for (auto& elem : arr) {
            results.push_back(&elem);
            collect_all_descendant_refs(elem, results);
        }
    }
}

inline void collect_descendants(const value& current, std::string_view target_key, std::vector<value>& results) {
    std::vector<const value*> refs;
    collect_descendant_refs(current, target_key, refs);
    for (const auto* r : refs) {
        if (r) results.push_back(*r);
    }
}

inline void collect_all_descendants(const value& current, std::vector<value>& results) {
    std::vector<const value*> refs;
    collect_all_descendant_refs(current, refs);
    for (const auto* r : refs) {
        if (r) results.push_back(*r);
    }
}

inline std::vector<path_segment> parse_jsonpath(std::string_view expr) {
    std::vector<path_segment> segments;
    size_t i = 0;
    size_t len = expr.size();

    // Skip leading whitespace
    while (i < len && std::isspace(static_cast<unsigned char>(expr[i]))) i++;

    if (i >= len || expr[i] != '$') {
        throw jsonpath_error("JSONPath expression must start with '$'");
    }
    segments.push_back({segment_type::root, "", 0, {}, {}});
    i++; // skip '$'

    while (i < len) {
        if (expr[i] == '.') {
            i++;
            if (i < len && expr[i] == '.') {
                // Recursive descent ..
                i++;
                if (i < len && expr[i] == '*') {
                    segments.push_back({segment_type::descendant_wildcard, "", 0, {}, {}});
                    i++;
                } else {
                    size_t start = i;
                    while (i < len && (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_' || expr[i] == '-')) {
                        i++;
                    }
                    if (start == i) throw jsonpath_error("Expected key name after '..'");
                    segments.push_back({segment_type::descendant_key, std::string(expr.substr(start, i - start)), 0, {}, {}});
                }
            } else if (i < len && expr[i] == '*') {
                segments.push_back({segment_type::child_wildcard, "", 0, {}, {}});
                i++;
            } else {
                size_t start = i;
                while (i < len && (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_' || expr[i] == '-')) {
                    i++;
                }
                if (start == i) throw jsonpath_error("Expected key name after '.'");
                segments.push_back({segment_type::child_key, std::string(expr.substr(start, i - start)), 0, {}, {}});
            }
        } else if (expr[i] == '[') {
            i++; // skip '['
            while (i < len && std::isspace(static_cast<unsigned char>(expr[i]))) i++;

            if (i < len && expr[i] == '*') {
                segments.push_back({segment_type::child_wildcard, "", 0, {}, {}});
                i++;
            } else if (i < len && (expr[i] == '\'' || expr[i] == '"')) {
                // ['key']
                char quote = expr[i++];
                size_t start = i;
                while (i < len && expr[i] != quote) i++;
                if (i >= len) throw jsonpath_error("Unterminated quoted key in bracket notation");
                segments.push_back({segment_type::child_key, std::string(expr.substr(start, i - start)), 0, {}, {}});
                i++; // skip closing quote
            } else if (i < len && expr[i] == '?') {
                // Filter [?(@.price < 10)]
                i++; // skip '?'
                while (i < len && std::isspace(static_cast<unsigned char>(expr[i]))) i++;
                if (i < len && expr[i] == '(') i++; // skip '('
                while (i < len && std::isspace(static_cast<unsigned char>(expr[i]))) i++;

                if (i < len && expr[i] == '@') i++; // skip '@'
                if (i < len && expr[i] == '.') i++; // skip '.'

                // Read property name
                size_t start_k = i;
                while (i < len && (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_' || expr[i] == '-')) i++;
                std::string k = std::string(expr.substr(start_k, i - start_k));

                while (i < len && std::isspace(static_cast<unsigned char>(expr[i]))) i++;

                // Read operator
                std::string op;
                if (i + 1 < len && (expr.substr(i, 2) == "==" || expr.substr(i, 2) == "!=" || expr.substr(i, 2) == "<=" || expr.substr(i, 2) == ">=")) {
                    op = std::string(expr.substr(i, 2));
                    i += 2;
                } else if (i < len && (expr[i] == '<' || expr[i] == '>')) {
                    op = std::string(1, expr[i]);
                    i++;
                } else {
                    throw jsonpath_error("Unsupported or missing comparison operator in filter");
                }

                while (i < len && std::isspace(static_cast<unsigned char>(expr[i]))) i++;

                // Read value
                filter_expr filt;
                filt.key = k;
                filt.op = op;

                if (expr[i] == '\'' || expr[i] == '"') {
                    char q = expr[i++];
                    size_t sv = i;
                    while (i < len && expr[i] != q) i++;
                    filt.value_str = std::string(expr.substr(sv, i - sv));
                    i++; // skip quote
                } else {
                    size_t sv = i;
                    while (i < len && expr[i] != ')' && expr[i] != ']' && !std::isspace(static_cast<unsigned char>(expr[i]))) i++;
                    std::string raw_val = std::string(expr.substr(sv, i - sv));
                    if (raw_val == "true") {
                        filt.is_bool = true;
                        filt.bool_val = true;
                    } else if (raw_val == "false") {
                        filt.is_bool = true;
                        filt.bool_val = false;
                    } else {
                        filt.is_number = true;
                        try {
                            filt.number_val = std::stod(raw_val);
                        } catch (...) {
                            throw jsonpath_error("Invalid literal or number value in filter: '" + raw_val + "'");
                        }
                    }
                }

                while (i < len && expr[i] != ']') i++;
                segments.push_back({segment_type::filter, "", 0, {}, filt});

            } else {
                // Check if this is an array slice (contains ':') or a single index
                size_t close_bracket = expr.find(']', i);
                if (close_bracket == std::string_view::npos) {
                    throw jsonpath_error("Missing closing bracket ']' in array subscript");
                }
                std::string_view sub = expr.substr(i, close_bracket - i);
                if (sub.find(':') != std::string_view::npos) {
                    // Array Slice syntax: [start:end:step]
                    slice_params sl;
                    size_t pos = i;

                    // 1. Read start
                    while (pos < close_bracket && std::isspace(static_cast<unsigned char>(expr[pos]))) pos++;
                    if (pos < close_bracket && expr[pos] != ':') {
                        size_t s_start = pos;
                        if (expr[pos] == '-') pos++;
                        while (pos < close_bracket && std::isdigit(static_cast<unsigned char>(expr[pos]))) pos++;
                        try {
                            sl.start = std::stoi(std::string(expr.substr(s_start, pos - s_start)));
                        } catch (...) {
                            throw jsonpath_error("Invalid start index in array slice");
                        }
                        sl.has_start = true;
                    }

                    while (pos < close_bracket && std::isspace(static_cast<unsigned char>(expr[pos]))) pos++;
                    if (pos < close_bracket && expr[pos] == ':') {
                        pos++; // consume first ':'
                    } else {
                        throw jsonpath_error("Expected ':' in array slice");
                    }

                    // 2. Read end
                    while (pos < close_bracket && std::isspace(static_cast<unsigned char>(expr[pos]))) pos++;
                    if (pos < close_bracket && expr[pos] != ':' && expr[pos] != ']') {
                        size_t e_start = pos;
                        if (expr[pos] == '-') pos++;
                        while (pos < close_bracket && std::isdigit(static_cast<unsigned char>(expr[pos]))) pos++;
                        try {
                            sl.end = std::stoi(std::string(expr.substr(e_start, pos - e_start)));
                        } catch (...) {
                            throw jsonpath_error("Invalid end index in array slice");
                        }
                        sl.has_end = true;
                    }

                    // 3. Read optional step
                    while (pos < close_bracket && std::isspace(static_cast<unsigned char>(expr[pos]))) pos++;
                    if (pos < close_bracket && expr[pos] == ':') {
                        pos++; // consume second ':'
                        while (pos < close_bracket && std::isspace(static_cast<unsigned char>(expr[pos]))) pos++;
                        if (pos < close_bracket && expr[pos] != ']') {
                            size_t st_start = pos;
                            if (expr[pos] == '-') pos++;
                            while (pos < close_bracket && std::isdigit(static_cast<unsigned char>(expr[pos]))) pos++;
                            try {
                                sl.step = std::stoi(std::string(expr.substr(st_start, pos - st_start)));
                            } catch (...) {
                                throw jsonpath_error("Invalid step in array slice");
                            }
                            if (sl.step == 0) throw jsonpath_error("Step cannot be 0 in array slice");
                        }
                    }

                    segments.push_back({segment_type::array_slice, "", 0, sl, {}});
                    i = close_bracket;
                } else {
                    // Single index [index]
                    size_t start = i;
                    if (expr[i] == '-') i++;
                    while (i < len && std::isdigit(static_cast<unsigned char>(expr[i]))) i++;
                    if (start == i) throw jsonpath_error("Invalid index in array bracket");
                    try {
                        int idx = std::stoi(std::string(expr.substr(start, i - start)));
                        segments.push_back({segment_type::array_index, "", idx, {}, {}});
                    } catch (...) {
                        throw jsonpath_error("Invalid index in array bracket");
                    }
                }
            }

            while (i < len && expr[i] != ']') i++;
            if (i >= len || expr[i] != ']') throw jsonpath_error("Expected ']' closing bracket");
            i++; // skip ']'
        } else {
            throw jsonpath_error(std::string("Unexpected character '") + expr[i] + "' in JSONPath");
        }
    }

    return segments;
}

template <typename ValueType>
inline std::vector<ValueType*> evaluate_jsonpath_refs_impl(ValueType& root, std::string_view query) {
    auto segments = parse_jsonpath(query);
    std::vector<ValueType*> current_set = {&root};

    for (const auto& seg : segments) {
        std::vector<ValueType*> next_set;

        for (auto* item : current_set) {
            if (!item) continue;
            switch (seg.type) {
                case segment_type::root:
                    next_set.push_back(item);
                    break;
                case segment_type::child_key:
                    if (item->is_object()) {
                        auto* p = item->find(seg.key);
                        if (p) {
                            next_set.push_back(p);
                        }
                    }
                    break;
                case segment_type::child_wildcard:
                    if (item->is_object()) {
                        for (auto& pair : item->get_ref_object()) {
                            next_set.push_back(&(pair.second));
                        }
                    } else if (item->is_array()) {
                        for (auto& elem : item->get_ref_array()) {
                            next_set.push_back(&elem);
                        }
                    }
                    break;
                case segment_type::array_index:
                    if (item->is_array()) {
                        int idx = seg.index;
                        auto& arr = item->get_ref_array();
                        if (idx < 0) idx += static_cast<int>(arr.size());
                        if (idx >= 0 && static_cast<size_t>(idx) < arr.size()) {
                            next_set.push_back(&(arr[static_cast<size_t>(idx)]));
                        }
                    }
                    break;
                case segment_type::array_slice:
                    if (item->is_array()) {
                        auto& arr = item->get_ref_array();
                        int n = static_cast<int>(arr.size());
                        int step = seg.slice.step;
                        if (step == 0) throw jsonpath_error("Step cannot be 0 in array slice");

                        if (step > 0) {
                            int s = 0;
                            if (seg.slice.has_start) {
                                s = seg.slice.start < 0 ? seg.slice.start + n : seg.slice.start;
                                s = (std::max)(0, (std::min)(n, s));
                            }
                            int e = n;
                            if (seg.slice.has_end) {
                                e = seg.slice.end < 0 ? seg.slice.end + n : seg.slice.end;
                                e = (std::max)(0, (std::min)(n, e));
                            }
                            for (int idx = s; idx < e; idx += step) {
                                next_set.push_back(&(arr[static_cast<size_t>(idx)]));
                            }
                        } else {
                            // Negative step
                            int s = n - 1;
                            if (seg.slice.has_start) {
                                s = seg.slice.start < 0 ? seg.slice.start + n : seg.slice.start;
                                s = (std::max)(-1, (std::min)(n - 1, s));
                            }
                            int e = -1;
                            if (seg.slice.has_end) {
                                e = seg.slice.end < 0 ? seg.slice.end + n : seg.slice.end;
                                e = (std::max)(-1, (std::min)(n - 1, e));
                            }
                            for (int idx = s; idx > e; idx += step) {
                                next_set.push_back(&(arr[static_cast<size_t>(idx)]));
                            }
                        }
                    }
                    break;
                case segment_type::descendant_key:
                    collect_descendant_refs(*item, seg.key, next_set);
                    break;
                case segment_type::descendant_wildcard:
                    collect_all_descendant_refs(*item, next_set);
                    break;
                case segment_type::filter:
                    if (item->is_array()) {
                        auto& arr = item->get_ref_array();
                        for (auto& elem : arr) {
                            if (seg.filter.evaluate(elem)) {
                                next_set.push_back(&elem);
                            }
                        }
                    } else if (item->is_object()) {
                        if (seg.filter.evaluate(*item)) {
                            next_set.push_back(item);
                        }
                    }
                    break;
            }
        }

        current_set = std::move(next_set);
        if (current_set.empty()) break;
    }

    return current_set;
}

} // namespace detail

// Zero-copy JSONPath evaluation returning non-owning pointers
inline std::vector<const value*> evaluate_jsonpath_refs(const value& root, std::string_view query) {
    return detail::evaluate_jsonpath_refs_impl(root, query);
}

inline std::vector<value*> evaluate_jsonpath_refs(value& root, std::string_view query) {
    return detail::evaluate_jsonpath_refs_impl(root, query);
}

inline const value* evaluate_jsonpath_first_ref(const value& root, std::string_view query) {
    auto results = evaluate_jsonpath_refs(root, query);
    return results.empty() ? nullptr : results[0];
}

inline value* evaluate_jsonpath_first_ref(value& root, std::string_view query) {
    auto results = evaluate_jsonpath_refs(root, query);
    return results.empty() ? nullptr : results[0];
}

// Deep-copy JSONPath evaluation (built on top of zero-copy engine)
inline std::vector<value> evaluate_jsonpath(const value& root, std::string_view query) {
    auto refs = evaluate_jsonpath_refs(root, query);
    std::vector<value> results;
    results.reserve(refs.size());
    for (const auto* r : refs) {
        if (r) results.push_back(*r);
    }
    return results;
}

inline std::vector<value> value::jsonpath(std::string_view query) const {
    return evaluate_jsonpath(*this, query);
}

inline value value::jsonpath_first(std::string_view query) const {
    const auto* r = jsonpath_first_ref(query);
    if (!r) return value(nullptr);
    return *r;
}

inline std::vector<const value*> value::jsonpath_refs(std::string_view query) const {
    return evaluate_jsonpath_refs(*this, query);
}

inline std::vector<value*> value::jsonpath_refs(std::string_view query) {
    return evaluate_jsonpath_refs(*this, query);
}

inline const value* value::jsonpath_first_ref(std::string_view query) const {
    return evaluate_jsonpath_first_ref(*this, query);
}

inline value* value::jsonpath_first_ref(std::string_view query) {
    return evaluate_jsonpath_first_ref(*this, query);
}

} // namespace senko

