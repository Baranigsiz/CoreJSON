#include "test_framework.hpp"
#include <senko/senko.hpp>

using json = senko::json;
using namespace senko::literals;

TEST_CASE("JSON Pointer - RFC 6901 Specification Tests") {
    // Official RFC 6901 Example Document
    std::string rfc_doc = R"({
        "foo": ["bar", "baz"],
        "": 0,
        "a/b": 1,
        "c%d": 2,
        "e^f": 3,
        "g|h": 4,
        "i\\j": 5,
        "k\"l": 6,
        " ": 7,
        "m~n": 8
    })";

    json doc = json::parse(rfc_doc);

    // "" -> whole document
    CHECK_EQ(doc[""_json_pointer]["foo"][0].get<std::string>(), "bar");

    // "/foo" -> ["bar", "baz"]
    CHECK_EQ(doc["/foo"_json_pointer].size(), 2);

    // "/foo/0" -> "bar"
    CHECK_EQ(doc["/foo/0"_json_pointer].get<std::string>(), "bar");

    // "/" -> 0
    CHECK_EQ(doc["/"_json_pointer].get<int>(), 0);

    // "/a~1b" -> 1
    CHECK_EQ(doc["/a~1b"_json_pointer].get<int>(), 1);

    // "/c%d" -> 2
    CHECK_EQ(doc["/c%d"_json_pointer].get<int>(), 2);

    // "/e^f" -> 3
    CHECK_EQ(doc["/e^f"_json_pointer].get<int>(), 3);

    // "/g|h" -> 4
    CHECK_EQ(doc["/g|h"_json_pointer].get<int>(), 4);

    // "/i\\j" -> 5
    CHECK_EQ(doc["/i\\j"_json_pointer].get<int>(), 5);

    // "/k\"l" -> 6
    CHECK_EQ(doc["/k\"l"_json_pointer].get<int>(), 6);

    // "/ " -> 7
    CHECK_EQ(doc["/ "_json_pointer].get<int>(), 7);

    // "/m~0n" -> 8
    CHECK_EQ(doc["/m~0n"_json_pointer].get<int>(), 8);
}

TEST_CASE("JSON Pointer - Error Handling") {
    json doc = json::parse(R"({"a": {"b": [10, 20]}})");

    // Invalid start char
    CHECK_THROWS(doc["invalid_start"_json_pointer]);

    // Key not found
    CHECK_THROWS(doc["/a/non_existent"_json_pointer]);

    // Array index out of range
    CHECK_THROWS(doc["/a/b/5"_json_pointer]);

    // Non-integer index into array
    CHECK_THROWS(doc["/a/b/invalid"_json_pointer]);

    // Leading zeros not allowed per RFC 6901
    CHECK_THROWS(doc["/a/b/01"_json_pointer]);

    // Plus/minus signs not allowed per RFC 6901
    CHECK_THROWS(doc["/a/b/+1"_json_pointer]);
    CHECK_THROWS(doc["/a/b/-1"_json_pointer]);

    // Safe fallback value_or() with JSON Pointer
    CHECK_EQ(doc.value_or("/a/b/0"_json_pointer, 0), 10);
    CHECK_EQ(doc.value_or("/a/b/99"_json_pointer, 999), 999);
    CHECK_EQ(doc.value_or("/missing/key"_json_pointer, std::string("default")), "default");
}

TEST_CASE("JSON Pointer - Flatten and Unflatten") {
    json original = R"({
        "name": "Senko",
        "age": 500,
        "location": {
            "country": "Japan",
            "city": "Tokyo"
        },
        "items": ["tea", "rice"]
    })"_json;

    // 1. Flatten
    json flat = original.flatten();
    CHECK(flat.is_object());
    CHECK_EQ(flat["/name"].get<std::string>(), "Senko");
    CHECK_EQ(flat["/age"].get<int>(), 500);
    CHECK_EQ(flat["/location/country"].get<std::string>(), "Japan");
    CHECK_EQ(flat["/location/city"].get<std::string>(), "Tokyo");
    CHECK_EQ(flat["/items/0"].get<std::string>(), "tea");
    CHECK_EQ(flat["/items/1"].get<std::string>(), "rice");

    // 2. Unflatten (Roundtrip)
    json restored = flat.unflatten();
    CHECK_EQ(restored, original);

    // 3. Unflatten with invalid array indices should throw pointer_error safely
    json bad_flat1 = json::object({{"/0", "valid"}, {"/invalid_token", "fail"}});
    CHECK_THROWS(bad_flat1.unflatten());

    json bad_flat2 = json::object({{"/0", "valid"}, {"/999999", "too_large"}});
    CHECK_THROWS(bad_flat2.unflatten());
}

TEST_CASE("JSON Pointer - Operators, Hierarchy & std::hash") {
    senko::json_pointer root;
    CHECK(root.empty());
    CHECK_THROWS(root.parent_pointer());
    CHECK_THROWS(root.back());

    // Operator / and /=
    auto p1 = "/users"_json_pointer / 0 / "profile";
    CHECK_EQ(p1.to_string(), "/users/0/profile");
    CHECK_EQ(p1.back(), "profile");

    auto parent = p1.parent_pointer();
    CHECK_EQ(parent.to_string(), "/users/0");
    CHECK_EQ(parent.back(), "0");

    auto grandparent = parent.parent_pointer();
    CHECK_EQ(grandparent.to_string(), "/users");

    senko::json_pointer mut = "/api"_json_pointer;
    mut /= "v1";
    mut /= 2;
    CHECK_EQ(mut.to_string(), "/api/v1/2");

    // Equality, inequality & ordering
    senko::json_pointer ptr_a("/a/b");
    senko::json_pointer ptr_b("/a/b");
    senko::json_pointer ptr_c("/a/c");
    CHECK(ptr_a == ptr_b);
    CHECK(ptr_a != ptr_c);
    CHECK(ptr_a < ptr_c);

    // std::unordered_set<senko::json_pointer>
    std::unordered_set<senko::json_pointer> ptr_set;
    ptr_set.insert(ptr_a);
    ptr_set.insert(ptr_c);
    CHECK_EQ(ptr_set.size(), 2);
    CHECK_EQ(ptr_set.count(ptr_b), 1);
}

TEST_CASE("JSON Pointer - doc.contains(json_pointer)") {
    json doc = {
        {"user", {
            {"name", "Baran"},
            {"tags", {"c++", "performance"}},
            {"nested", {
                {"active", true}
            }}
        }}
    };

    // Existing paths
    CHECK(doc.contains(""_json_pointer));
    CHECK(doc.contains("/user"_json_pointer));
    CHECK(doc.contains("/user/name"_json_pointer));
    CHECK(doc.contains("/user/tags"_json_pointer));
    CHECK(doc.contains("/user/tags/0"_json_pointer));
    CHECK(doc.contains("/user/tags/1"_json_pointer));
    CHECK(doc.contains("/user/nested/active"_json_pointer));

    // Non-existing paths
    CHECK(!doc.contains("/missing"_json_pointer));
    CHECK(!doc.contains("/user/missing"_json_pointer));
    CHECK(!doc.contains("/user/tags/2"_json_pointer));      // Out of bounds
    CHECK(!doc.contains("/user/tags/99999"_json_pointer));  // Far out of bounds
    CHECK(!doc.contains("/user/tags/bad_idx"_json_pointer));// String on array
    CHECK(!doc.contains("/user/name/sub"_json_pointer));    // Navigating primitive
    CHECK(!doc.contains("/user/tags/-"_json_pointer));      // '-' token
}


