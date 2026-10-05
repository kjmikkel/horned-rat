add_executable(core_tests ini_test.cpp)
target_link_libraries(core_tests PRIVATE hr_core hr_warnings Catch2::Catch2WithMain)
catch_discover_tests(core_tests ADD_TAGS_AS_LABELS)
