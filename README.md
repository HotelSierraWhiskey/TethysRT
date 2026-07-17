# TethysRT

<i>"And Tethys bore to Ocean eddying rivers..."</i> — Hesiod, Theogony

![version](https://img.shields.io/badge/Version-0.2.0-brightgreen?style=flat-square) [![License](https://img.shields.io/badge/license-Apache%202.0-blue?style=flat-square)](./LICENSE)

## About

TethysRT (Tethys Runtime) is a lightweight dynamic loader for 32-bit ARM embedded platforms. It allows firmware to load, relocate, and run ELF modules at runtime from external storage without reflashing the device or linking modules into the main application image. TethysRT loads pre-compiled modules that conform to a subset of the ELF specification. Modules can live on SD cards, flash partitions, or any block/stream-based storage the host firmware provides.

TethysRT is designed to be small, simple, and fast, and it makes very few assumptions about your embedded project.

## Getting Started

### Creating Modules

Modules are written and compiled separately from the TethysRT library, and their dependencies are largely project-specific. TethysRT works exclusively with absolute address relocations (`R_ARM_ABS32`). This keeps ELF parsing code simple, predictable, and avoids complex relocation types. As such, the compilation of modules (particularly multi-file modules) will likely require long calls enabled (`-mlong-calls` for GCC, `-mllvm --long-calls` for Clang), and/ or the explicit use of `__attribute__((long_call))` on ABI and module entry functions (on the off-chance your compiler doesn't generate absolute entries on long calls, you can call ABI functions via function pointers - this will guarantee absolute relocation entries). In short, consider compiling your modules with all/ some of the following flags:

- `-mthumb` to generate thumb instructions
- `-ffreestanding` to compile your modules without hosted C runtime assumptions
- `-fno-builtin` disable implicit libc built-ins
- `-fno-pic` to disabling position independent code
- `-mlong-calls` for absolute address relocation records

ABI functions must be defined in the host firmware and exposed to TethysRT to be used by modules. All modules must define a special entry point called `tethys_main`, which is called from the host firmware after successful loading. Modules must also be created as relocatable ELF files (that is, ELF files of type `ET_REL`. Link using `ld`'s `-r` flag. <strong>Do not</strong> compile/ link modules using `-shared`. This will create a `ET_DYN` ELF).

Given a single-file module, my_module.c, a GCC line might look like:
```
arm-none-eabi-gcc \
  -c my_module.c \
  -o my_module.o \
  -mthumb \
  -ffreestanding \
  -fno-builtin \
  -fno-pic \
  -mlong-calls
```

followed by `arm-none-eabi-ld -r my_module.o my_module.elf`.

#### Basic Module Example

The example below illustrates a minimal module that includes one external ABI function.

```c
#include "tethysrt.h"

// printf ABI function, implemented in host firmware
extern void LONG_CALL custom_printf ( const char * format, ... );

/**
 *	Example module entry point.
 *
 *	Calls ABI function and returns
 */
int32_t tethys_main ( void * context )
{
	custom_printf("Hello from Module Land!\n");
}
```

### Hosting the Loader

#### Build Requirements

TethysRT relies on a small subset of C standard headers:

- `<stdint.h>`
- `<stdbool.h>`
- `<string.h>`

It also depends on the target sysroot providing an `<elf.h>` header for ELF format definitions.

The loader is designed for ELF32 little-endian targets and assumes a flat, contiguous address model. No reliance is placed on stdio, dynamic memory allocation, or operating-system services.

#### Adding the Loader to your Project

Integrating TethysRT into an existing project depends on two user-defined callbacks:

- A <strong>Read</strong> callback, for retrieving modules from storage
- A <strong>Get Size</strong> callback, for retrieving the size of a given module

These callbacks (and an optional IO context) comprise a `tethys_io_t`, the interface used to stream module file data through the loader's parsing logic. A module object of type `tethys_module_t` containing the module's name, the address of a RAM buffer, and a size must also be defined. Upon successful loading, this object will be populated with a valid entry point which can then be called to launch the module. Module entry functions accept a generic pointer should the module require some additional context (otherwise, provide `NULL`). Although not strictly required, modules will usually make use of a core ABI, often defined in the host firmware. These ABI functions should be passed into the library during initialization in the form of an array of `tethys_symbol_t`s. Modules will have these symbols relocated upon their successful loading.

#### TethysRT API

Only two top-level functions are required for initializing the library and loading a module into memory. Besides the status codes, which are fairly straightforward, the ABI (passed to `tethys_init`) and the IO interface + module (passed to `tethys_load_module`) are the only library-specific types with which users need concern themselves. Although not strictly necessary for launching a module, `tethys_get_module_size` is useful for precisely sizing the RAM buffer provided to the loading routine, and ensuring that modules only use as much memory as they require.

```c
/**
 *	Module initialization
 *
 * 	@param[in] kp_symbols
 * 	a user-level ABI in the form of a list of symbols.
 * 	The addresses of these symbols will be used for relocation.
 * 
 * 	@param[in] u32_num_symbols
 * 	The number of symbols provided
 * 
 * 	@return
 * 	`TETHYS_STATUS_OK` upon success
 */
tethys_status_t tethys_init ( const tethys_symbol_t * kp_symbols, uint32_t u32_num_symbols );

/**
 *	Module loader
 *
 * 	@param[in] kp_io
 * 	read-only pointer to io callbacks + context
 * 
 * 	@param[in, out] p_module
 * 	contains the provided module address space and size, as well as a
 * 	pointer to the module entry point function, which will be relocated
 * 
 * 	@return
 * 	`TETHYS_STATUS_OK` upon success
 */
tethys_status_t tethys_load_module ( const tethys_io_t * kp_io, tethys_module_t * p_module );

/**
 *	Determines the amount of RAM required by a module
 *
 * 	@param[in] kp_io
 * 	read-only pointer to io callbacks + context
 * 
 * 	@param[out] pi32_size
 * 	the RAM required by the module, or -1 if something goes wrong
 * 
 * 	@return
 * 	`TETHYS_STATUS_OK` upon success
 * 	`TETHYS_STATUS_INVALID_PARAMS` if bad params are provided
 * 	`TETHYS_STATUS_READ_FAILED` on failed read
 */
tethys_status_t tethys_get_module_size ( const tethys_io_t * kp_io, int32_t * pi32_size );
```

#### Basic Loader Integration Example

```c
#include "tethysrt.h"

/**
 *	Host-side loader integration example.
 *
 *	Initializes, loads, and runs a module.
 *	Assumes that IO callbacks exist, IO context is valid, and memory has been set aside for the module.
 */
void example ( void )
{
	// initializes library with your ABI
	tethys_init(abi_symbols, num_symbols);

	tethys_io_t io =
	{
		.get_size 	= fs_get_size,	// your get_size callback
		.read		= fs_read		// your read callback
		.pv_ctx		= &file_handle	// a valid handle/ context for the callbacks
	};

	tethys_module_t module =
	{
		.kpc_name 	= "my_module.elf",		// the name of your module
		.pv_base 	= module_exec_buffer,	// a buffer, used as the module's address space
		.u32_size 	= DEFAULT_MODULE_SIZE	// buffer size
	};

	// loads the module into memory, performs relocations as necessary
	if (TETHYS_STATUS_OK == tethys_load_module(&io, &module))
	{
		module.entry(NULL);
	}
}
```

## Memory Requirements

TethysRT has different memory requirements depending on its configuration (debug level, maximum sections/symbols, IO buffer size, etc.).

### Typical Minimum Requirements

| Configuration             | Flash 	| RAM 		|
|---------------------------|-----------|-----------|
| (Debug OFF)  				| ~1.6 KB   | ~0.4 KB	|
| (Debug ON)   				| ~2.3 KB   | ~1.4 KB	|

Measurements taken using the GNU Arm Embedded Toolchain (`-Os` enabled) with `TETHYS_MAX_SECTIONS` set to 16, and `TETHYS_IO_BUFFER_SIZE` set to 64.

## Contributing

Refer to the [contributing page](./CONTRIBUTING.md)
