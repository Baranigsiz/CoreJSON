#include "test_framework.hpp"
#include <senko/senko.hpp>
#include <sstream>

using json = senko::json;

TEST_CASE("JSON Writer - Basic Minified Object & Primitives") {
    senko::json_writer w;
    w.start_object()
     .key("name").value("SenkoJSON")
     .key("version").value(2)
     .key("floating").value(3.14)
     .key("active").value(true)
     .key("inactive").value(false)
     .key("empty").null_value()
     .end_object();

    CHECK(w.is_complete());
    std::string expected = "{\"name\":\"SenkoJSON\",\"version\":2,\"floating\":3.14,\"active\":true,\"inactive\":false,\"empty\":null}";
    CHECK_EQ(w.str(), expected);

    // Verify round-trip parsing
    json parsed = json::parse(w.str());
    CHECK_EQ(parsed["name"].get<std::string>(), "SenkoJSON");
    CHECK_EQ(parsed["version"].get<int>(), 2);
    CHECK_EQ(parsed["active"].get<bool>(), true);
    CHECK_EQ(parsed["inactive"].get<bool>(), false);
    CHECK(parsed["empty"].is_null());
}

TEST_CASE("JSON Writer - Nested Objects and Arrays") {
    senko::json_writer w;
    w.start_object()
     .key("users").start_array()
        .start_object()
            .key("id").value(1)
            .key("username").value("alice")
        .end_object()
        .start_object()
            .key("id").value(2)
            .key("username").value("bob")
        .end_object()
     .end_array()
     .key("numbers").start_array()
        .value(10)
        .value(20)
        .value(30)
     .end_array()
     .end_object();

    CHECK(w.is_complete());
    json doc = json::parse(w.str());
    CHECK_EQ(doc["users"].size(), 2);
    CHECK_EQ(doc["users"][0]["username"].get<std::string>(), "alice");
    CHECK_EQ(doc["users"][1]["id"].get<int>(), 2);
    CHECK_EQ(doc["numbers"].size(), 3);
    CHECK_EQ(doc["numbers"][2].get<int>(), 30);
}

TEST_CASE("JSON Writer - Pretty Printing") {
    senko::json_writer w(2);
    w.start_object()
     .key("key").value("val")
     .key("arr").start_array()
        .value(1)
     .end_array()
     .end_object();

    std::string formatted = w.str();
    CHECK(formatted.find('\n') != std::string::npos);
    CHECK(formatted.find("  \"key\": \"val\"") != std::string::npos);
    CHECK(formatted.find("  \"arr\": [\n    1\n  ]") != std::string::npos);

    json parsed = json::parse(formatted);
    CHECK_EQ(parsed["key"].get<std::string>(), "val");
    CHECK_EQ(parsed["arr"][0].get<int>(), 1);
}

TEST_CASE("JSON Writer - Embedding Value and Raw JSON") {
    json child = {{"sub_id", 42}, {"tags", {"fast", "modern"}}};

    senko::json_writer w;
    w.start_object()
     .key("status").value("ok")
     .key("child").value(child)
     .key("preformatted").raw_json("{\"direct\": true}")
     .end_object();

    json parsed = json::parse(w.str());
    CHECK_EQ(parsed["status"].get<std::string>(), "ok");
    CHECK_EQ(parsed["child"]["sub_id"].get<int>(), 42);
    CHECK_EQ(parsed["child"]["tags"][0].get<std::string>(), "fast");
    CHECK_EQ(parsed["preformatted"]["direct"].get<bool>(), true);
}

TEST_CASE("JSON Writer - Target Stream and External String Buffer") {
    std::string buffer;
    {
        senko::json_writer w(buffer);
        w.start_array()
         .value("item1")
         .value("item2")
         .end_array();
    }
    CHECK_EQ(buffer, "[\"item1\",\"item2\"]");

    std::ostringstream oss;
    {
        senko::json_writer w(oss);
        w.start_object()
         .key("streamed").value(true)
         .end_object();
        w.flush();
    }
    CHECK_EQ(oss.str(), "{\"streamed\":true}");
}

TEST_CASE("JSON Writer - Error Handling & Contract Enforcement") {
    // 1. key() inside array
    {
        senko::json_writer w;
        w.start_array();
        CHECK_THROWS(w.key("test"));
    }

    // 2. value() in object without key
    {
        senko::json_writer w;
        w.start_object();
        CHECK_THROWS(w.value(123));
    }

    // 3. Duplicate key without value
    {
        senko::json_writer w;
        w.start_object();
        w.key("first");
        CHECK_THROWS(w.key("second"));
    }

    // 4. Mismatched scope closing
    {
        senko::json_writer w;
        w.start_object();
        CHECK_THROWS(w.end_array());
    }
    {
        senko::json_writer w;
        w.start_array();
        CHECK_THROWS(w.end_object());
    }

    // 5. Premature end of object when expecting value
    {
        senko::json_writer w;
        w.start_object();
        w.key("hanging");
        CHECK_THROWS(w.end_object());
    }

    // 6. Multiple roots
    {
        senko::json_writer w;
        w.value(1);
        CHECK_THROWS(w.value(2));
    }
}

TEST_CASE("Terminal Output - dump_colored ANSI Syntax Highlighting") {
    json doc = {
        {"name", "Senko"},
        {"count", 42},
        {"ratio", 3.14},
        {"valid", true},
        {"empty", nullptr},
        {"list", {1, "two"}}
    };

    std::string colored = doc.dump_colored(2);
    CHECK(!colored.empty());
    
    // Check that ANSI color escapes exist
    CHECK(colored.find("\033[") != std::string::npos);
    // Keys highlighted in cyan (36m)
    CHECK(colored.find("\033[36m\"name\"") != std::string::npos);
    // Strings highlighted in green (32m)
    CHECK(colored.find("\033[32m\"Senko\"") != std::string::npos);
    // Numbers highlighted in yellow (33m)
    CHECK(colored.find("\033[33m42") != std::string::npos);
    // Booleans highlighted in magenta (35m)
    CHECK(colored.find("\033[35mtrue") != std::string::npos);
    // Null highlighted in gray (90m)
    CHECK(colored.find("\033[90mnull") != std::string::npos);
    // Reset escape sequences exist
    CHECK(colored.find("\033[0m") != std::string::npos);

    // Stream version matches string version
    std::ostringstream oss;
    doc.dump_colored(oss, 2);
    CHECK_EQ(oss.str(), colored);
}
