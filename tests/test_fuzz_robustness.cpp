#include "test_framework.hpp"
#include <senko/senko.hpp>
#include <random>
#include <vector>
#include <string>
#include <string_view>
#include <limits>

using json = senko::json;

TEST_CASE("Fuzz - Truncated JSON Prefix Slicing") {
    // Valid sample JSON with all types
    std::string sample = R"({
        "string": "Hello, \u0041\uD83D\uDE00 World!",
        "number": 12345.6789e-2,
        "integer": -9876543210,
        "boolean_true": true,
        "boolean_false": false,
        "null_val": null,
        "array": [1, "two", 3.0, null, [true, false], {"nested": "value"}],
        "object": {"a": 1, "b": 2, "c": [1, 2, 3]}
    })";

    // Truncate at every single character index from 1 to sample.size() - 1
    // None should crash or hang - all must either parse (unlikely for incomplete) or throw exception
    int rejected_count = 0;
    for (size_t i = 1; i < sample.size(); ++i) {
        std::string_view prefix(sample.data(), i);
        try {
            auto j = json::parse(prefix);
            (void)j;
        } catch (const senko::exception&) {
            rejected_count++;
        }
    }
    CHECK(rejected_count > 0);
    CHECK_EQ(rejected_count, static_cast<int>(sample.size() - 1));
}

TEST_CASE("Fuzz - Malformed Numbers Robustness") {
    const char* malformed_numbers[] = {
        "-", "+", "+1", "++1", "--1",
        ".", ".5", "1.", "1.2.3", "1.e", "1.e+", "1.e-",
        "1e", "1e+", "1e-", "1E", "1E+", "1E-",
        "01", "00", "-01", "-00",
        "0x12", "0XFF", "0b101",
        "NaN", "nan", "Infinity", "inf", "-Infinity",
        "1a", "123_456", "1.2.3.4",
        "1e99999999999999999999999999999999999999999999",
        "-1e9999999999999999999999999999999999999999999",
        "9999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999",
        "-999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999"
    };

    for (const char* num_str : malformed_numbers) {
        std::string wrapped = std::string("{\"val\": ") + num_str + "}";
        try {
            auto j = json::parse(wrapped);
            // Some very large numbers might parse into double infinity or overflow gracefully
            (void)j;
        } catch (const senko::exception&) {
            // Expected for malformed numbers
        } catch (...) {
            // Unhandled exception - fail
            CHECK(false);
        }
    }
}

TEST_CASE("Fuzz - Malformed String Escapes & Unicode") {
    const char* bad_strings[] = {
        "\"unclosed",
        "\"\\\"",
        "\"\\\"",
        "\"\\u\"",
        "\"\\u0\"",
        "\"\\u00\"",
        "\"\\u000\"",
        "\"\\uZZZZ\"",
        "\"\\uD800\"",         // Lone high surrogate without low surrogate
        "\"\\uD800\\u0000\"", // High surrogate followed by non-surrogate
        "\"\\uD800\\uDBFF\"", // High surrogate followed by another high surrogate
        "\"\\uDC00\"",         // Lone low surrogate
        "\"\\x00\"",          // Invalid \x escape
        "\"\\c\"",            // Invalid \c escape
        "\"\n\"",             // Raw unescaped newline
        "\"\r\"",             // Raw unescaped carriage return
        "\"\t\"",             // Raw unescaped tab
        "\"\x01\"",           // Raw unescaped control character 0x01
        "\"\x1F\"",           // Raw unescaped control character 0x1F
    };

    for (const char* str : bad_strings) {
        try {
            auto j = json::parse(str);
            (void)j;
        } catch (const senko::exception&) {
            // Successfully rejected
        } catch (...) {
            CHECK(false);
        }
    }
}

TEST_CASE("Fuzz - Truncated Binary Streams (CBOR & MessagePack)") {
    json doc = {
        {"name", "Fuzzing Target"},
        {"values", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}},
        {"meta", {
            {"pi", 3.1415926535},
            {"flag", true},
            {"empty", nullptr}
        }}
    };

    std::vector<uint8_t> msgpack_bytes = senko::to_msgpack(doc);
    std::vector<uint8_t> cbor_bytes = senko::to_cbor(doc);

    CHECK(!msgpack_bytes.empty());
    CHECK(!cbor_bytes.empty());

    // Truncate MessagePack at each byte length
    for (size_t len = 0; len < msgpack_bytes.size(); ++len) {
        std::vector<uint8_t> truncated(msgpack_bytes.begin(), msgpack_bytes.begin() + len);
        try {
            auto j = senko::from_msgpack(truncated);
            (void)j;
        } catch (const senko::msgpack_error&) {
            // Expected safe rejection
        } catch (...) {
            CHECK(false);
        }
    }

    // Truncate CBOR at each byte length
    for (size_t len = 0; len < cbor_bytes.size(); ++len) {
        std::vector<uint8_t> truncated(cbor_bytes.begin(), cbor_bytes.begin() + len);
        try {
            auto j = senko::from_cbor(truncated);
            (void)j;
        } catch (const senko::cbor_error&) {
            // Expected safe rejection
        } catch (...) {
            CHECK(false);
        }
    }
}

