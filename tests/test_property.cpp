#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/mvvm/Property.h"

using tw::app::Property;

TEST_CASE("Property::Set notifies subscribers with new value") {
    Property<int> prop(0);
    int observed = -1;
    prop.Subscribe([&](const int& v) { observed = v; });

    prop.Set(42);

    REQUIRE(observed == 42);
    REQUIRE(prop.Get() == 42);
}

TEST_CASE("Property::Unsubscribe stops further notifications") {
    Property<int> prop(0);
    int callCount = 0;
    int id = prop.Subscribe([&](const int&) { ++callCount; });

    prop.Set(1);
    prop.Unsubscribe(id);
    prop.Set(2);

    REQUIRE(callCount == 1);
}

TEST_CASE("Property supports multiple independent subscribers") {
    Property<std::string> prop("init");
    std::vector<std::string> a, b;
    prop.Subscribe([&](const std::string& v) { a.push_back(v); });
    prop.Subscribe([&](const std::string& v) { b.push_back(v); });

    prop.Set("x");

    REQUIRE(a == std::vector<std::string>{"x"});
    REQUIRE(b == std::vector<std::string>{"x"});
}
