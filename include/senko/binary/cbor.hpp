#pragma once

#include "../fwd.hpp"
#include "../error.hpp"
#include "../value.hpp"

#include <vector>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <limits>
#include <cmath>

namespace senko {

class cbor_error : public exception {
public:
    explicit cbor_error(std::string msg) : exception("[senko::cbor_error] " + std::move(msg)) {}
};

namespace detail {

inline double decode_half_float(uint16_t raw) {
    uint16_t sign = (raw >> 15) & 0x0001;
    uint16_t exp  = (raw >> 10) & 0x001F;
    uint16_t mant = raw & 0x03FF;

    double val = 0.0;
    if (exp == 0) {
        if (mant == 0) {
            val = 0.0;
        } else {
            val = std::ldexp(static_cast<double>(mant), -24);
        }
    } else if (exp == 31) {
        if (mant == 0) {
            val = std::numeric_limits<double>::infinity();
        } else {
            val = std::numeric_limits<double>::quiet_NaN();
        }
    } else {
        val = std::ldexp(static_cast<double>(1024 + mant), static_cast<int>(exp) - 25);
    }
    return sign ? -val : val;
}

inline void cbor_write_type_and_val(std::vector<uint8_t>& out, uint8_t major, uint64_t val) {
    uint8_t m = static_cast<uint8_t>(major << 5);
    if (val < 24) {
        out.push_back(static_cast<uint8_t>(m | val));
    } else if (val <= 0xFF) {
        out.push_back(static_cast<uint8_t>(m | 24));
        out.push_back(static_cast<uint8_t>(val));
    } else if (val <= 0xFFFF) {
        out.push_back(static_cast<uint8_t>(m | 25));
        out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(val & 0xFF));
    } else if (val <= 0xFFFFFFFFULL) {
        out.push_back(static_cast<uint8_t>(m | 26));
        out.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
        out.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(val & 0xFF));
    } else {
        out.push_back(static_cast<uint8_t>(m | 27));
        for (int i = 7; i >= 0; --i) {
            out.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
        }
    }
}

inline void serialize_cbor_impl(const value& v, std::vector<uint8_t>& out) {
    switch (v.type()) {
        case value_t::null:
            out.push_back(0xF6); // null
            break;
        case value_t::boolean:
            out.push_back(v.get<bool>() ? 0xF5 : 0xF4); // true : false
            break;
        case value_t::number_integer: {
            int64_t val = v.get<int64_t>();
            if (val >= 0) {
                cbor_write_type_and_val(out, 0, static_cast<uint64_t>(val)); // Major 0: unsigned
            } else {
                cbor_write_type_and_val(out, 1, static_cast<uint64_t>(-1 - val)); // Major 1: negative
            }
            break;
        }
        case value_t::number_unsigned: {
            cbor_write_type_and_val(out, 0, v.get<uint64_t>());
            break;
        }
        case value_t::number_float: {
            double d = v.get<double>();
            out.push_back(0xFB); // float 64
            uint64_t raw = 0;
            std::memcpy(&raw, &d, sizeof(double));
            for (int i = 7; i >= 0; --i) {
                out.push_back(static_cast<uint8_t>((raw >> (i * 8)) & 0xFF));
            }
            break;
        }
        case value_t::string: {
            const std::string& str = v.get_ref_string();
            cbor_write_type_and_val(out, 3, str.size()); // Major 3: text string
            out.insert(out.end(), str.begin(), str.end());
            break;
        }
        case value_t::array: {
            const auto& arr = v.get_ref_array();
            cbor_write_type_and_val(out, 4, arr.size()); // Major 4: array
            for (const auto& elem : arr) {
                serialize_cbor_impl(elem, out);
            }
            break;
        }
        case value_t::object: {
            const auto& obj = v.get_ref_object();
            cbor_write_type_and_val(out, 5, obj.size()); // Major 5: map
            for (const auto& pair : obj) {
                // Key (text string)
                cbor_write_type_and_val(out, 3, pair.first.size());
                out.insert(out.end(), pair.first.begin(), pair.first.end());
                // Value
                serialize_cbor_impl(pair.second, out);
            }
            break;
        }
    }
}

class cbor_reader {
public:
    static constexpr size_t max_depth = 128;

    cbor_reader(const uint8_t* data, size_t size)
        : m_data(data), m_size(size), m_pos(0), m_depth(0) {}