TEST_CASE("Fuzz - Malformed JSONPath Queries") {
    json doc = {
        {"store", {
            {"book", {
                {{"title", "A"}, {"price", 10}},
                {{"title", "B"}, {"price", 20}}
            }}
        }}
    };

    const char* bad_queries[] = {
        "",
        "store.book",         // Missing leading '$'
        "$..",                // Trailing double dot without key
        "$.",                 // Trailing single dot
        "$[",                 // Unclosed bracket
        "$[]",                // Empty bracket
        "$[0:1:0]",           // Step 0 slice (must throw step cannot be 0)
        "$[?]",               // Empty filter
        "$[?(@.price)]",      // Filter without comparison operator
        "$[?(@.price ==)]",   // Filter with missing comparison value
        "$[?(@.price < not_a_num)]", // Filter with non-parsable unquoted literal
        "$['unclosed string", // Unclosed quote inside bracket
        "$.store.book[99999999999999999999999999999]", // Oversized integer
        "$.store.book[-99999999999999999999999999999]",
        "$..*",
        "$..book[*].price",
        "$~invalid"
    };

    for (const char* query : bad_queries) {
        try {
            auto res = doc.jsonpath(query);
            (void)res;
            auto refs = doc.jsonpath_refs(query);
            (void)refs;
        } catch (const senko::jsonpath_error&) {
            // Expected for invalid syntax
        } catch (const senko::exception&) {
            // Expected base exception
        } catch (...) {
            CHECK(false);
        }
    }
}

TEST_CASE("Fuzz - Malformed JSON Pointer & JSON Patch") {
    json doc = {{"a", 1}, {"b", {{"c", 2}}}};

    const char* bad_pointers[] = {
        "~",
        "~2",
        "/~",
        "/~2",
        "a/b",                // Missing leading slash
        "/b/c/d/e/f",
        "/-1",
        "/99999999999999999999999999999"
    };

    for (const char* ptr : bad_pointers) {
        try {
            senko::json_pointer jp(ptr);
            auto val = doc.at_ptr(jp);
            (void)val;
        } catch (const senko::exception&) {
            // Handled safely (pointer_error, out_of_range, etc.)
        } catch (...) {
            CHECK(false);
        }
    }

    // Malformed patch documents
    json bad_patches[] = {
        json::array(), // Empty patch (valid no-op)
        json::object(), // Patch must be an array
        json::parse(R"([{"op": "add"}])"), // Missing path & value
        json::parse(R"([{"op": "unknown", "path": "/a", "value": 1}])"), // Unknown op
        json::parse(R"([{"op": "move", "path": "/a"}])"), // Missing 'from'
        json::parse(R"([{"op": "copy", "from": "/a"}])"), // Missing 'path'
        json::parse(R"([{"op": "test", "path": "/a", "value": 999}])") // Value mismatch
    };

    for (const auto& p : bad_patches) {
        try {
            auto patched = doc.patch(p);
            (void)patched;
        } catch (const senko::exception&) {
            // Expected
        } catch (...) {
            CHECK(false);
        }
    }
}

TEST_CASE("Fuzz - Pseudo-Random Byte Stream Mutation Engine (10,000 runs)") {
    // Deterministic seed for reproducible fuzzing runs
    std::mt19937 rng(0x5E4C01);
    std::uniform_int_distribution<int> len_dist(1, 128);
    std::uniform_int_distribution<int> byte_dist(0, 255);

    int total_runs = 10000;
    int processed_json_ok = 0;
    int caught_json_errors = 0;
    int processed_msgpack_ok = 0;
    int caught_msgpack_errors = 0;
    int processed_cbor_ok = 0;
    int caught_cbor_errors = 0;

    std::vector<uint8_t> buffer;
    buffer.reserve(256);

    for (int run = 0; run < total_runs; ++run) {
        size_t len = static_cast<size_t>(len_dist(rng));
        buffer.resize(len);
        for (size_t i = 0; i < len; ++i) {
            buffer[i] = static_cast<uint8_t>(byte_dist(rng));
        }

        // 1. Fuzz JSON parser with raw bytes as string_view
        std::string_view raw_str(reinterpret_cast<const char*>(buffer.data()), len);
        try {
            auto j = json::parse(raw_str);
            processed_json_ok++;
        } catch (const senko::exception&) {
            caught_json_errors++;
        }

        // 2. Fuzz MessagePack binary decoder
        try {
            auto j = senko::from_msgpack(buffer);
            processed_msgpack_ok++;
        } catch (const senko::exception&) {
            caught_msgpack_errors++;
        }

        // 3. Fuzz CBOR binary decoder
        try {
            auto j = senko::from_cbor(buffer);
            processed_cbor_ok++;
        } catch (const senko::exception&) {
            caught_cbor_errors++;
        }
    }

    // Verify all 10,000 random inputs were processed safely without crashes
    CHECK_EQ(processed_json_ok + caught_json_errors, total_runs);
    CHECK_EQ(processed_msgpack_ok + caught_msgpack_errors, total_runs);
    CHECK_EQ(processed_cbor_ok + caught_cbor_errors, total_runs);
}
