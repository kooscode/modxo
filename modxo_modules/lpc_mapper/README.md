# LPC Mapper

### What is this?
LPC Mapper is a Modxo module that gives the xbox the ability to read or write data from the otherwise normal BIOS ROM memory range (0xFF000000-0xFFFFFFFF, 16MiB) using one of several "handlers". It allows opening upto 4 variable-sized, non-overlapping "windows" (sometimes called "memory overlays") within the BIOS ROM range and assigning it to one of the built-in handlers (see below). Reading or writing to any memory address within the window, while it's enabled, will be redirected to the handler instead. The handler decides what data to return for reads and what to do with the data for writes.

Configuring the window is done via a handful of IO-port addresses. Handlers can have optional, handler specific, options or commands, as well. There is a single arbitrary 32-bit "Long Register" that is shared between the handlers and the core LPC Mapper module itself, used for anything that needs a 32-bit value, like assigning the base memory address and size for a window, for example. Handlers can use it whenever they need a 32-bit value.

Use caution when using some of the handlers as leaving the window open (enabled) and forgetting to close (disable) it may cause unexpected behavior since the xbox kernel or xbox apps might try to read from the BIOS ROM range expecting to see BIOS ROM data but instead sees handler data. This only applies if the read is within an enabled window's address range. Typically though, the only time the xbox kernel or apps try to access the BIOS ROM range is during boot-up or when launching an XBE or when the kernel switches video modes.

## Handlers

### echo.c
Simple single-byte read/write handler. Writing an arbitrary byte of data to any address within the window will be the same when read back.
- This handler has no options.

### expansion.c
** TODO **

### flash.c
A handler to send or receive arbitrary commands and data to the flash device connected the RP2040/RP2350's QSPI bus. Internally, this just uses Pico SDK's flash_do_cmd() function to send & receive commands/data.
- Read Max size: Sets the Long Register to the maximum size of the data buffer that can be used. Read the Long Register to retrieve the value.
- Send Buffer: This is a command to do the actual send to flash device. Read the window range afterwards to retrieve the results.

### heap.c
This will carve out a small chunk of contiguous free memory from the RP2040/RP2350's SRAM memory matching the window's size. Can be used for any purpose while the window is enabled. The allocated memory will be uninitialized.
- Read Max size: Sets the Long Register to the maximum allowed size that can be allocated. Read the Long Register to retrieve the value. This value doesn't indicate the amount of memory available, it's just a static value that the handler will enforce.

### int_ram.c
A simple handler that maps the window range to a configurable offset inside the RP2040/RP2350's address range. Use this handler with caution as reading or writing to certain addresses can cause unexpected behavior to the RP2040/RP2350.
- Read/Set offset: Read or Set the Long Register to the offset of the RP2040/RP2350's address range that will be mapped to the window. Set the Long Register first before calling this command.

### psram.c
Gives access to a PSRAM chip connected to the RP2350's QSPI bus (via CSn1 GPIO).
- Read/Set offset: Read or Set the Long Register to an offset within PSRAM's address range that will be mapped to the window. Set the Long Register first before calling this command.
- PSRAM Initialized: Call this command to initialize the PSRAM chip. This only needs to be done once per Modxo boot. A read from this command will return whehter or not the PSRAM has been initalized.
- Get ID: Returns the PSRAM's device ID.
- Get Size: Returns the PSRAM's total capacity, in MiB. For example, a returned value of 8 means the PSRAM has an 8MiB capacity.

### rng.c
A simple Random Number Generator. Reading from any address within the window's range will return random data. A single byte read from any 8-byte aligned address (within the window) will regenrate the internal 64-bit random value. Internally, this handler just uses the Pico SDK's get_rand_64() function to generate and store a 64-bit random value.
- Reset value: This command can be called to regenerate the 64-bit value, but is not normally needed as it gets regenerated automatically for 8-byte aligned memory reads.

### sdcard.c (and sdconfig.c)
Gives access to features and data of a SDcard connected to one of the RP2040/RP2350's peripheral SPI interface. This will do nothing if the SDcard feature isn't compiled in to the Modxo build you are using. Features include: mounting/unmounting the filesystem, reading volume capacity, browsing files, reading/writing file data, rename/delete files and folders, reading file/folder metadata (size, dates, attributes) and finally booting a xbox BIOS .bin file directly.
- There are several commands and options for this handler, most of which deal with the above mentioned features. Bulk reading/writing of data, for some of the features, is done through the window range.
