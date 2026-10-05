add_library(hr_platform STATIC pe.cpp)
target_link_libraries(hr_platform PUBLIC hr_core PRIVATE hr_warnings)
target_compile_definitions(hr_platform PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
