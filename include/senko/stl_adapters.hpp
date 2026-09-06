#pragma once

#include "fwd.hpp"
#include "value.hpp"

#include <optional>
#include <vector>
#include <deque>
#include <list>
#include <set>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <utility>
#include <type_traits>
#include <filesystem>
#include <chrono>

namespace senko {

// ==========================================
// std::optional<T> Adapter
// ==========================================
template <typename T>
struct adl_serializer<std::optional<T>> {
    static void serialize(value& j, const std::optional<T>& opt) {
        if (opt.has_value()) {
            j = *opt;
        } else {
            j = nullptr;
        }
    }

    static void deserialize(const value& j, std::optional<T>& opt) {
        if (j.is_null()) {
            opt = std::nullopt;
        } else {
            opt = j.get<T>();
        }
    }
};

// ==========================================
// std::pair<T1, T2> Adapter
// ==========================================
template <typename T1, typename T2>
struct adl_serializer<std::pair<T1, T2>> {
    static void serialize(value& j, const std::pair<T1, T2>& p) {
        value::array_t arr;
        arr.reserve(2);
        arr.emplace_back(p.first);
        arr.emplace_back(p.second);
        j = std::move(arr);
    }

    static void deserialize(const value& j, std::pair<T1, T2>& p) {
        if (!j.is_array() || j.size() < 2) {
            throw type_error("Expected array with at least 2 elements for std::pair, got " + std::string(j.type_name()));
        }
        p.first = j[0].get<T1>();
        p.second = j[1].get<T2>();
    }
};

// ==========================================
// std::vector<T> Adapter (for non-value types)
// ==========================================
template <typename T>
struct adl_serializer<std::vector<T>, std::enable_if_t<!std::is_same_v<T, value>>> {
    static void serialize(value& j, const std::vector<T>& vec) {
        value::array_t arr;
        arr.reserve(vec.size());
        for (const auto& item : vec) {
            arr.emplace_back(item);
        }
        j = std::move(arr);
    }

    static void deserialize(const value& j, std::vector<T>& vec) {
        if (!j.is_array()) {
            throw type_error("Expected array for std::vector, got " + std::string(j.type_name()));
        }
        vec.clear();
        vec.reserve(j.size());
        for (size_t i = 0; i < j.size(); ++i) {
            vec.push_back(j[i].get<T>());
        }
    }
};

// ==========================================
// std::deque<T> Adapter
// ==========================================
template <typename T>
struct adl_serializer<std::deque<T>> {
    static void serialize(value& j, const std::deque<T>& deq) {
        value::array_t arr;
        arr.reserve(deq.size());
        for (const auto& item : deq) {
            arr.emplace_back(item);
        }
        j = std::move(arr);
    }

    static void deserialize(const value& j, std::deque<T>& deq) {
        if (!j.is_array()) {
            throw type_error("Expected array for std::deque, got " + std::string(j.type_name()));
        }
        deq.clear();
        for (size_t i = 0; i < j.size(); ++i) {
            deq.push_back(j[i].get<T>());
        }
    }
};

// ==========================================
// std::list<T> Adapter
// ==========================================
template <typename T>
struct adl_serializer<std::list<T>> {
    static void serialize(value& j, const std::list<T>& lst) {
        value::array_t arr;
        arr.reserve(lst.size());
        for (const auto& item : lst) {
            arr.emplace_back(item);
        }
        j = std::move(arr);
    }

    static void deserialize(const value& j, std::list<T>& lst) {
        if (!j.is_array()) {
            throw type_error("Expected array for std::list, got " + std::string(j.type_name()));
        }
        lst.clear();
        for (size_t i = 0; i < j.size(); ++i) {
            lst.push_back(j[i].get<T>());
        }
    }
};

// ==========================================
// std::set<T> Adapter
// ==========================================
template <typename T>
struct adl_serializer<std::set<T>> {
    static void serialize(value& j, const std::set<T>& s) {
        value::array_t arr;
        arr.reserve(s.size());
        for (const auto& item : s) {
            arr.emplace_back(item);
        }
        j = std::move(arr);
    }

