#include "test_framework.hpp"
#include <senko/senko.hpp>

#include <optional>
#include <map>
#include <unordered_map>
#include <vector>
#include <set>
#include <deque>
#include <list>

using json = senko::json;

struct Profile {
    std::string username;
    std::optional<std::string> email;
    std::optional<int> age;
    std::vector<std::string> roles;
    std::map<std::string, int> scores;
};
SENKO_BIND(Profile, username, email, age, roles, scores)

TEST_CASE("STL - std::optional Serialization & Deserialization") {
    Profile p1;
    p1.username = "Baran";
    p1.email = "baran@example.com";
    p1.age = std::nullopt;
    p1.roles = {"admin", "developer"};
    p1.scores = {{"cpp", 100}, {"perf", 95}};

    json j = p1;
    CHECK(j.is_object());
    CHECK_EQ(j["username"].get<std::string>(), "Baran");
    CHECK_EQ(j["email"].get<std::string>(), "baran@example.com");
    CHECK(j["age"].is_null());
    CHECK_EQ(j["roles"].size(), 2);
    CHECK_EQ(j["scores"]["cpp"].get<int>(), 100);

    // Deserialization with engaged and disengaged optional
    Profile restored = j.get<Profile>();
    CHECK_EQ(restored.username, "Baran");
    CHECK(restored.email.has_value());
    CHECK_EQ(*restored.email, "baran@example.com");
    CHECK(!restored.age.has_value());
    CHECK_EQ(restored.roles.size(), 2);
    CHECK_EQ(restored.scores["cpp"], 100);
}

TEST_CASE("STL - Direct std::map and std::unordered_map") {
    std::map<std::string, double> exchange_rates = {
        {"USD", 1.0},
        {"EUR", 0.92},
        {"JPY", 155.4}
    };

    json j = exchange_rates;
    CHECK(j.is_object());
    CHECK_EQ(j["USD"].get<double>(), 1.0);
    CHECK_EQ(j["EUR"].get<double>(), 0.92);

    auto restored_map = j.get<std::map<std::string, double>>();
    CHECK_EQ(restored_map.size(), 3);
    CHECK_EQ(restored_map["JPY"], 155.4);

    std::unordered_map<std::string, std::string> headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer token123"}
    };
    json j_headers = headers;
    CHECK(j_headers.is_object());
    auto restored_headers = j_headers.get<std::unordered_map<std::string, std::string>>();
    CHECK_EQ(restored_headers["Content-Type"], "application/json");
}

TEST_CASE("STL - std::vector, std::set, std::deque, std::list") {
    std::vector<int> nums = {10, 20, 30, 40};
    json j_vec = nums;
    CHECK(j_vec.is_array());
    CHECK_EQ(j_vec.size(), 4);
    auto restored_vec = j_vec.get<std::vector<int>>();
    CHECK(restored_vec == nums);

    std::set<std::string> tags = {"c++", "json", "fast"};
    json j_set = tags;
    CHECK(j_set.is_array());
    auto restored_set = j_set.get<std::set<std::string>>();
    CHECK_EQ(restored_set.size(), 3);
    CHECK(restored_set.count("fast") > 0);

    std::deque<int> deq = {1, 2, 3};
    json j_deq = deq;
    CHECK(j_deq.get<std::deque<int>>() == deq);

    std::list<std::string> lst = {"alpha", "beta"};
    json j_lst = lst;
    CHECK(j_lst.get<std::list<std::string>>() == lst);
}

TEST_CASE("STL - std::pair Serialization") {
    std::pair<std::string, int> item = {"score", 99};
    json j = item;
    CHECK(j.is_array());
    CHECK_EQ(j.size(), 2);
    CHECK_EQ(j[0].get<std::string>(), "score");
    CHECK_EQ(j[1].get<int>(), 99);

    auto restored_pair = j.get<std::pair<std::string, int>>();
    CHECK_EQ(restored_pair.first, "score");
    CHECK_EQ(restored_pair.second, 99);
}

TEST_CASE("STL - std::hash & std::unordered_set / unordered_map") {
    std::unordered_set<json> json_set;
    json_set.insert(10);
    json_set.insert("hello");
    json_set.insert(json{{"key", "val"}});

    CHECK_EQ(json_set.size(), 3);
    CHECK(json_set.count(10) > 0);
    CHECK(json_set.count("hello") > 0);
    CHECK(json_set.count(json{{"key", "val"}}) > 0);
    CHECK(json_set.count(20) == 0);

    std::unordered_map<json, std::string> json_map;
    json key1 = json{{"id", 1}};
    json_map[key1] = "User One";
    CHECK_EQ(json_map[key1], "User One");

    // Test that objects with different key orders produce identical hash and find in unordered_set
    json obj_a = json::object({{"alpha", 1}, {"beta", 2}});
    json obj_b = json::object({{"beta", 2}, {"alpha", 1}});
    CHECK(obj_a == obj_b);
    CHECK_EQ(obj_a.hash(), obj_b.hash());
    std::unordered_set<json> order_set;
    order_set.insert(obj_a);
    CHECK_EQ(order_set.count(obj_b), 1);
}


