#include "test_framework.hpp"
#include <senko/senko.hpp>

using json = senko::json;

TEST_CASE("JSONPath - Dot & Bracket Child Access") {
    json doc = json::parse(R"({
        "store": {
            "name": "Senko Bookshop",
            "city": "Tokyo"
        }
    })");

    auto name_res = doc.jsonpath("$.store.name");
    CHECK_EQ(name_res.size(), 1);
    CHECK_EQ(name_res[0].get<std::string>(), "Senko Bookshop");

    auto city_res = doc.jsonpath("$['store']['city']");
    CHECK_EQ(city_res.size(), 1);
    CHECK_EQ(city_res[0].get<std::string>(), "Tokyo");

    CHECK_EQ(doc.jsonpath_first("$.store.name").get<std::string>(), "Senko Bookshop");
}

TEST_CASE("JSONPath - Array Wildcards & Slices") {
    json doc = json::parse(R"({
        "books": [
            {"title": "Book A", "price": 10},
            {"title": "Book B", "price": 20},
            {"title": "Book C", "price": 30}
        ]
    })");

    // All titles via wildcard
    auto titles = doc.jsonpath("$.books[*].title");
    CHECK_EQ(titles.size(), 3);
    CHECK_EQ(titles[0].get<std::string>(), "Book A");
    CHECK_EQ(titles[1].get<std::string>(), "Book B");
    CHECK_EQ(titles[2].get<std::string>(), "Book C");

    // Index access
    auto first_book = doc.jsonpath("$.books[0].title");
    CHECK_EQ(first_book.size(), 1);
    CHECK_EQ(first_book[0].get<std::string>(), "Book A");

    // Negative index
    auto last_book = doc.jsonpath("$.books[-1].title");
    CHECK_EQ(last_book.size(), 1);
    CHECK_EQ(last_book[0].get<std::string>(), "Book C");
}

TEST_CASE("JSONPath - Recursive Descent (..)") {
    json doc = json::parse(R"({
        "level1": {
            "author": "Author 1",
            "level2": {
                "author": "Author 2",
                "level3": {
                    "author": "Author 3"
                }
            }
        }
    })");

    auto authors = doc.jsonpath("$..author");
    CHECK_EQ(authors.size(), 3);
    CHECK_EQ(authors[0].get<std::string>(), "Author 1");
    CHECK_EQ(authors[1].get<std::string>(), "Author 2");
    CHECK_EQ(authors[2].get<std::string>(), "Author 3");
}

TEST_CASE("JSONPath - Filter Expressions [?(@.field op val)]") {
    json doc = json::parse(R"({
        "products": [
            {"name": "Keyboard", "price": 45.0, "in_stock": true},
            {"name": "Mouse", "price": 15.0, "in_stock": true},
            {"name": "Monitor", "price": 150.0, "in_stock": false},
            {"name": "Pad", "price": 8.0, "in_stock": true}
        ]
    })");

    // Price < 20
    auto cheap = doc.jsonpath("$.products[?(@.price < 20)].name");
    CHECK_EQ(cheap.size(), 2);
    CHECK_EQ(cheap[0].get<std::string>(), "Mouse");
    CHECK_EQ(cheap[1].get<std::string>(), "Pad");

    // In stock == true
    auto in_stock = doc.jsonpath("$.products[?(@.in_stock == true)].name");
    CHECK_EQ(in_stock.size(), 3);

    // Exact string match
    auto keyboard = doc.jsonpath("$.products[?(@.name == 'Keyboard')].price");
    CHECK_EQ(keyboard.size(), 1);
    CHECK_EQ(keyboard[0].get<double>(), 45.0);

    // Invalid unquoted literal that cannot be parsed as number or bool throws jsonpath_error
    CHECK_THROWS(doc.jsonpath("$.products[?(@.price < not_a_number)]"));
}

