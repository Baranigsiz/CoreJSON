#pragma once

/**
 * @file senko.hpp
 * @brief Master header for SenkoJSON - A lightning-fast, modern, header-only C++ JSON library.
 * @version 2.6.0
 * @license MIT
 */

#define SENKO_VERSION_MAJOR 2
#define SENKO_VERSION_MINOR 6
#define SENKO_VERSION_PATCH 0

#include "fwd.hpp"
#include "error.hpp"
#include "value.hpp"
#include "stl_adapters.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "serializer.hpp"
#include "json_pointer.hpp"
#include "jsonpath.hpp"
#include "patch.hpp"
#include "binary/msgpack.hpp"
#include "binary/cbor.hpp"
#include "macro.hpp"
#include "schema.hpp"
#include "sax.hpp"
#include "jsonc.hpp"
#include "jsonl.hpp"
#include "writer.hpp"

namespace senko {

namespace literals {

/**
 * @brief User-defined literal for parsing JSON strings directly.
 * Example: `auto j = "{\"key\": 42}"_json;`
 */
inline value operator""_json(const char* str, size_t len) {
    return value::parse(std::string_view(str, len));
}

inline value operator""_json(unsigned long long val) {
    return value(static_cast<uint64_t>(val));
}

inline value operator""_json(long double val) {
    return value(static_cast<double>(val));
}

/**
 * @brief User-defined literal for creating JSON Pointer objects.
 * Example: `auto ptr = "/users/0/name"_json_pointer;`
 */
inline json_pointer operator""_json_pointer(const char* str, size_t len) {
    return json_pointer(std::string_view(str, len));
}

} // namespace literals

} // namespace senko
