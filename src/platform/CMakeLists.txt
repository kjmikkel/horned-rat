add_library(hr_platform STATIC pe.cpp wing_forward.cpp log.cpp iat_hook.cpp modules.cpp dc_info.cpp
                               window_capture.cpp hidden_window.cpp
                               window_subclass.cpp child_input.cpp
                               presenter_d3d11.cpp)
target_link_libraries(hr_platform PUBLIC hr_core d3d11 dxgi PRIVATE hr_warnings)
target_compile_definitions(hr_platform PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
