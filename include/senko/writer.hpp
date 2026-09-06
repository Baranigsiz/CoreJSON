#pragma once

#include "fwd.hpp"
#include "error.hpp"
#include "serializer.hpp"
#include "value.hpp"

#include <vector>
#include <string>
#include <string_view>
#include <ostream>
#include <charconv>
#include <cmath>
#include <memory>
#include <type_traits>

namespace senko {

class writer_error : public exception {
public:
    explicit writer_error(std::string message)
        : exception("[senko::writer_error] " + std::move(message)) {}
};

namespace detail {

enum class writer_scope_type : uint8_t {
    array,
    object
};

struct writer_scope {
    writer_scope_type type;
    size_t count = 0;
    bool expecting_value = false;
};

} // namespace detail

/**
 * @brief Template-based streaming JSON writer without DOM tree allocation.
 */
template <typename TargetWriter>
class basic_json_writer {
public:
    explicit basic_json_writer(TargetWriter& out, int indent = -1)
        : m_out(out), m_indent(indent), m_depth(0) {}

    ~basic_json_writer() = default;

    int indent() const noexcept { return m_indent; }
    void set_indent(int indent) noexcept { m_indent = indent; }

    basic_json_writer& start_object() {
        prepare_value();
        m_out.push_back('{');
        m_scopes.push_back({detail::writer_scope_type::object, 0, false});
        m_depth++;
        return *this;
    }

    basic_json_writer& end_object() {
        if (m_scopes.empty()) {
            throw writer_error("Cannot call end_object(): no open scopes");
        }
        if (m_scopes.back().type != detail::writer_scope_type::object) {
            throw writer_error("Mismatched scope: expected end_array() but got end_object()");
        }
        if (m_scopes.back().expecting_value) {
            throw writer_error("Cannot call end_object(): object key is missing a corresponding value");
        }

        bool has_elements = (m_scopes.back().count > 0);
        m_scopes.pop_back();
        m_depth--;

        if (has_elements) {
            indent_newline();
        }
        m_out.push_back('}');
        post_value();
        return *this;
    }

    basic_json_writer& start_array() {
        prepare_value();
        m_out.push_back('[');
        m_scopes.push_back({detail::writer_scope_type::array, 0, false});
        m_depth++;
        return *this;
    }

    basic_json_writer& end_array() {
        if (m_scopes.empty()) {
            throw writer_error("Cannot call end_array(): no open scopes");
        }
        if (m_scopes.back().type != detail::writer_scope_type::array) {
            throw writer_error("Mismatched scope: expected end_object() but got end_array()");
        }

        bool has_elements = (m_scopes.back().count > 0);
        m_scopes.pop_back();
        m_depth--;

        if (has_elements) {
            indent_newline();
        }
        m_out.push_back(']');
        post_value();
        return *this;
    }

    basic_json_writer& key(std::string_view k) {
        if (m_scopes.empty() || m_scopes.back().type != detail::writer_scope_type::object) {
            throw writer_error("key() can only be called inside an open object");
        }
        if (m_scopes.back().expecting_value) {
            throw writer_error("Duplicate key() call without a value() for the previous key");
        }

        if (m_scopes.back().count > 0) {
            m_out.push_back(',');
        }
        indent_newline();
        detail::dump_string_escaped(m_out, k);
        if (m_indent >= 0) {
            m_out.append(": ", 2);
        } else {
            m_out.push_back(':');
        }
        m_scopes.back().expecting_value = true;
        return *this;
    }

    // Primitive values
    basic_json_writer& value(std::nullptr_t) {
        prepare_value();
        m_out.append("null", 4);
        post_value();
        return *this;
    }

    basic_json_writer& null_value() {
        return value(nullptr);
    }

    basic_json_writer& value(bool b) {
        prepare_value();
        if (b) {
            m_out.append("true", 4);
        } else {
            m_out.append("false", 5);
        }
        post_value();
        return *this;
    }

    template <typename Int, typename std::enable_if_t<std::is_integral_v<Int> && std::is_signed_v<Int>, int> = 0>
    basic_json_writer& value(Int n) {
        prepare_value();
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<int64_t>(n));
        m_out.append(buf, ptr - buf);
        post_value();
        return *this;
    }

