add_executable(core_tests ini_test.cpp config_test.cpp log_format_test.cpp sample_limiter_test.cpp
                          game_modules_test.cpp viewport_test.cpp input_map_test.cpp content_rect_test.cpp
                          luma_test.cpp audio_test.cpp scaling_test.cpp display_mode_test.cpp
                          message_census_test.cpp cursor_math_test.cpp cursor_bitmap_test.cpp)
target_link_libraries(core_tests PRIVATE hr_core hr_warnings Catch2::Catch2WithMain)
catch_discover_tests(core_tests ADD_TAGS_AS_LABELS)
