#pragma once

#include "fwd.hpp"
#include "value.hpp"

#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <limits>
#include <charconv>
#include <cstdio>
#include <cstring>

namespace senko {

namespace detail {

struct string_writer {
    std::string& out;

    explicit string_writer(std::string& s) : out(s) {}

    void push_back(char c) {
        out.push_back(c);
    }

    void append(const char* data, size_t len) {
        out.append(data, len);
    }

    void append(std::string_view sv) {
        out.append(sv.data(), sv.size());
    }

    void append_n(size_t n, char c) {
        out.append(n, c);
    }
};

struct stream_writer {
    std::ostream& os;
    char buf[4096];
    size_t pos = 0;

    explicit stream_writer(std::ostream& s) : os(s) {}

    ~stream_writer() {
        flush();
    }

    void flush() {
        if (pos > 0) {
            os.write(buf, pos);
            pos = 0;
        }
    }

    void push_back(char c) {
        if (pos >= sizeof(buf)) flush();
        buf[pos++] = c;
    }

    void append(const char* data, size_t len) {
        if (len >= sizeof(buf)) {
            flush();
            os.write(data, len);
        } else {
            if (pos + len > sizeof(buf)) flush();
            std::memcpy(buf + pos, data, len);
            pos += len;
        }
    }

    void append(std::string_view sv) {
        append(sv.data(), sv.size());
    }

    void append_n(size_t n, char c) {
        while (n > 0) {
            if (pos >= sizeof(buf)) flush();
            size_t chunk = (std::min)(n, sizeof(buf) - pos);
            std::memset(buf + pos, c, chunk);
            pos += chunk;
            n -= chunk;
        }
    }
};

template <typename Writer>
inline void dump_string_escaped(Writer& out, std::string_view sv) {
    out.push_back('"');
    size_t chunk_start = 0;
    for (size_t i = 0; i < sv.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(sv[i]);
        const char* esc = nullptr;
        size_t esc_len = 0;
        char hex_buf[8];

        switch (c) {
            case '"':  esc = "\\\""; esc_len = 2; break;
            case '\\': esc = "\\\\"; esc_len = 2; break;
            case '\b': esc = "\\b"; esc_len = 2; break;
            case '\f': esc = "\\f"; esc_len = 2; break;
            case '\n': esc = "\\n"; esc_len = 2; break;
            case '\r': esc = "\\r"; esc_len = 2; break;
            case '\t': esc = "\\t"; esc_len = 2; break;
            default:
                if (c < 0x20) {
                    std::snprintf(hex_buf, sizeof(hex_buf), "\\u%04x", c);
                    esc = hex_buf;
                    esc_len = 6;
                }
                break;
        }

        if (esc) {
            if (i > chunk_start) {
                out.append(sv.data() + chunk_start, i - chunk_start);
            }
            out.append(esc, esc_len);
            chunk_start = i + 1;
        }
    }
    if (sv.size() > chunk_start) {
        out.append(sv.data() + chunk_start, sv.size() - chunk_start);
    }
    out.push_back('"');
}

template <typename Writer>
class basic_serializer {
public:
    static constexpr size_t max_depth = 128;

    explicit basic_serializer(Writer& out, int indent = -1)
        : m_out(out), m_indent(indent), m_depth(0) {}

    void dump(const value& v) {
        if (static_cast<size_t>(m_depth) > max_depth) {
            throw serializer_error("Maximum JSON serialization depth exceeded (potential stack overflow)");
        }
        switch (v.type()) {
            case value_t::null:
                m_out.append("null", 4);
                break;
            case value_t::boolean:
                if (v.get<bool>()) {
                    m_out.append("true", 4);
                } else {
                    m_out.append("false", 5);
                }
                break;
            case value_t::number_integer: {
                char buf[32];
                auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v.get<int64_t>());
                m_out.append(buf, ptr - buf);
                break;
            }
            case value_t::number_unsigned: {
                char buf[32];
                auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v.get<uint64_t>());
                m_out.append(buf, ptr - buf);
                break;
            }
            case value_t::number_float: {
                double d = v.get<double>();
                if (std::isnan(d) || std::isinf(d)) {
                    m_out.append("null", 4); // RFC 8259 requires NaN/Infinity to be serialized as null
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
                break;
            }
            case value_t::string:
                dump_string(v.get_ref_string());
                break;
            case value_t::array:
                dump_array(v.get_ref_array());
                break;
            case value_t::object:
                dump_object(v.get_ref_object());
                break;
        }
    }

private:
    Writer& m_out;
    int m_indent;
    int m_depth;

