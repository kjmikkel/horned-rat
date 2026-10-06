add_library(fake_wing SHARED fake_wing/fake_wing.cpp fake_wing/fake_wing.def)
set_target_properties(fake_wing PROPERTIES OUTPUT_NAME wing32_orig)
target_compile_definitions(fake_wing PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(fake_wing PRIVATE hr_warnings)

add_library(iat_fixture SHARED iat_fixture/iat_fixture.cpp iat_fixture/iat_fixture.def)
target_compile_definitions(iat_fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(iat_fixture PRIVATE hr_warnings)

add_executable(platform_tests pe_test.cpp wing_forward_test.cpp proxy_test.cpp log_test.cpp
                              guard_test.cpp iat_hook_test.cpp modules_test.cpp dc_info_test.cpp
                              proxy_init_test.cpp window_capture_test.cpp
                              hidden_window_test.cpp window_subclass_test.cpp
                              child_input_test.cpp presenter_d3d11_test.cpp
                              presenter_window_test.cpp)
target_include_directories(platform_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(platform_tests PRIVATE hr_platform hr_warnings iat_fixture Catch2::Catch2WithMain)
add_dependencies(platform_tests fake_wing wing32)
catch_discover_tests(platform_tests ADD_TAGS_AS_LABELS)
