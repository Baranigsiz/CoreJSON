#pragma once

#include "fwd.hpp"
#include "error.hpp"

#include <variant>
#include <string>
#include <vector>
#include <utility>
#include <algorithm>
#include <type_traits>
#include <iostream>
#include <initializer_list>
#include <memory>

namespace senko {

class value {
public:
    using array_t = std::vector<value>;
    using object_t = std::vector<std::pair<std::string, value>>;

private:
    using variant_t = std::variant<
        std::nullptr_t,
        bool,
        int64_t,
        uint64_t,
        double,
        std::string,
        array_t,
        object_t
    >;

    variant_t m_data;

public:
    // ==========================================
    // Constructors & Assignment
    // ==========================================

    value() noexcept : m_data(nullptr) {}
    value(const value&) = default;
    value(value&&) noexcept = default;
    value& operator=(const value&) = default;
    value& operator=(value&&) noexcept = default;

    value(std::nullptr_t) noexcept : m_data(nullptr) {}
    value(bool b) noexcept : m_data(b) {}

    // Signed integers
    template <typename T, typename std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T> && !std::is_same_v<T, bool>, int> = 0>
    value(T val) noexcept : m_data(static_cast<int64_t>(val)) {}

    // Unsigned integers
    template <typename T, typename std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T> && !std::is_same_v<T, bool>, int> = 0>
    value(T val) noexcept : m_data(static_cast<uint64_t>(val)) {}

    // Floating point
    template <typename T, typename std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    value(T val) noexcept : m_data(static_cast<double>(val)) {}

    // Strings
    value(const char* s) : m_data(s ? std::string(s) : std::string{}) {}
    value(std::string s) : m_data(std::move(s)) {}
    value(std::string_view sv) : m_data(std::string(sv)) {}

    // Structured types
    value(array_t arr) : m_data(std::move(arr)) {}
    value(object_t obj) : m_data(std::move(obj)) {}

    // Construct empty container of given type
    explicit value(value_t t) {
        switch (t) {
            case value_t::null: m_data = nullptr; break;
            case value_t::boolean: m_data = false; break;
            case value_t::number_integer: m_data = int64_t{0}; break;
            case value_t::number_unsigned: m_data = uint64_t{0}; break;
            case value_t::number_float: m_data = 0.0; break;
            case value_t::string: m_data = std::string{}; break;
            case value_t::array: m_data = array_t{}; break;
            case value_t::object: m_data = object_t{}; break;
        }
    }

    // Direct reference getters for high performance
    std::string& get_ref_string() {
        if (!is_string()) throw type_error("Expected string, got " + std::string(type_name()));
        return std::get<std::string>(m_data);
    }
    const std::string& get_ref_string() const {
        if (!is_string()) throw type_error("Expected string, got " + std::string(type_name()));
        return std::get<std::string>(m_data);
    }

    array_t& get_ref_array() {
        if (!is_array()) throw type_error("Expected array, got " + std::string(type_name()));
        return std::get<array_t>(m_data);
    }
    const array_t& get_ref_array() const {
        if (!is_array()) throw type_error("Expected array, got " + std::string(type_name()));
        return std::get<array_t>(m_data);
    }

    object_t& get_ref_object() {
        if (!is_object()) throw type_error("Expected object, got " + std::string(type_name()));
        return std::get<object_t>(m_data);
    }
    const object_t& get_ref_object() const {
        if (!is_object()) throw type_error("Expected object, got " + std::string(type_name()));
        return std::get<object_t>(m_data);
    }

    // Initializer list constructor (Smart Object / Array detection)
    value(std::initializer_list<value> init) {
        bool is_object_like = (init.size() > 0);
        for (const auto& elem : init) {
            if (!elem.is_array() || elem.get_ref_array().size() != 2 || !elem.get_ref_array()[0].is_string()) {
                is_object_like = false;
                break;
            }
        }

        if (is_object_like) {
            object_t obj;
            obj.reserve(init.size());
            for (const auto& elem : init) {
                const auto& sub_arr = elem.get_ref_array();
                obj.emplace_back(sub_arr[0].get_ref_string(), sub_arr[1]);
            }
            m_data = std::move(obj);
        } else {
            m_data = array_t(init.begin(), init.end());
        }
    }

