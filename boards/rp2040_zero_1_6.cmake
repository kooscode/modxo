# RP2040-Zero / Tiny on a 1.6 console (oxos-debug): the rp2040_zero_tiny pinout, with GPIO 6 (D0, which a 1.6 does
# not use) wired straight to the power switch line for the oxos_debug power button: high, pulled low to press.
# Modxo's D0 handling moves to GPIO 29, which nothing uses on this board.
include(${CMAKE_CURRENT_LIST_DIR}/rp2040_zero_tiny.cmake)
set(GPIO_D0 29)
add_compile_definitions(OXOS_POWER_PIN=6 OXOS_POWER_ACTIVE_LOW=1)
