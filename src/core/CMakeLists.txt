add_library(hr_core STATIC ini.cpp config.cpp)
target_include_directories(hr_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hr_core PRIVATE hr_warnings)