    template <typename UInt, typename std::enable_if_t<std::is_integral_v<UInt> && std::is_unsigned_v<UInt> && !std::is_same_v<UInt, bool>, int> = 0>
    basic_json_writer& value(UInt n) {
        prepare_value();
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), static_cast<uint64_t>(n));
        m_out.append(buf, ptr - buf);
        post_value();
        return *this;
    }

    basic_json_writer& value(float f) {
        return value(static_cast<double>(f));
    }

    basic_json_writer& value(double d) {
        prepare_value();
        if (std::isnan(d) || std::isinf(d)) {
            m_out.append("null", 4);
        } else {
            char buf[64];
            auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), d);
            if (ec == std::errc()) {
                std::string_view sv(buf, ptr - buf);
                m_out.append(sv);
                if (sv.find('.') == std::string_view::npos && sv.find('e') == std::string_view::npos && sv.find('E') == std::string_view::npos) {
                    m_out.append(".0", 2);
                }
            } else {
                int len = std::snprintf(buf, sizeof(buf), "%.17g", d);
                if (len > 0) {
                    std::string_view sv(buf, len);
                    m_out.append(sv);
                    if (sv.find('.') == std::string_view::npos && sv.find('e') == std::string_view::npos && sv.find('E') == std::string_view::npos) {
                        m_out.append(".0", 2);
                    }
                }
            }
        }
        post_value();
        return *this;
    }

    basic_json_writer& value(std::string_view s) {
        prepare_value();
        detail::dump_string_escaped(m_out, s);
        post_value();
        return *this;
    }

    basic_json_writer& value(const char* s) {
        return value(std::string_view(s ? s : ""));
    }

    basic_json_writer& value(const std::string& s) {
        return value(std::string_view(s));
    }

    // Embed an existing senko::value directly into the stream
    basic_json_writer& value(const senko::value& v) {
        prepare_value();
        detail::basic_serializer<TargetWriter> s(m_out, m_indent);
        s.dump(v);
        post_value();
        return *this;
    }

    // Raw JSON injection (bypasses escaping and validation)
    basic_json_writer& raw_json(std::string_view raw) {
        prepare_value();
        m_out.append(raw);
        post_value();
        return *this;
    }

    bool is_complete() const noexcept {
        return m_scopes.empty() && m_has_root;
    }

    void reset() {
        m_depth = 0;
        m_has_root = false;
        m_scopes.clear();
    }

private:
    TargetWriter& m_out;
    int m_indent;
    int m_depth;
    bool m_has_root = false;
    std::vector<detail::writer_scope> m_scopes;

    void indent_newline() {
        if (m_indent >= 0) {
            m_out.push_back('\n');
            m_out.append_n(static_cast<size_t>(m_depth * m_indent), ' ');
        }
    }

    void prepare_value() {
        if (m_scopes.empty()) {
            if (m_has_root) {
                throw writer_error("Cannot write multiple root values to a single JSON document");
            }
            m_has_root = true;
            return;
        }

        auto& current = m_scopes.back();
        if (current.type == detail::writer_scope_type::object) {
            if (!current.expecting_value) {
                throw writer_error("Cannot write value in object without a preceding key()");
            }
        } else if (current.type == detail::writer_scope_type::array) {
            if (current.count > 0) {
                m_out.push_back(',');
            }
            indent_newline();
        }
    }

    void post_value() {
        if (!m_scopes.empty()) {
            auto& current = m_scopes.back();
            current.count++;
            current.expecting_value = false;
        }
    }
};

/**
 * @brief High-level streaming JSON generator writing to strings or streams with zero DOM allocations.
 */
class json_writer {
public:
    explicit json_writer(int indent = -1)
        : m_owned_str(std::make_unique<std::string>()),
          m_str_writer(std::make_unique<detail::string_writer>(*m_owned_str)),
          m_str_impl(std::make_unique<basic_json_writer<detail::string_writer>>(*m_str_writer, indent)) {}

    explicit json_writer(std::string& out, int indent = -1)
        : m_str_writer(std::make_unique<detail::string_writer>(out)),
          m_str_impl(std::make_unique<basic_json_writer<detail::string_writer>>(*m_str_writer, indent)) {}