    void indent_newline() {
        if (m_indent >= 0) {
            m_out.push_back('\n');
            m_out.append_n(static_cast<size_t>(m_depth * m_indent), ' ');
        }
    }

    void dump_string(std::string_view sv) {
        dump_string_escaped(m_out, sv);
    }

    void dump_array(const value::array_t& arr) {
        if (arr.empty()) {
            m_out.append("[]", 2);
            return;
        }

        m_out.push_back('[');
        m_depth++;

        for (size_t i = 0; i < arr.size(); ++i) {
            indent_newline();
            dump(arr[i]);
            if (i + 1 < arr.size()) {
                m_out.push_back(',');
            }
        }

        m_depth--;
        indent_newline();
        m_out.push_back(']');
    }

    void dump_object(const value::object_t& obj) {
        if (obj.empty()) {
            m_out.append("{}", 2);
            return;
        }

        m_out.push_back('{');
        m_depth++;

        for (size_t i = 0; i < obj.size(); ++i) {
            indent_newline();
            dump_string(obj[i].first);
            if (m_indent >= 0) {
                m_out.append(": ", 2);
            } else {
                m_out.push_back(':');
            }
            dump(obj[i].second);
            if (i + 1 < obj.size()) {
                m_out.push_back(',');
            }
        }

        m_depth--;
        indent_newline();
        m_out.push_back('}');
    }
};

namespace colors {
    inline constexpr std::string_view reset       = "\033[0m";
    inline constexpr std::string_view key         = "\033[36m";       // Cyan
    inline constexpr std::string_view string_val  = "\033[32m";       // Green
    inline constexpr std::string_view number_val  = "\033[33m";       // Yellow
    inline constexpr std::string_view bool_val    = "\033[35m";       // Magenta
    inline constexpr std::string_view null_val    = "\033[90m";       // Gray
    inline constexpr std::string_view punctuation = "\033[37m";       // White / Punctuation
}

template <typename Writer>
class colored_serializer {
public:
    static constexpr size_t max_depth = 128;

    explicit colored_serializer(Writer& out, int indent = 2)
        : m_out(out), m_indent(indent), m_depth(0) {}

    void dump(const value& v) {
        if (static_cast<size_t>(m_depth) > max_depth) {
            throw serializer_error("Maximum JSON serialization depth exceeded (potential stack overflow)");
        }
        switch (v.type()) {
            case value_t::null:
                m_out.append(colors::null_val);
                m_out.append("null", 4);
                m_out.append(colors::reset);
                break;
            case value_t::boolean:
                m_out.append(colors::bool_val);
                if (v.get<bool>()) {
                    m_out.append("true", 4);
                } else {
                    m_out.append("false", 5);
                }
                m_out.append(colors::reset);
                break;
            case value_t::number_integer: {
                char buf[32];
                auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v.get<int64_t>());
                m_out.append(colors::number_val);
                m_out.append(buf, ptr - buf);
                m_out.append(colors::reset);
                break;
            }
            case value_t::number_unsigned: {
                char buf[32];
                auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v.get<uint64_t>());
                m_out.append(colors::number_val);
                m_out.append(buf, ptr - buf);
                m_out.append(colors::reset);
                break;
            }
            case value_t::number_float: {
                double d = v.get<double>();
                if (std::isnan(d) || std::isinf(d)) {
                    m_out.append(colors::null_val);
                    m_out.append("null", 4);
                    m_out.append(colors::reset);
                } else {
                    char buf[64];
                    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), d);
                    m_out.append(colors::number_val);
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
                    m_out.append(colors::reset);
                }
                break;
            }
            case value_t::string:
                m_out.append(colors::string_val);
                dump_string_escaped(m_out, v.get_ref_string());
                m_out.append(colors::reset);
                break;
            case value_t::array:
                dump_array(v.get_ref_array());
                break;
            case value_t::object:
                dump_object(v.get_ref_object());
                break;
        }
    }

