add_executable(gamedata_tests gamedata_test.cpp)
target_link_libraries(gamedata_tests PRIVATE hr_platform hr_warnings Catch2::Catch2WithMain)
add_dependencies(gamedata_tests wing32)
# Catch2 exits with 4 when every test was skipped.
catch_discover_tests(gamedata_tests ADD_TAGS_AS_LABELS PROPERTIES SKIP_RETURN_CODE 4)
