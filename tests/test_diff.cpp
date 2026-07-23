#include <catch2/catch_test_macros.hpp>

#include "core/diff.h"

using tw::diff_payloads;
using tw::json;

TEST_CASE("diff of identical payloads is empty") {
    json payload = {{"a", {{"x", 1}}}, {"b", {{"y", 2}}}};
    REQUIRE(diff_payloads(payload, payload).empty());
}

TEST_CASE("diff detects added, removed and changed entities") {
    json before = {{"keep", {{"v", 1}}}, {"gone", {{"v", 2}}}, {"mod", {{"v", 3}}}};
    json after = {{"keep", {{"v", 1}}}, {"mod", {{"v", 99}}}, {"fresh", {{"v", 4}}}};

    auto diff = diff_payloads(before, after);
    REQUIRE(diff.added == std::vector<std::string>{"fresh"});
    REQUIRE(diff.removed == std::vector<std::string>{"gone"});
    REQUIRE(diff.changed.size() == 1);
    CHECK(diff.changed[0].key == "mod");
    CHECK(diff.changed[0].before == json{{"v", 3}});
    CHECK(diff.changed[0].after == json{{"v", 99}});
}

TEST_CASE("diff of non-objects is empty") {
    CHECK(diff_payloads(json::array(), json::object()).empty());
    CHECK(diff_payloads(json(nullptr), json(nullptr)).empty());
}