    explicit json_writer(std::ostream& os, int indent = -1)
        : m_stream_writer(std::make_unique<detail::stream_writer>(os)),
          m_stream_impl(std::make_unique<basic_json_writer<detail::stream_writer>>(*m_stream_writer, indent)) {}

    ~json_writer() {
        flush();
    }

    json_writer(const json_writer&) = delete;
    json_writer& operator=(const json_writer&) = delete;

    json_writer(json_writer&&) noexcept = default;
    json_writer& operator=(json_writer&&) noexcept = default;

    void flush() {
        if (m_stream_writer) {
            m_stream_writer->flush();
        }
    }

    const std::string& str() const {
        if (m_owned_str) return *m_owned_str;
        if (m_str_writer) return m_str_writer->out;
        static const std::string empty;
        return empty;
    }

    bool is_complete() const noexcept {
        if (m_str_impl) return m_str_impl->is_complete();
        if (m_stream_impl) return m_stream_impl->is_complete();
        return false;
    }

    json_writer& start_object() {
        if (m_str_impl) m_str_impl->start_object();
        else m_stream_impl->start_object();
        return *this;
    }

    json_writer& end_object() {
        if (m_str_impl) m_str_impl->end_object();
        else m_stream_impl->end_object();
        return *this;
    }

    json_writer& start_array() {
        if (m_str_impl) m_str_impl->start_array();
        else m_stream_impl->start_array();
        return *this;
    }

    json_writer& end_array() {
        if (m_str_impl) m_str_impl->end_array();
        else m_stream_impl->end_array();
        return *this;
    }

    json_writer& key(std::string_view k) {
        if (m_str_impl) m_str_impl->key(k);
        else m_stream_impl->key(k);
        return *this;
    }

    json_writer& value(std::nullptr_t) {
        if (m_str_impl) m_str_impl->value(nullptr);
        else m_stream_impl->value(nullptr);
        return *this;
    }

    json_writer& null_value() {
        return value(nullptr);
    }

    json_writer& value(bool b) {
        if (m_str_impl) m_str_impl->value(b);
        else m_stream_impl->value(b);
        return *this;
    }

    template <typename Int, typename std::enable_if_t<std::is_integral_v<Int> && std::is_signed_v<Int>, int> = 0>
    json_writer& value(Int n) {
        if (m_str_impl) m_str_impl->value(n);
        else m_stream_impl->value(n);
        return *this;
    }

    template <typename UInt, typename std::enable_if_t<std::is_integral_v<UInt> && std::is_unsigned_v<UInt> && !std::is_same_v<UInt, bool>, int> = 0>
    json_writer& value(UInt n) {
        if (m_str_impl) m_str_impl->value(n);
        else m_stream_impl->value(n);
        return *this;
    }

    json_writer& value(float f) {
        if (m_str_impl) m_str_impl->value(f);
        else m_stream_impl->value(f);
        return *this;
    }

    json_writer& value(double d) {
        if (m_str_impl) m_str_impl->value(d);
        else m_stream_impl->value(d);
        return *this;
    }

    json_writer& value(std::string_view s) {
        if (m_str_impl) m_str_impl->value(s);
        else m_stream_impl->value(s);
        return *this;
    }

    json_writer& value(const char* s) {
        return value(std::string_view(s ? s : ""));
    }

    json_writer& value(const std::string& s) {
        return value(std::string_view(s));
    }

    json_writer& value(const senko::value& v) {
        if (m_str_impl) m_str_impl->value(v);
        else m_stream_impl->value(v);
        return *this;
    }

    json_writer& raw_json(std::string_view raw) {
        if (m_str_impl) m_str_impl->raw_json(raw);
        else m_stream_impl->raw_json(raw);
        return *this;
    }

private:
    std::unique_ptr<std::string> m_owned_str;
    std::unique_ptr<detail::string_writer> m_str_writer;
    std::unique_ptr<basic_json_writer<detail::string_writer>> m_str_impl;

    std::unique_ptr<detail::stream_writer> m_stream_writer;
    std::unique_ptr<basic_json_writer<detail::stream_writer>> m_stream_impl;
};

} // namespace senko