    // Static factories
    static value array() { return value(value_t::array); }
    static value array(std::initializer_list<value> init) {
        array_t arr(init.begin(), init.end());
        return value(std::move(arr));
    }
    static value object() { return value(value_t::object); }
    static value object(std::initializer_list<std::pair<std::string, value>> init) {
        object_t obj;
        obj.reserve(init.size());
        for (const auto& pair : init) {
            obj.push_back(pair);
        }
        return value(std::move(obj));
    }

    // Generic ADL constructor (strictly enabled ONLY for custom user types)
    template <typename T, typename std::enable_if_t<
        !std::is_same_v<std::decay_t<T>, value> &&
        !std::is_arithmetic_v<std::decay_t<T>> &&
        !std::is_same_v<std::decay_t<T>, std::string> &&
        !std::is_same_v<std::decay_t<T>, std::string_view> &&
        !std::is_same_v<std::decay_t<T>, const char*> &&
        !std::is_same_v<std::decay_t<T>, char*> &&
        !std::is_same_v<std::decay_t<T>, std::nullptr_t> &&
        !std::is_same_v<std::decay_t<T>, array_t> &&
        !std::is_same_v<std::decay_t<T>, object_t>, int> = 0>
    value(const T& custom) {
        adl_serializer<T>::serialize(*this, custom);
    }

    template <typename T, typename std::enable_if_t<
        !std::is_same_v<std::decay_t<T>, value>, int> = 0>
    value& operator=(T&& val) {
        *this = value(std::forward<T>(val));
        return *this;
    }

    // ==========================================
    // Type Inspection
    // ==========================================

    value_t type() const noexcept {
        switch (m_data.index()) {
            case 0: return value_t::null;
            case 1: return value_t::boolean;
            case 2: return value_t::number_integer;
            case 3: return value_t::number_unsigned;
            case 4: return value_t::number_float;
            case 5: return value_t::string;
            case 6: return value_t::array;
            case 7: return value_t::object;
            default: return value_t::null;
        }
    }

    std::string_view type_name() const noexcept {
        return to_string(type());
    }

    bool is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(m_data); }
    bool is_boolean() const noexcept { return std::holds_alternative<bool>(m_data); }
    bool is_number_integer() const noexcept { return std::holds_alternative<int64_t>(m_data); }
    bool is_number_unsigned() const noexcept { return std::holds_alternative<uint64_t>(m_data); }
    bool is_number_float() const noexcept { return std::holds_alternative<double>(m_data); }
    bool is_number() const noexcept {
        return is_number_integer() || is_number_unsigned() || is_number_float();
    }
    bool is_string() const noexcept { return std::holds_alternative<std::string>(m_data); }
    bool is_array() const noexcept { return std::holds_alternative<array_t>(m_data); }
    bool is_object() const noexcept { return std::holds_alternative<object_t>(m_data); }
    bool is_primitive() const noexcept { return is_null() || is_boolean() || is_number() || is_string(); }
    bool is_structured() const noexcept { return is_array() || is_object(); }

    // ==========================================
    // Value Accessors (get, get_to, value)
    // ==========================================