    static void deserialize(const value& j, std::set<T>& s) {
        if (!j.is_array()) {
            throw type_error("Expected array for std::set, got " + std::string(j.type_name()));
        }
        s.clear();
        for (size_t i = 0; i < j.size(); ++i) {
            s.insert(j[i].get<T>());
        }
    }
};

// ==========================================
// std::unordered_set<T> Adapter
// ==========================================
template <typename T>
struct adl_serializer<std::unordered_set<T>> {
    static void serialize(value& j, const std::unordered_set<T>& s) {
        value::array_t arr;
        arr.reserve(s.size());
        for (const auto& item : s) {
            arr.emplace_back(item);
        }
        j = std::move(arr);
    }

    static void deserialize(const value& j, std::unordered_set<T>& s) {
        if (!j.is_array()) {
            throw type_error("Expected array for std::unordered_set, got " + std::string(j.type_name()));
        }
        s.clear();
        for (size_t i = 0; i < j.size(); ++i) {
            s.insert(j[i].get<T>());
        }
    }
};

// ==========================================
// std::map<std::string, T> Adapter
// ==========================================
template <typename T>
struct adl_serializer<std::map<std::string, T>> {
    static void serialize(value& j, const std::map<std::string, T>& m) {
        value::object_t obj;
        obj.reserve(m.size());
        for (const auto& [k, v] : m) {
            obj.emplace_back(k, value(v));
        }
        j = std::move(obj);
    }

    static void deserialize(const value& j, std::map<std::string, T>& m) {
        if (!j.is_object()) {
            throw type_error("Expected object for std::map, got " + std::string(j.type_name()));
        }
        m.clear();
        const auto& obj = j.get_ref_object();
        for (const auto& [k, v] : obj) {
            m[k] = v.template get<T>();
        }
    }
};

// ==========================================
// std::unordered_map<std::string, T> Adapter
// ==========================================
template <typename T>
struct adl_serializer<std::unordered_map<std::string, T>> {
    static void serialize(value& j, const std::unordered_map<std::string, T>& m) {
        value::object_t obj;
        obj.reserve(m.size());
        for (const auto& [k, v] : m) {
            obj.emplace_back(k, value(v));
        }
        j = std::move(obj);
    }

    static void deserialize(const value& j, std::unordered_map<std::string, T>& m) {
        if (!j.is_object()) {
            throw type_error("Expected object for std::unordered_map, got " + std::string(j.type_name()));
        }
        m.clear();
        const auto& obj = j.get_ref_object();
        for (const auto& [k, v] : obj) {
            m[k] = v.template get<T>();
        }
    }
};

// ==========================================
// std::filesystem::path Adapter
// ==========================================
template <>
struct adl_serializer<std::filesystem::path> {
    static void serialize(value& j, const std::filesystem::path& p) {
        j = p.string();
    }

    static void deserialize(const value& j, std::filesystem::path& p) {
        if (!j.is_string()) {
            throw type_error("Expected string for std::filesystem::path, got " + std::string(j.type_name()));
        }
        p = std::filesystem::path(j.get<std::string>());
    }
};

// ==========================================
// std::chrono::duration Adapter
// ==========================================
template <typename Rep, typename Period>
struct adl_serializer<std::chrono::duration<Rep, Period>> {
    using duration_t = std::chrono::duration<Rep, Period>;

    static void serialize(value& j, const duration_t& d) {
        j = static_cast<int64_t>(d.count());
    }

    static void deserialize(const value& j, duration_t& d) {
        if (!j.is_number()) {
            throw type_error("Expected number for std::chrono::duration, got " + std::string(j.type_name()));
        }
        if (j.is_integer()) {
            d = duration_t(static_cast<Rep>(j.get<int64_t>()));
        } else if (j.is_unsigned()) {
            d = duration_t(static_cast<Rep>(j.get<uint64_t>()));
        } else {
            d = duration_t(static_cast<Rep>(j.get<double>()));
        }
    }
};

} // namespace senko
