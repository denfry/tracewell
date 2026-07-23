find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(tracewell-core STATIC
    core/snapshot.cpp
    core/diff.cpp
    core/storage.cpp
    core/collectors/startup_registry.cpp
    core/collectors/services.cpp
    core/collectors/disk_pdh.cpp
)
target_include_directories(tracewell-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tracewell-core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE unofficial::sqlite3::sqlite3 pdh advapi32 shell32 ole32
)

add_executable(tracewell-cli cli/main.cpp)
target_link_libraries(tracewell-cli PRIVATE tracewell-core)