    template <typename T>
    T get() const {
        if constexpr (std::is_same_v<T, bool>) {
            if (!is_boolean()) throw type_error("Expected boolean, got " + std::string(type_name()));
            return std::get<bool>(m_data);
        } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
            if (is_number_integer()) return static_cast<T>(std::get<int64_t>(m_data));
            if (is_number_unsigned()) return static_cast<T>(std::get<uint64_t>(m_data));
            if (is_number_float()) return static_cast<T>(std::get<double>(m_data));
            throw type_error("Expected integer number, got " + std::string(type_name()));
        } else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) {
            if (is_number_unsigned()) return static_cast<T>(std::get<uint64_t>(m_data));
            if (is_number_integer()) {
                int64_t v = std::get<int64_t>(m_data);
                if (v < 0) throw type_error("Cannot convert negative integer to unsigned");
                return static_cast<T>(v);
            }
            if (is_number_float()) return static_cast<T>(std::get<double>(m_data));
            throw type_error("Expected unsigned number, got " + std::string(type_name()));
        } else if constexpr (std::is_floating_point_v<T>) {
            if (is_number_float()) return static_cast<T>(std::get<double>(m_data));
            if (is_number_integer()) return static_cast<T>(std::get<int64_t>(m_data));
            if (is_number_unsigned()) return static_cast<T>(std::get<uint64_t>(m_data));
            throw type_error("Expected floating-point number, got " + std::string(type_name()));
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (!is_string()) throw type_error("Expected string, got " + std::string(type_name()));
            return std::get<std::string>(m_data);
        } else if constexpr (std::is_same_v<T, std::string_view>) {
            if (!is_string()) throw type_error("Expected string, got " + std::string(type_name()));
            return std::string_view(std::get<std::string>(m_data));
        } else if constexpr (std::is_same_v<T, array_t>) {
            if (!is_array()) throw type_error("Expected array, got " + std::string(type_name()));
            return std::get<array_t>(m_data);
        } else if constexpr (std::is_same_v<T, object_t>) {
            if (!is_object()) throw type_error("Expected object, got " + std::string(type_name()));
            return std::get<object_t>(m_data);
        } else if constexpr (std::is_same_v<T, value>) {
            return *this;
        } else {
            T target{};
            adl_serializer<T>::deserialize(*this, target);
            return target;
        }
    }

    template <typename T>
    void get_to(T& val) const {
        val = get<T>();
    }

    template <typename T>
    T value_or(std::string_view key, const T& default_value) const {
        if (!is_object()) return default_value;
        const auto* ptr = find(key);
        if (!ptr) return default_value;
        try {
            return ptr->get<T>();
        } catch (...) {
            return default_value;
        }
    }

    template <typename T>
    T value_or(size_t index, const T& default_value) const {
        if (!is_array()) return default_value;
        const auto& arr = std::get<array_t>(m_data);
        if (index >= arr.size()) return default_value;
        try {
            return arr[index].get<T>();
        } catch (...) {
            return default_value;
        }
    }

    // ==========================================
    // Container Operations & Indexing
    // ==========================================

    size_t size() const noexcept {
        if (is_array()) return std::get<array_t>(m_data).size();
        if (is_object()) return std::get<object_t>(m_data).size();
        if (is_null()) return 0;
        return 1;
    }

    bool empty() const noexcept {
        if (is_array()) return std::get<array_t>(m_data).empty();
        if (is_object()) return std::get<object_t>(m_data).empty();
        if (is_null()) return true;
        return false;
    }

    void clear() noexcept {
        if (is_array()) std::get<array_t>(m_data).clear();
        else if (is_object()) std::get<object_t>(m_data).clear();
        else if (is_string()) std::get<std::string>(m_data).clear();
        else m_data = nullptr;
    }

    value* find(std::string_view key) noexcept {
        if (!is_object()) return nullptr;
        auto& obj = std::get<object_t>(m_data);
        for (auto& pair : obj) {
            if (pair.first == key) return &pair.second;
        }
        return nullptr;
    }

    const value* find(std::string_view key) const noexcept {
        if (!is_object()) return nullptr;
        const auto& obj = std::get<object_t>(m_data);
        for (const auto& pair : obj) {
            if (pair.first == key) return &pair.second;
        }
        return nullptr;
    }

    bool contains(std::string_view key) const noexcept {
        return find(key) != nullptr;
    }

    size_t count(std::string_view key) const noexcept {
        return contains(key) ? 1 : 0;
    }

    // Object Indexing (mutable) -> automatically becomes object if null
    value& operator[](std::string_view key) {
        if (is_null()) {
            m_data = object_t{};
        }
        if (!is_object()) {
            throw type_error("Cannot use operator[](string) on non-object type " + std::string(type_name()));
        }
        auto& obj = std::get<object_t>(m_data);
        for (auto& pair : obj) {
            if (pair.first == key) return pair.second;
        }
        obj.emplace_back(std::string(key), value{});
        return obj.back().second;
    }

    // Strict element access (throws out_of_range)
    value& at(std::string_view key) {
        if (!is_object()) {
            throw type_error("Cannot use at(key) on non-object type " + std::string(type_name()));
        }
        auto& obj = std::get<object_t>(m_data);
        for (auto& pair : obj) {
            if (pair.first == key) return pair.second;
        }
        throw out_of_range("Key not found in object: '" + std::string(key) + "'");
    }

    const value& at(std::string_view key) const {
        if (!is_object()) {
            throw type_error("Cannot use at(key) on non-object type " + std::string(type_name()));
        }
        const auto& obj = std::get<object_t>(m_data);
        for (const auto& pair : obj) {
            if (pair.first == key) return pair.second;
        }
        throw out_of_range("Key not found in object: '" + std::string(key) + "'");
    }

    value& at(size_t index) {
        if (!is_array()) {
            throw type_error("Cannot use at(index) on non-array type " + std::string(type_name()));
        }
        auto& arr = std::get<array_t>(m_data);
        if (index >= arr.size()) {
            throw out_of_range("Array index " + std::to_string(index) + " out of range (size: " + std::to_string(arr.size()) + ")");
        }
        return arr[index];
    }

    const value& at(size_t index) const {
        if (!is_array()) {
            throw type_error("Cannot use at(index) on non-array type " + std::string(type_name()));
        }
        const auto& arr = std::get<array_t>(m_data);
        if (index >= arr.size()) {
            throw out_of_range("Array index " + std::to_string(index) + " out of range (size: " + std::to_string(arr.size()) + ")");
        }
        return arr[index];
    }

    // Object Indexing (const)
    const value& operator[](std::string_view key) const {
        return at(key);
    }

    // Array Indexing (mutable) -> automatically becomes array if null
    template <typename Int, typename std::enable_if_t<std::is_integral_v<Int>, int> = 0>
    value& operator[](Int index) {
        if constexpr (std::is_signed_v<Int>) {
            if (index < 0) {
                throw out_of_range("Array index cannot be negative: " + std::to_string(index));
            }
        }
        if (is_null()) {
            m_data = array_t{};
        }
        if (!is_array()) {
            throw type_error("Cannot use operator[](index) on non-array type " + std::string(type_name()));
        }
        auto& arr = std::get<array_t>(m_data);
        size_t uidx = static_cast<size_t>(index);
        if (uidx >= arr.size()) {
            arr.resize(uidx + 1);
        }
        return arr[uidx];
    }

    template <typename Int, typename std::enable_if_t<std::is_integral_v<Int>, int> = 0>
    const value& operator[](Int index) const {
        if constexpr (std::is_signed_v<Int>) {
            if (index < 0) {
                throw out_of_range("Array index cannot be negative: " + std::to_string(index));
            }
        }
        return at(static_cast<size_t>(index));
    }

    // Array modifiers
    void push_back(value val) {
        if (is_null()) {
            m_data = array_t{};
        }
        if (!is_array()) {
            throw type_error("Cannot push_back on non-array type " + std::string(type_name()));
        }
        std::get<array_t>(m_data).push_back(std::move(val));
    }

    void emplace_back(value val) {
        push_back(std::move(val));
    }

    // Object modifiers
    void emplace(std::string key, value val) {
        if (is_null()) {
            m_data = object_t{};
        }
        if (!is_object()) {
            throw type_error("Cannot emplace key-value on non-object type " + std::string(type_name()));
        }
        auto& obj = std::get<object_t>(m_data);
        for (auto& pair : obj) {
            if (pair.first == key) {
                pair.second = std::move(val);
                return;
            }
        }
        obj.emplace_back(std::move(key), std::move(val));
    }

    bool erase(std::string_view key) {
        if (!is_object()) return false;
        auto& obj = std::get<object_t>(m_data);
        auto it = std::find_if(obj.begin(), obj.end(), [key](const auto& pair) {
            return pair.first == key;
        });
        if (it != obj.end()) {
            obj.erase(it);
            return true;
        }
        return false;
    }

    bool erase(size_t index) {
        if (!is_array()) return false;
        auto& arr = std::get<array_t>(m_data);
        if (index < arr.size()) {
            arr.erase(arr.begin() + index);
            return true;
        }
        return false;
    }

    // ==========================================
    // Iterators (Array) & Items Proxy (Object)
    // ==========================================

    using iterator = array_t::iterator;
    using const_iterator = array_t::const_iterator;

    iterator begin() {
        if (!is_array()) throw type_error("Cannot call begin() on non-array type " + std::string(type_name()));
        return std::get<array_t>(m_data).begin();
    }
    iterator end() {
        if (!is_array()) throw type_error("Cannot call end() on non-array type " + std::string(type_name()));
        return std::get<array_t>(m_data).end();
    }
    const_iterator begin() const {
        if (!is_array()) throw type_error("Cannot call begin() on non-array type " + std::string(type_name()));
        return std::get<array_t>(m_data).begin();
    }
    const_iterator end() const {
        if (!is_array()) throw type_error("Cannot call end() on non-array type " + std::string(type_name()));
        return std::get<array_t>(m_data).end();
    }
    const_iterator cbegin() const {
        return begin();
    }
    const_iterator cend() const {
        return end();
    }

    struct items_view {
        object_t& obj;
        auto begin() noexcept { return obj.begin(); }
        auto end() noexcept { return obj.end(); }
    };

    struct const_items_view {
        const object_t& obj;
        auto begin() const noexcept { return obj.begin(); }
        auto end() const noexcept { return obj.end(); }
        auto cbegin() const noexcept { return obj.cbegin(); }
        auto cend() const noexcept { return obj.cend(); }
    };

    items_view items() {
        if (!is_object()) throw type_error("Cannot call items() on non-object type " + std::string(type_name()));
        return items_view{std::get<object_t>(m_data)};
    }

    const_items_view items() const {
        if (!is_object()) throw type_error("Cannot call items() on non-object type " + std::string(type_name()));
        return const_items_view{std::get<object_t>(m_data)};
    }

    // ==========================================
    // Comparisons
    // ==========================================

    bool operator==(const value& other) const {
        if (type() != other.type()) {
            // Compare integer vs unsigned vs float numerically if both are numbers
            if (is_number() && other.is_number()) {
                if (is_number_integer() && other.is_number_unsigned()) {
                    int64_t a = get<int64_t>();
                    return (a >= 0) && (static_cast<uint64_t>(a) == other.get<uint64_t>());
                }
                if (is_number_unsigned() && other.is_number_integer()) {
                    int64_t b = other.get<int64_t>();
                    return (b >= 0) && (get<uint64_t>() == static_cast<uint64_t>(b));
                }
                return get<double>() == other.get<double>();
            }
            return false;
        }
        if (is_object()) {
            const auto& obj1 = std::get<object_t>(m_data);
            const auto& obj2 = std::get<object_t>(other.m_data);
            if (obj1.size() != obj2.size()) return false;
            if (obj1 == obj2) return true;
            for (const auto& [k, v] : obj1) {
                const value* other_v = other.find(k);
                if (!other_v || *other_v != v) return false;
            }
            for (const auto& [k, v] : obj2) {
                const value* this_v = find(k);
                if (!this_v || *this_v != v) return false;
            }
            return true;
        }
        return m_data == other.m_data;
    }

    bool operator!=(const value& other) const {
        return !(*this == other);
    }

    // ==========================================
    // Serialization & Parsing declarations
    // ==========================================

    std::string dump(int indent = -1) const;
    void dump(std::ostream& os, int indent = -1) const;
    void dump_file(const std::string& filepath, int indent = -1) const;
    std::string dump_colored(int indent = 2) const;
    void dump_colored(std::ostream& os, int indent = 2) const;

    static value parse(std::string_view input, bool allow_comments = false, bool allow_trailing_comma = false);
    static value parse(std::istream& is, bool allow_comments = false, bool allow_trailing_comma = false);
    static value parse_file(const std::string& filepath, bool allow_comments = false, bool allow_trailing_comma = false);

    // Hash computation for std::unordered_map and std::unordered_set
    size_t hash() const noexcept {
        size_t h = std::hash<size_t>{}(static_cast<size_t>(type()));
        switch (type()) {
            case value_t::null: break;
            case value_t::boolean: h ^= std::hash<bool>{}(get<bool>()) + 0x9e3779b9 + (h << 6) + (h >> 2); break;
            case value_t::number_integer: h ^= std::hash<int64_t>{}(get<int64_t>()) + 0x9e3779b9 + (h << 6) + (h >> 2); break;
            case value_t::number_unsigned: h ^= std::hash<uint64_t>{}(get<uint64_t>()) + 0x9e3779b9 + (h << 6) + (h >> 2); break;
            case value_t::number_float: h ^= std::hash<double>{}(get<double>()) + 0x9e3779b9 + (h << 6) + (h >> 2); break;
            case value_t::string: h ^= std::hash<std::string>{}(get_ref_string()) + 0x9e3779b9 + (h << 6) + (h >> 2); break;
            case value_t::array: {
                for (const auto& item : get_ref_array()) {
                    h ^= item.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
                }
                break;
            }
            case value_t::object: {
                size_t obj_acc = 0;
                for (const auto& [k, v] : get_ref_object()) {
                    size_t item_h = std::hash<std::string>{}(k);
                    item_h ^= v.hash() + 0x9e3779b9 + (item_h << 6) + (item_h >> 2);
                    obj_acc += item_h;
                }
                h ^= obj_acc + 0x9e3779b9 + (h << 6) + (h >> 2);
                break;
            }
        }
        return h;
    }

    // JSON Pointer support declarations
    value& at_ptr(const json_pointer& ptr);
    const value& at_ptr(const json_pointer& ptr) const;
    value& operator[](const json_pointer& ptr);
    const value& operator[](const json_pointer& ptr) const;

    template <typename T>
    T value_or(const json_pointer& ptr, const T& default_val) const {
        try {
            return at_ptr(ptr).get<T>();
        } catch (...) {
            return default_val;
        }
    }

    value flatten() const;
    value unflatten() const;

    // JSONPath support declarations
    std::vector<value> jsonpath(std::string_view query) const;
    value jsonpath_first(std::string_view query) const;
    std::vector<const value*> jsonpath_refs(std::string_view query) const;
    std::vector<value*> jsonpath_refs(std::string_view query);
    const value* jsonpath_first_ref(std::string_view query) const;
    value* jsonpath_first_ref(std::string_view query);

    // JSON Patch (RFC 6902) & Diff declarations
    value patch(const value& patch_doc) const;
    void patch_in_place(const value& patch_doc);
    static value diff(const value& source, const value& target);

    // JSON Merge Patch (RFC 7396) declarations
    value merge_patch(const value& patch_doc) const;
    void merge_patch_in_place(const value& patch_doc);
    static value merge_patch(const value& target, const value& patch_doc);

    // JSON Schema Validation declaration
    bool validate(const value& schema_doc, std::string* error_out = nullptr) const;
};

} // namespace senko

namespace std {
template <>
struct hash<senko::value> {
    size_t operator()(const senko::value& v) const noexcept {
        return v.hash();
    }
};
}

namespace senko {

// Stream operator for output
std::ostream& operator<<(std::ostream& os, const value& j);

// Default ADL serializer fallback
template <typename T, typename SFINAE>
struct adl_serializer {
    static void serialize(value& j, const T& val) {
        to_json(j, val);
    }
    static void deserialize(const value& j, T& val) {
        from_json(j, val);
    }
};

} // namespace senko