private:
    Writer& m_out;
    int m_indent;
    int m_depth;

    void indent_newline() {
        if (m_indent >= 0) {
            m_out.push_back('\n');
            m_out.append_n(static_cast<size_t>(m_depth * m_indent), ' ');
        }
    }

    void dump_array(const value::array_t& arr) {
        if (arr.empty()) {
            m_out.append(colors::punctuation);
            m_out.append("[]", 2);
            m_out.append(colors::reset);
            return;
        }

        m_out.append(colors::punctuation);
        m_out.push_back('[');
        m_out.append(colors::reset);
        m_depth++;

        for (size_t i = 0; i < arr.size(); ++i) {
            indent_newline();
            dump(arr[i]);
            if (i + 1 < arr.size()) {
                m_out.append(colors::punctuation);
                m_out.push_back(',');
                m_out.append(colors::reset);
            }
        }

        m_depth--;
        indent_newline();
        m_out.append(colors::punctuation);
        m_out.push_back(']');
        m_out.append(colors::reset);
    }

    void dump_object(const value::object_t& obj) {
        if (obj.empty()) {
            m_out.append(colors::punctuation);
            m_out.append("{}", 2);
            m_out.append(colors::reset);
            return;
        }

        m_out.append(colors::punctuation);
        m_out.push_back('{');
        m_out.append(colors::reset);
        m_depth++;

        for (size_t i = 0; i < obj.size(); ++i) {
            indent_newline();
            m_out.append(colors::key);
            dump_string_escaped(m_out, obj[i].first);
            m_out.append(colors::reset);

            m_out.append(colors::punctuation);
            if (m_indent >= 0) {
                m_out.append(": ", 2);
            } else {
                m_out.push_back(':');
            }
            m_out.append(colors::reset);

            dump(obj[i].second);
            if (i + 1 < obj.size()) {
                m_out.append(colors::punctuation);
                m_out.push_back(',');
                m_out.append(colors::reset);
            }
        }

        m_depth--;
        indent_newline();
        m_out.append(colors::punctuation);
        m_out.push_back('}');
        m_out.append(colors::reset);
    }
};

using fast_string_serializer = basic_serializer<string_writer>;

} // namespace detail

class serializer {
public:
    explicit serializer(std::ostream& os, int indent = -1)
        : m_os(os), m_indent(indent) {}

    void dump(const value& v) {
        dump_to_stream(v, m_os, m_indent);
    }

    static std::string dump_to_string(const value& v, int indent = -1) {
        std::string out;
        out.reserve(256);
        detail::string_writer writer(out);
        detail::basic_serializer<detail::string_writer> s(writer, indent);
        s.dump(v);
        return out;
    }

    static void dump_to_stream(const value& v, std::ostream& os, int indent = -1) {
        detail::stream_writer writer(os);
        detail::basic_serializer<detail::stream_writer> s(writer, indent);
        s.dump(v);
        writer.flush();
    }

    static std::string dump_colored_to_string(const value& v, int indent = 2) {
        std::string out;
        out.reserve(512);
        detail::string_writer writer(out);
        detail::colored_serializer<detail::string_writer> s(writer, indent);
        s.dump(v);
        return out;
    }

    static void dump_colored_to_stream(const value& v, std::ostream& os, int indent = 2) {
        detail::stream_writer writer(os);
        detail::colored_serializer<detail::stream_writer> s(writer, indent);
        s.dump(v);
        writer.flush();
    }

    static void dump_to_file(const value& v, const std::string& filepath, int indent = -1) {
        std::ofstream file(filepath, std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            throw serializer_error("Failed to open file for writing: " + filepath);
        }
        dump_to_stream(v, file, indent);
    }

private:
    std::ostream& m_os;
    int m_indent;
};

// Inline implementations of value::dump and operator<<
inline std::string value::dump(int indent) const {
    return serializer::dump_to_string(*this, indent);
}

inline void value::dump(std::ostream& os, int indent) const {
    serializer::dump_to_stream(*this, os, indent);
}

inline void value::dump_file(const std::string& filepath, int indent) const {
    serializer::dump_to_file(*this, filepath, indent);
}

inline std::string value::dump_colored(int indent) const {
    return serializer::dump_colored_to_string(*this, indent);
}

inline void value::dump_colored(std::ostream& os, int indent) const {
    serializer::dump_colored_to_stream(*this, os, indent);
}

inline std::ostream& operator<<(std::ostream& os, const value& j) {
    serializer::dump_to_stream(j, os, -1);
    return os;
}

} // namespace senko
