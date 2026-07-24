find_package(Catch2 3 CONFIG REQUIRED)

add_executable(tracewell-tests
    test_diff.cpp
    test_storage.cpp
    test_property.cpp
)
target_link_libraries(tracewell-tests PRIVATE tracewell-core Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(tracewell-tests)
