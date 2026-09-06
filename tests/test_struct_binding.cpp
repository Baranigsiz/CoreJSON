#include "test_framework.hpp"
#include <senko/senko.hpp>

using json = senko::json;

struct Player {
    std::string name;
    int level;
    double health;
    bool is_online;
};
SENKO_BIND(Player, name, level, health, is_online)

struct Team {
    std::string team_name;
    int rank;
};
SENKO_BIND(Team, team_name, rank)

TEST_CASE("Struct Binding - Serialization & Deserialization") {
    Player p1{"DragonSlayer", 42, 98.5, true};

    // Serialize
    json j = p1;
    CHECK(j.is_object());
    CHECK_EQ(j["name"].get<std::string>(), "DragonSlayer");
    CHECK_EQ(j["level"].get<int>(), 42);
    CHECK(std::abs(j["health"].get<double>() - 98.5) < 0.01);
    CHECK_EQ(j["is_online"].get<bool>(), true);

    // Deserialize
    Player p2 = j.get<Player>();
    CHECK_EQ(p2.name, "DragonSlayer");
    CHECK_EQ(p2.level, 42);
    CHECK(std::abs(p2.health - 98.5) < 0.01);
    CHECK_EQ(p2.is_online, true);

    // Partial JSON Deserialization
    json partial = json::parse(R"({"name": "Noob", "level": 1})");
    Player p3 = partial.get<Player>();
    CHECK_EQ(p3.name, "Noob");
    CHECK_EQ(p3.level, 1);

    // Deserializing struct from non-object throws type_error
    json not_an_obj = 123;
    CHECK_THROWS(not_an_obj.get<Player>());
    json array_json = json::array({1, 2, 3});
    CHECK_THROWS(array_json.get<Player>());
}

struct BigConfig {
    int f1, f2, f3, f4, f5, f6, f7, f8, f9, f10;
    int f11, f12, f13, f14, f15, f16, f17, f18, f19, f20;
};
SENKO_BIND(BigConfig, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20)

TEST_CASE("Struct Binding - Large Struct (20 Fields)") {
    BigConfig cfg{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20};
    json j = cfg;
    CHECK_EQ(j.size(), 20);
    CHECK_EQ(j["f1"].get<int>(), 1);
    CHECK_EQ(j["f20"].get<int>(), 20);

    BigConfig loaded = j.get<BigConfig>();
    CHECK_EQ(loaded.f1, 1);
    CHECK_EQ(loaded.f20, 20);
}

class SecretAgent {
private:
    std::string codename;
    int clearance_level;
    SENKO_BIND_INTRUSIVE(SecretAgent, codename, clearance_level)

public:
    SecretAgent() = default;
    SecretAgent(std::string name, int clearance)
        : codename(std::move(name)), clearance_level(clearance) {}

    const std::string& get_codename() const { return codename; }
    int get_clearance() const { return clearance_level; }
};

TEST_CASE("Struct Binding - Intrusive Private Fields (SENKO_BIND_INTRUSIVE)") {
    SecretAgent agent{"007", 9};
    json j = agent;
    CHECK(j.is_object());
    CHECK_EQ(j["codename"].get<std::string>(), "007");
    CHECK_EQ(j["clearance_level"].get<int>(), 9);

    SecretAgent restored = j.get<SecretAgent>();
    CHECK_EQ(restored.get_codename(), "007");
    CHECK_EQ(restored.get_clearance(), 9);
}

namespace geometry {
struct Vector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline void to_json(senko::json& j, const Vector3& v) {
    j = senko::json::array({v.x, v.y, v.z});
}

inline void from_json(const senko::json& j, Vector3& v) {
    if (!j.is_array() || j.size() < 3) {
        throw senko::type_error("Expected 3-element array for Vector3");
    }
    v.x = j[0].get<double>();
    v.y = j[1].get<double>();
    v.z = j[2].get<double>();
}
} // namespace geometry

TEST_CASE("Struct Binding - Non-Intrusive ADL to_json / from_json") {
    geometry::Vector3 vec{1.5, 2.5, 3.5};
    json j = vec;
    CHECK(j.is_array());
    CHECK_EQ(j.size(), 3);
    CHECK_EQ(j[0].get<double>(), 1.5);
    CHECK_EQ(j[1].get<double>(), 2.5);
    CHECK_EQ(j[2].get<double>(), 3.5);

    geometry::Vector3 restored = j.get<geometry::Vector3>();
    CHECK_EQ(restored.x, 1.5);
    CHECK_EQ(restored.y, 2.5);
    CHECK_EQ(restored.z, 3.5);
}