    value parse() {
        if (m_depth > max_depth) {
            throw cbor_error("Maximum CBOR nesting depth exceeded (potential stack overflow)");
        }
        if (m_pos >= m_size) {
            throw cbor_error("Unexpected end of CBOR input");
        }
        uint8_t initial = m_data[m_pos++];
        uint8_t major = initial >> 5;
        uint8_t info = initial & 0x1F;

        uint64_t val = read_length(info);

        switch (major) {
            case 0: // Unsigned integer
                if (val <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) {
                    return value(static_cast<int64_t>(val));
                }
                return value(val);
            case 1: // Negative integer (-1 - val)
                if (val <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) {
                    return value(static_cast<int64_t>(-1 - static_cast<int64_t>(val)));
                }
                throw cbor_error("Negative integer underflow in CBOR");
            case 2: // Byte string (treat as hex or raw string for JSON DOM)
            case 3: { // Text string
                ensure_bytes(val);
                std::string str(reinterpret_cast<const char*>(&m_data[m_pos]), val);
                m_pos += val;
                return value(std::move(str));
            }
            case 4: { // Array
                if (val > (m_size - m_pos)) {
                    throw cbor_error("Array size exceeds remaining bytes in CBOR input");
                }
                value::array_t arr;
                arr.reserve(static_cast<size_t>((std::min)(val, uint64_t(4096))));
                m_depth++;
                for (size_t i = 0; i < val; ++i) {
                    arr.push_back(parse());
                }
                m_depth--;
                return value(std::move(arr));
            }
            case 5: { // Map
                if (val > (m_size - m_pos)) {
                    throw cbor_error("Map size exceeds remaining bytes in CBOR input");
                }
                value::object_t obj;
                obj.reserve(static_cast<size_t>((std::min)(val, uint64_t(4096))));
                m_depth++;
                for (size_t i = 0; i < val; ++i) {
                    value key_v = parse();
                    if (!key_v.is_string()) {
                        throw cbor_error("Map key in CBOR must be a string");
                    }
                    value val_v = parse();
                    obj.emplace_back(std::move(key_v.get_ref_string()), std::move(val_v));
                }
                m_depth--;
                return value(std::move(obj));
            }
            case 7: { // Simple / Float
                if (info == 20) return value(false);
                if (info == 21) return value(true);
                if (info == 22) return value(nullptr);
                if (info == 25) { // float 16 (half-precision)
                    return value(decode_half_float(static_cast<uint16_t>(val)));
                }
                if (info == 26) { // float 32
                    uint32_t raw = static_cast<uint32_t>(val);
                    float f = 0.0f;
                    std::memcpy(&f, &raw, sizeof(float));
                    return value(static_cast<double>(f));
                }
                if (info == 27) { // float 64
                    uint64_t raw = val;
                    double d = 0.0;
                    std::memcpy(&d, &raw, sizeof(double));
                    return value(d);
                }
                throw cbor_error("Unsupported CBOR simple type or info: " + std::to_string(info));
            }
            default:
                throw cbor_error("Unsupported CBOR major type: " + std::to_string(major));
        }
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos;
    size_t m_depth;

    void ensure_bytes(size_t n) {
        if (m_pos + n > m_size) {
            throw cbor_error("Unexpected end of CBOR input, expected " + std::to_string(n) + " more bytes");
        }
    }

    uint64_t read_length(uint8_t info) {
        if (info < 24) {
            return info;
        } else if (info == 24) {
            ensure_bytes(1);
            return m_data[m_pos++];
        } else if (info == 25) {
            ensure_bytes(2);
            uint16_t v = (uint16_t(m_data[m_pos]) << 8) | uint16_t(m_data[m_pos + 1]);
            m_pos += 2;
            return v;
        } else if (info == 26) {
            ensure_bytes(4);
            uint32_t v = (uint32_t(m_data[m_pos]) << 24) |
                         (uint32_t(m_data[m_pos + 1]) << 16) |
                         (uint32_t(m_data[m_pos + 2]) << 8) |
                         uint32_t(m_data[m_pos + 3]);
            m_pos += 4;
            return v;
        } else if (info == 27) {
            ensure_bytes(8);
            uint64_t v = 0;
            for (int i = 0; i < 8; ++i) {
                v = (v << 8) | uint64_t(m_data[m_pos + i]);
            }
            m_pos += 8;
            return v;
        }
        throw cbor_error("Indefinite length or invalid CBOR additional info: " + std::to_string(info));
    }
};

} // namespace detail

/**
 * @brief Serializes a Senko JSON value into a binary CBOR buffer (RFC 8949).
 */
inline std::vector<uint8_t> to_cbor(const value& j) {
    std::vector<uint8_t> out;
    detail::serialize_cbor_impl(j, out);
    return out;
}

/**
 * @brief Deserializes a binary CBOR buffer into a Senko JSON value.
 */
inline value from_cbor(const uint8_t* data, size_t size) {
    detail::cbor_reader reader(data, size);
    return reader.parse();
}

inline value from_cbor(const std::vector<uint8_t>& bytes) {
    return from_cbor(bytes.data(), bytes.size());
}

inline value from_cbor(std::string_view bytes) {
    return from_cbor(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

} // namespace senko
