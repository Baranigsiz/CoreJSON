#pragma once

#include "fwd.hpp"
#include "error.hpp"
#include "lexer.hpp"
#include "value.hpp"

#include <string_view>
#include <string>
#include <cstdint>
#include <vector>

namespace senko {

/**
 * @brief Default empty SAX handler base.
 * Users can inherit from this or provide their own class with matching methods.
 */
struct default_sax_handler {
    bool null() { return true; }
    bool boolean(bool) { return true; }
    bool number_integer(int64_t) { return true; }
    bool number_unsigned(uint64_t) { return true; }
    bool number_float(double) { return true; }
    bool string(std::string_view) { return true; }
    bool start_object(size_t = static_cast<size_t>(-1)) { return true; }
    bool key(std::string_view) { return true; }
    bool end_object() { return true; }
    bool start_array(size_t = static_cast<size_t>(-1)) { return true; }
    bool end_array() { return true; }
    bool parse_error(size_t, std::string_view, const parse_error&) { return false; }
};

/**
 * @brief Event-driven SAX Parser for SenkoJSON.
 * Parses JSON inputs directly into handler callbacks with zero intermediate DOM allocations.
 */
template <typename Handler>
class sax_parser {
public:
    static constexpr size_t max_depth = 128;

    explicit sax_parser(Handler& handler, bool allow_comments = false, bool allow_trailing_comma = false)
        : m_handler(handler), m_allow_comments(allow_comments), m_allow_trailing_comma(allow_trailing_comma), m_depth(0) {}

    bool parse(std::string_view source) {
        lexer lex(source, m_allow_comments);
        try {
            m_depth = 0;
            if (!parse_value(lex)) return false;
            lex.skip_whitespace_and_comments();
            if (lex.has_more()) {
                lex.throw_parse_error("Extra characters after JSON payload");
            }
            return true;
        } catch (const parse_error& e) {
            m_handler.parse_error(lex.offset(), "", e);
            return false;
        }
    }

private:
    Handler& m_handler;
    bool m_allow_comments;
    bool m_allow_trailing_comma;
    size_t m_depth = 0;

    struct depth_guard {
        size_t& d;
        explicit depth_guard(size_t& depth) : d(depth) { d++; }
        ~depth_guard() { d--; }
    };

    bool parse_value(lexer& lex) {
        if (m_depth > max_depth) {
            lex.throw_parse_error("Maximum JSON nesting depth exceeded in SAX parser");
        }
        lex.skip_whitespace_and_comments();
        if (!lex.has_more()) {
            lex.throw_parse_error("Unexpected end of input while expecting value");
        }

        char c = lex.peek();
        switch (c) {
            case '{': return parse_object(lex);
            case '[': return parse_array(lex);
            case '"': {
                std::string s = lex.parse_string();
                return m_handler.string(s);
            }
            case 't': {
                lex.expect_keyword("true");
                return m_handler.boolean(true);
            }
            case 'f': {
                lex.expect_keyword("false");
                return m_handler.boolean(false);
            }
            case 'n': {
                lex.expect_keyword("null");
                return m_handler.null();
            }
            case '-':
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9': {
                value num = lex.parse_number();
                if (num.is_number_integer()) return m_handler.number_integer(num.get<int64_t>());
                if (num.is_number_unsigned()) return m_handler.number_unsigned(num.get<uint64_t>());
                return m_handler.number_float(num.get<double>());
            }
            default:
                lex.throw_parse_error(std::string("Unexpected token '") + c + "'");
        }
        return false;
    }

    bool parse_object(lexer& lex) {
        depth_guard guard(m_depth);
        lex.get(); // consume '{'
        if (!m_handler.start_object()) return false;

        lex.skip_whitespace_and_comments();
        if (lex.peek() == '}') {
            lex.get(); // consume '}'
            return m_handler.end_object();
        }

        while (true) {
            lex.skip_whitespace_and_comments();
            if (lex.peek() != '"') {
                lex.throw_parse_error("Expected string key in object");
            }
            std::string k = lex.parse_string();
            if (!m_handler.key(k)) return false;

            lex.skip_whitespace_and_comments();
            if (lex.get() != ':') {
                lex.throw_parse_error("Expected ':' after key in object");
            }

            if (!parse_value(lex)) return false;

            lex.skip_whitespace_and_comments();
            char next = lex.peek();
            if (next == '}') {
                lex.get(); // consume '}'
                return m_handler.end_object();
            }
            if (next == ',') {
                lex.get(); // consume ','
                if (m_allow_trailing_comma) {
                    lex.skip_whitespace_and_comments();
                    if (lex.peek() == '}') {
                        lex.get(); // consume '}'
                        return m_handler.end_object();
                    }
                }
            } else {
                lex.throw_parse_error("Expected ',' or '}' inside object");
            }
        }
    }

    bool parse_array(lexer& lex) {
        depth_guard guard(m_depth);
        lex.get(); // consume '['
        if (!m_handler.start_array()) return false;

        lex.skip_whitespace_and_comments();
        if (lex.peek() == ']') {
            lex.get(); // consume ']'
            return m_handler.end_array();
        }

        while (true) {
            if (!parse_value(lex)) return false;

            lex.skip_whitespace_and_comments();
            char next = lex.peek();
            if (next == ']') {
                lex.get(); // consume ']'
                return m_handler.end_array();
            }
            if (next == ',') {
                lex.get(); // consume ','
                if (m_allow_trailing_comma) {
                    lex.skip_whitespace_and_comments();
                    if (lex.peek() == ']') {
                        lex.get(); // consume ']'
                        return m_handler.end_array();
                    }
                }
            } else {
                lex.throw_parse_error("Expected ',' or ']' inside array");
            }
        }
    }
};

/**
 * @brief Helper function to parse JSON with a custom SAX handler.
 */
template <typename Handler>
inline bool sax_parse(std::string_view source, Handler& handler, bool allow_comments = false, bool allow_trailing_comma = false) {
    sax_parser<Handler> parser(handler, allow_comments, allow_trailing_comma);
    return parser.parse(source);
}

/**
 * @brief Helper function to parse JSON stream with a custom SAX handler.
 */
template <typename Handler>
inline bool sax_parse(std::istream& is, Handler& handler, bool allow_comments = false, bool allow_trailing_comma = false) {
    std::string str((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    return sax_parse(str, handler, allow_comments, allow_trailing_comma);
}

} // namespace senko
