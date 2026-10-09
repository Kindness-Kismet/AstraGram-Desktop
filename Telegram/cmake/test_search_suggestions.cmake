add_executable(test_search_suggestions)
init_target(test_search_suggestions "(tests)")

target_include_directories(test_search_suggestions PRIVATE ${src_loc})
nice_target_sources(test_search_suggestions ${src_loc}
PRIVATE
    storage/details/storage_search_suggestions.cpp
    storage/details/storage_search_suggestions.h
    tests/test_search_suggestions.cpp
)
target_link_libraries(test_search_suggestions PRIVATE desktop-app::external_qt)
set_target_properties(test_search_suggestions PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
