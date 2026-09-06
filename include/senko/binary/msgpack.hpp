#pragma once

#include "../fwd.hpp"
#include "../error.hpp"
#include "../value.hpp"

#include <vector>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace senko {

class msgpack_error : public exception {
public:
    explicit msgpack_error(std::string msg) : exception("[senko::msgpack_error] " + std::move(msg)) {}
};

namespace detail {

// Big-Endian helpers
inline void write_u8(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(v);
}

inline void write_u16_be(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

inline void write_u32_be(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

inline void write_u64_be(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
    }
}

inline uint16_t read_u16_be(const uint8_t* p) {
    return static_cast<uint16_t>((uint16_t(p[0]) << 8) | uint16_t(p[1]));
}

inline uint32_t read_u32_be(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

inline uint64_t read_u64_be(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | uint64_t(p[i]);
    }
    return v;
}

inline void serialize_msgpack_impl(const value& v, std::vector<uint8_t>& out) {
    switch (v.type()) {
        case value_t::null:
            write_u8(out, 0xC0); // nil
            break;
        case value_t::boolean:
            write_u8(out, v.get<bool>() ? 0xC3 : 0xC2); // true : false
            break;
        case value_t::number_integer: {
            int64_t val = v.get<int64_t>();
            if (val >= -32 && val <= 127) {
                write_u8(out, static_cast<uint8_t>(val));
            } else if (val >= std::numeric_limits<int8_t>::min() && val <= std::numeric_limits<int8_t>::max()) {
                write_u8(out, 0xD0); // int 8
                write_u8(out, static_cast<uint8_t>(val));
            } else if (val >= std::numeric_limits<int16_t>::min() && val <= std::numeric_limits<int16_t>::max()) {
                write_u8(out, 0xD1); // int 16
                write_u16_be(out, static_cast<uint16_t>(val));
            } else if (val >= std::numeric_limits<int32_t>::min() && val <= std::numeric_limits<int32_t>::max()) {
                write_u8(out, 0xD2); // int 32
                write_u32_be(out, static_cast<uint32_t>(val));
            } else {
                write_u8(out, 0xD3); // int 64
                write_u64_be(out, static_cast<uint64_t>(val));
            }
            break;
        }
        case value_t::number_unsigned: {
            uint64_t val = v.get<uint64_t>();
            if (val <= 127) {
                write_u8(out, static_cast<uint8_t>(val));
            } else if (val <= std::numeric_limits<uint8_t>::max()) {
                write_u8(out, 0xCC); // uint 8
                write_u8(out, static_cast<uint8_t>(val));
            } else if (val <= std::numeric_limits<uint16_t>::max()) {
                write_u8(out, 0xCD); // uint 16
                write_u16_be(out, static_cast<uint16_t>(val));
            } else if (val <= std::numeric_limits<uint32_t>::max()) {
                write_u8(out, 0xCE); // uint 32
                write_u32_be(out, static_cast<uint32_t>(val));
            } else {
                write_u8(out, 0xCF); // uint 64
                write_u64_be(out, val);
            }
            break;
        }
        case value_t::number_float: {
            double d = v.get<double>();
            write_u8(out, 0xCB); // float 64
            uint64_t raw = 0;
            std::memcpy(&raw, &d, sizeof(double));
            write_u64_be(out, raw);
            break;
        }
        case value_t::string: {
            const std::string& str = v.get_ref_string();
            size_t len = str.size();
            if (len <= 31) {
                write_u8(out, static_cast<uint8_t>(0xA0 | len));
            } else if (len <= 0xFF) {
                write_u8(out, 0xD9); // str 8
                write_u8(out, static_cast<uint8_t>(len));
            } else if (len <= 0xFFFF) {
                write_u8(out, 0xDA); // str 16
                write_u16_be(out, static_cast<uint16_t>(len));
            } else {
                write_u8(out, 0xDB); // str 32
                write_u32_be(out, static_cast<uint32_t>(len));
            }
            out.insert(out.end(), str.begin(), str.end());
            break;
        }
        case value_t::array: {
            const auto& arr = v.get_ref_array();
            size_t len = arr.size();
            if (len <= 15) {
                write_u8(out, static_cast<uint8_t>(0x90 | len));
            } else if (len <= 0xFFFF) {
                write_u8(out, 0xDC); // array 16
                write_u16_be(out, static_cast<uint16_t>(len));
            } else {
                write_u8(out, 0xDD); // array 32
                write_u32_be(out, static_cast<uint32_t>(len));
            }
            for (const auto& elem : arr) {
                serialize_msgpack_impl(elem, out);
            }
            break;
        }
        case value_t::object: {
            const auto& obj = v.get_ref_object();
            size_t len = obj.size();
            if (len <= 15) {
                write_u8(out, static_cast<uint8_t>(0x80 | len));
            } else if (len <= 0xFFFF) {
                write_u8(out, 0xDE); // map 16
                write_u16_be(out, static_cast<uint16_t>(len));
            } else {
                write_u8(out, 0xDF); // map 32
                write_u32_be(out, static_cast<uint32_t>(len));
            }
            for (const auto& pair : obj) {
                // Key (string)
                size_t klen = pair.first.size();
                if (klen <= 31) {
                    write_u8(out, static_cast<uint8_t>(0xA0 | klen));
                } else if (klen <= 0xFF) {
                    write_u8(out, 0xD9);
                    write_u8(out, static_cast<uint8_t>(klen));
                } else if (klen <= 0xFFFF) {
                    write_u8(out, 0xDA);
                    write_u16_be(out, static_cast<uint16_t>(klen));
                } else {
                    write_u8(out, 0xDB);
                    write_u32_be(out, static_cast<uint32_t>(klen));
                }
                out.insert(out.end(), pair.first.begin(), pair.first.end());
                // Value
                serialize_msgpack_impl(pair.second, out);
            }
            break;
        }
    }
}

class msgpack_reader {
public:
    static constexpr size_t max_depth = 128;

    msgpack_reader(const uint8_t* data, size_t size)
        : m_data(data), m_size(size), m_pos(0), m_depth(0) {}

    value parse() {
        if (m_depth > max_depth) {
            throw msgpack_error("Maximum MessagePack nesting depth exceeded (potential stack overflow)");
        }
        if (m_pos >= m_size) {
            throw msgpack_error("Unexpected end of MessagePack input");
        }
        uint8_t tag = m_data[m_pos++];

        // Positive fixint: 0x00 - 0x7f
        if (tag <= 0x7F) {
            return value(static_cast<int64_t>(tag));
        }
        // Fixmap: 0x80 - 0x8f
        if (tag >= 0x80 && tag <= 0x8F) {
            return parse_map(tag & 0x0F);
        }
        // Fixarray: 0x90 - 0x9f
        if (tag >= 0x90 && tag <= 0x9F) {
            return parse_array(tag & 0x0F);
        }
        // Fixstr: 0xa0 - 0xbf
        if (tag >= 0xA0 && tag <= 0xBF) {
            return parse_string_bytes(tag & 0x1F);
        }
        // Negative fixint: 0xe0 - 0xff
        if (tag >= 0xE0) {
            return value(static_cast<int64_t>(static_cast<int8_t>(tag)));
        }

        switch (tag) {
            case 0xC0: return value(nullptr);
            case 0xC2: return value(false);
            case 0xC3: return value(true);
            case 0xC4: { // bin 8
                ensure_bytes(1);
                size_t len = m_data[m_pos++];
                return parse_string_bytes(len);
            }
            case 0xC5: { // bin 16
                ensure_bytes(2);
                size_t len = read_u16_be(&m_data[m_pos]);
                m_pos += 2;
                return parse_string_bytes(len);
            }
            case 0xC6: { // bin 32
                ensure_bytes(4);
                size_t len = read_u32_be(&m_data[m_pos]);
                m_pos += 4;
                return parse_string_bytes(len);
            }
            case 0xCA: { // float 32
                ensure_bytes(4);
                uint32_t raw = read_u32_be(&m_data[m_pos]);
                m_pos += 4;
                float f = 0.0f;
                std::memcpy(&f, &raw, sizeof(float));
                return value(static_cast<double>(f));
            }
            case 0xCB: { // float 64
                ensure_bytes(8);
                uint64_t raw = read_u64_be(&m_data[m_pos]);
                m_pos += 8;
                double d = 0.0;
                std::memcpy(&d, &raw, sizeof(double));
                return value(d);
            }
            case 0xCC: { // uint 8
                ensure_bytes(1);
                return value(static_cast<uint64_t>(m_data[m_pos++]));
            }
            case 0xCD: { // uint 16
                ensure_bytes(2);
                uint16_t v = read_u16_be(&m_data[m_pos]);
                m_pos += 2;
                return value(static_cast<uint64_t>(v));
            }
            case 0xCE: { // uint 32
                ensure_bytes(4);
                uint32_t v = read_u32_be(&m_data[m_pos]);
                m_pos += 4;
                return value(static_cast<uint64_t>(v));
            }
            case 0xCF: { // uint 64
                ensure_bytes(8);
                uint64_t v = read_u64_be(&m_data[m_pos]);
                m_pos += 8;
                return value(v);
            }
            case 0xD0: { // int 8
                ensure_bytes(1);
                return value(static_cast<int64_t>(static_cast<int8_t>(m_data[m_pos++])));
            }
            case 0xD1: { // int 16
                ensure_bytes(2);
                int16_t v = static_cast<int16_t>(read_u16_be(&m_data[m_pos]));
                m_pos += 2;
                return value(static_cast<int64_t>(v));
            }
            case 0xD2: { // int 32
                ensure_bytes(4);
                int32_t v = static_cast<int32_t>(read_u32_be(&m_data[m_pos]));
                m_pos += 4;
                return value(static_cast<int64_t>(v));
            }
            case 0xD3: { // int 64
                ensure_bytes(8);
                int64_t v = static_cast<int64_t>(read_u64_be(&m_data[m_pos]));
                m_pos += 8;
                return value(v);
            }
            case 0xD9: { // str 8
                ensure_bytes(1);
                size_t len = m_data[m_pos++];
                return parse_string_bytes(len);
            }
            case 0xDA: { // str 16
                ensure_bytes(2);
                size_t len = read_u16_be(&m_data[m_pos]);
                m_pos += 2;
                return parse_string_bytes(len);
            }
            case 0xDB: { // str 32
                ensure_bytes(4);
                size_t len = read_u32_be(&m_data[m_pos]);
                m_pos += 4;
                return parse_string_bytes(len);
            }
            case 0xDC: { // array 16
                ensure_bytes(2);
                size_t len = read_u16_be(&m_data[m_pos]);
                m_pos += 2;
                return parse_array(len);
            }
            case 0xDD: { // array 32
                ensure_bytes(4);
                size_t len = read_u32_be(&m_data[m_pos]);
                m_pos += 4;
                return parse_array(len);
            }
            case 0xDE: { // map 16
                ensure_bytes(2);
                size_t len = read_u16_be(&m_data[m_pos]);
                m_pos += 2;
                return parse_map(len);
            }
            case 0xDF: { // map 32
                ensure_bytes(4);
                size_t len = read_u32_be(&m_data[m_pos]);
                m_pos += 4;
                return parse_map(len);
            }
            default:
                throw msgpack_error("Unsupported or invalid MessagePack tag: 0x" + std::to_string(tag));
        }
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos;
    size_t m_depth;

    void ensure_bytes(size_t n) {
        if (m_pos + n > m_size) {
            throw msgpack_error("Unexpected end of MessagePack input, expected " + std::to_string(n) + " more bytes");
        }
    }

    value parse_string_bytes(size_t len) {
        ensure_bytes(len);
        std::string str(reinterpret_cast<const char*>(&m_data[m_pos]), len);
        m_pos += len;
        return value(std::move(str));
    }

    value parse_array(size_t len) {
        if (len > (m_size - m_pos)) {
            throw msgpack_error("Array size exceeds remaining bytes in MessagePack input");
        }
        value::array_t arr;
        arr.reserve((std::min)(len, size_t(4096)));
        m_depth++;
        for (size_t i = 0; i < len; ++i) {
            arr.push_back(parse());
        }
        m_depth--;
        return value(std::move(arr));
    }

    value parse_map(size_t len) {
        if (len > (m_size - m_pos)) {
            throw msgpack_error("Map size exceeds remaining bytes in MessagePack input");
        }
        value::object_t obj;
        obj.reserve((std::min)(len, size_t(4096)));
        m_depth++;
        for (size_t i = 0; i < len; ++i) {
            value key_v = parse();
            if (!key_v.is_string()) {
                throw msgpack_error("Map key in MessagePack must be a string");
            }
            value val_v = parse();
            obj.emplace_back(std::move(key_v.get_ref_string()), std::move(val_v));
        }
        m_depth--;
        return value(std::move(obj));
    }
};

} // namespace detail

/**
 * @brief Serializes a Senko JSON value into a binary MessagePack buffer.
 */
inline std::vector<uint8_t> to_msgpack(const value& j) {
    std::vector<uint8_t> out;
    detail::serialize_msgpack_impl(j, out);
    return out;
}

/**
 * @brief Deserializes a binary MessagePack buffer into a Senko JSON value.
 */
inline value from_msgpack(const uint8_t* data, size_t size) {
    detail::msgpack_reader reader(data, size);
    return reader.parse();
}

inline value from_msgpack(const std::vector<uint8_t>& bytes) {
    return from_msgpack(bytes.data(), bytes.size());
}

inline value from_msgpack(std::string_view bytes) {
    return from_msgpack(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

} // namespace senko
