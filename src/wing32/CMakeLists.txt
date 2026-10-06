add_library(wing32 SHARED dllmain.cpp exports.cpp runtime.cpp hooks.cpp a2.cpp wing32.def)
set_target_properties(wing32 PROPERTIES PREFIX "" OUTPUT_NAME wing32)
target_link_libraries(wing32 PRIVATE hr_platform hr_warnings)
target_compile_definitions(wing32 PRIVATE HR_VERSION="${PROJECT_VERSION}")
