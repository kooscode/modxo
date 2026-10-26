# oxos_debug: a boot-debugging rig for the Xbox

A Modxo module that turns the modchip into a boot-debugging rig for the Xbox. It was written for OXOS (an
open-source Xbox kernel) but works with any BIOS. Over one USB serial port it gives the PC:
- **POST codes:** every write to I/O port 0x80, as it happens;
- **COM1:** the emulated UART's text (stock Modxo);
- **status lines:** LPC power/reset pins, the LPC cycle counts and last addresses, the first memory reads after
  power-up, and memory cycles outside the flash window;
- **a power button:** the PC can press the console's power button.

The host program is OXOS's `tools/modxo/console.py`; its README covers usage.

## Wiring (power button)
GPIO `OXOS_POWER_PIN` (default 28) → 1 kΩ → base of an NPN transistor (e.g. 2N2222). Emitter to Xbox ground,
collector to the power switch's signal side, so the transistor closes the switch while the pin is high. A press lasts
2 s.

On the RP2040-Tiny, GPIO 28 is the SERIRQ pin, so `src/main.c` does not register the SERIRQ module in this branch.
Pick another pin (`-DOXOS_POWER_PIN=n`) and register SERIRQ again if your board needs it.

## Stream format (Pico → PC)
| Bytes | Meaning |
|---|---|
| `0xFE c` | POST code `c` |
| `0xFC text 0x0A` | a status line (see the top of `oxos_debug.c`) |
| anything else | COM1 output |

PC → Pico: `0xFD 'P'` = press the power button; other bytes go to the emulated UART.

As with stock Modxo, the UART and Super I/O answer only while the PC holds DTR on the USB serial port.