TEST_CASE("JSONPath - Array Slices [start:end:step]") {
    json doc = json::parse(R"({
        "numbers": [10, 20, 30, 40, 50, 60]
    })");

    // Slice [0:3]
    auto slice1 = doc.jsonpath("$.numbers[0:3]");
    CHECK_EQ(slice1.size(), 3);
    CHECK_EQ(slice1[0].get<int>(), 10);
    CHECK_EQ(slice1[1].get<int>(), 20);
    CHECK_EQ(slice1[2].get<int>(), 30);

    // Slice [2:]
    auto slice2 = doc.jsonpath("$.numbers[2:]");
    CHECK_EQ(slice2.size(), 4);
    CHECK_EQ(slice2[0].get<int>(), 30);
    CHECK_EQ(slice2[3].get<int>(), 60);

    // Slice [:2]
    auto slice3 = doc.jsonpath("$.numbers[:2]");
    CHECK_EQ(slice3.size(), 2);
    CHECK_EQ(slice3[0].get<int>(), 10);
    CHECK_EQ(slice3[1].get<int>(), 20);

    // Negative slice [-2:]
    auto slice4 = doc.jsonpath("$.numbers[-2:]");
    CHECK_EQ(slice4.size(), 2);
    CHECK_EQ(slice4[0].get<int>(), 50);
    CHECK_EQ(slice4[1].get<int>(), 60);

    // Slice with step [::2]
    auto slice5 = doc.jsonpath("$.numbers[::2]");
    CHECK_EQ(slice5.size(), 3);
    CHECK_EQ(slice5[0].get<int>(), 10);
    CHECK_EQ(slice5[1].get<int>(), 30);
    CHECK_EQ(slice5[2].get<int>(), 50);

    // Reverse slice [::-1]
    auto slice6 = doc.jsonpath("$.numbers[::-1]");
    CHECK_EQ(slice6.size(), 6);
    CHECK_EQ(slice6[0].get<int>(), 60);
    CHECK_EQ(slice6[5].get<int>(), 10);
}

TEST_CASE("JSONPath - Recursive Wildcard ($..*)") {
    json doc = json::parse(R"({
        "a": 1,
        "b": {
            "c": 2,
            "d": [3, 4]
        }
    })");

    auto all = doc.jsonpath("$..*");
    // Should recursively find all values: 1, {c:2, d:[3,4]}, 2, [3,4], 3, 4
    CHECK(all.size() >= 5);
}

TEST_CASE("JSONPath - Zero-Copy Pointer Queries (jsonpath_refs & jsonpath_first_ref)") {
    const json doc = json::parse(R"({
        "store": {
            "name": "Senko Bookshop",
            "city": "Tokyo"
        },
        "books": [
            {"title": "Book A", "price": 10},
            {"title": "Book B", "price": 20},
            {"title": "Book C", "price": 30}
        ]
    })");

    // jsonpath_refs on const json returns std::vector<const value*>
    auto book_refs = doc.jsonpath_refs("$.books[*].title");
    CHECK_EQ(book_refs.size(), 3);
    CHECK(book_refs[0] != nullptr);
    CHECK(book_refs[1] != nullptr);
    CHECK(book_refs[2] != nullptr);

    CHECK_EQ(book_refs[0]->get<std::string>(), "Book A");
    CHECK_EQ(book_refs[1]->get<std::string>(), "Book B");
    CHECK_EQ(book_refs[2]->get<std::string>(), "Book C");

    // Pointer address identity: verify it points directly inside the document without copying
    CHECK_EQ(book_refs[0], &(doc.at("books").at(0).at("title")));
    CHECK_EQ(book_refs[1], &(doc.at("books").at(1).at("title")));
    CHECK_EQ(book_refs[2], &(doc.at("books").at(2).at("title")));

    // jsonpath_first_ref
    const auto* first_ref = doc.jsonpath_first_ref("$.store.name");
    CHECK(first_ref != nullptr);
    CHECK_EQ(first_ref->get<std::string>(), "Senko Bookshop");
    CHECK_EQ(first_ref, &(doc.at("store").at("name")));

    // Non-existent path returns nullptr
    const auto* null_ref = doc.jsonpath_first_ref("$.store.non_existent");
    CHECK(null_ref == nullptr);
}

TEST_CASE("JSONPath - In-Place Mutation via jsonpath_refs & jsonpath_first_ref") {
    json doc = json::parse(R"({
        "store": {
            "name": "Senko Bookshop",
            "city": "Tokyo"
        },
        "products": [
            {"name": "Mouse", "price": 15.0},
            {"name": "Pad", "price": 8.0},
            {"name": "Keyboard", "price": 45.0}
        ]
    })");

    // Mutate via jsonpath_first_ref
    auto* store_name = doc.jsonpath_first_ref("$.store.name");
    CHECK(store_name != nullptr);
    if (store_name) *store_name = "Senko Megastore";
    CHECK_EQ(doc["store"]["name"].get<std::string>(), "Senko Megastore");

    // In-place double price of products with price < 20 via jsonpath_refs
    auto cheap_products = doc.jsonpath_refs("$.products[?(@.price < 20)].price");
    CHECK_EQ(cheap_products.size(), 2);
    for (auto* p_price : cheap_products) {
        CHECK(p_price != nullptr);
        if (p_price) *p_price = p_price->get<double>() * 2.0;
    }

    // Verify changes persisted in the original document
    CHECK_EQ(doc["products"][0]["price"].get<double>(), 30.0);
    CHECK_EQ(doc["products"][1]["price"].get<double>(), 16.0);
    CHECK_EQ(doc["products"][2]["price"].get<double>(), 45.0);
}


