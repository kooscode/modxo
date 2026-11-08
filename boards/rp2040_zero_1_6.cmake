# RP2040-Zero / Tiny on a 1.6 console (oxos-debug): the rp2040_zero_tiny pinout (D0 on GPIO 6), with GPIO 9, which
# nothing else uses, wired straight to the power switch line for the oxos_debug power button: high, pulled low to press.
include(${CMAKE_CURRENT_LIST_DIR}/rp2040_zero_tiny.cmake)
add_compile_definitions(OXOS_POWER_PIN=9 OXOS_POWER_ACTIVE_LOW=1)
