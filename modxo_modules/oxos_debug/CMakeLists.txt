add_library(oxos_debug STATIC ${CMAKE_CURRENT_LIST_DIR}/oxos_debug.c)

pico_enable_stdio_usb(oxos_debug 1)
pico_enable_stdio_uart(oxos_debug 0)

target_include_directories(oxos_debug PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(oxos_debug PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(oxos_debug PUBLIC modxo)
