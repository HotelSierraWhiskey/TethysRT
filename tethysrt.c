// SPDX-License-Identifier: Apache-2.0

#include <elf.h>
#include <stdbool.h>
#include <string.h>
#include "tethysrt.h"
#include "tethysrt_config.h"

/****************************************************************************************************
 *	D E F I N E S   &   T Y P E D E F S
 ****************************************************************************************************/

/**
 *	Do-nothing utility
 */
#define TETHYS_PASS	\
	do				\
	{				\
	/* pass */		\
	}				\
	while (0)

/**
 *	If the user didn't define a log level
 *	make the default zero 
 */
#ifndef TETHYS_DEBUG_LEVEL
	#define TETHYS_DEBUG_LEVEL 0
#endif

/**
 *	Debug log level
 */
#if TETHYS_DEBUG_LEVEL >= 1
	#define TETHYS_DEBUG(...)				TETHYS_PRINTF(__VA_ARGS__)
#else
	#define TETHYS_DEBUG(...)				TETHYS_PASS
#endif

/**
 *	Verbose log level
 */
#if TETHYS_DEBUG_LEVEL >= 2
	#define TETHYS_VERBOSE(...)				TETHYS_PRINTF(__VA_ARGS__)
#else
	#define TETHYS_VERBOSE(...)				TETHYS_PASS
#endif

/**
 *	Macro for debug compilation
 */
#if TETHYS_DEBUG_LEVEL > 0
	#define TETHYS_DEBUG_ENABLED 1
#else
	#define TETHYS_DEBUG_ENABLED 0
#endif

/**
 *	64-bit host check
 */
#if (UINTPTR_MAX > 0xFFFFFFFFU)
    #define TETHYS_HOST_64BIT 1
#else
    #define TETHYS_HOST_64BIT 0
#endif

/**
 *	GCC 64-bit suppressions
 */
#if TETHYS_HOST_64BIT && defined(__GNUC__)
	#define TETHYS_DIAG_PUSH()				_Pragma("GCC diagnostic push")
	#define TETHYS_DIAG_POP()				_Pragma("GCC diagnostic pop")
	#define TETHYS_DIAG_IGNORED_PTR2INT()	_Pragma("GCC diagnostic ignored \"-Wpointer-to-int-cast\"")
	#define TETHYS_DIAG_IGNORED_INT2PTR()	_Pragma("GCC diagnostic ignored \"-Wint-to-pointer-cast\"")
/**
 *	Clang 64-bit suppressions
 */
#elif TETHYS_HOST_64BIT && defined(__clang__)
	#define TETHYS_DIAG_PUSH()				_Pragma("clang diagnostic push")
	#define TETHYS_DIAG_POP()				_Pragma("clang diagnostic pop")
	#define TETHYS_DIAG_IGNORED_PTR2INT()	_Pragma("clang diagnostic ignored \"-Wpointer-to-int-cast\"")
	#define TETHYS_DIAG_IGNORED_INT2PTR()	_Pragma("clang diagnostic ignored \"-Wint-to-pointer-cast\"")
/**
 *	Suppressions stubs
 */
#else
	#define TETHYS_DIAG_PUSH()
	#define TETHYS_DIAG_POP()
	#define TETHYS_DIAG_IGNORED_PTR2INT()
	#define TETHYS_DIAG_IGNORED_INT2PTR()
#endif

/**
 *	0x7F 'E' 'L' 'F' as a little-endian 32 bit 
 */
#define TETHYS_ELF_MAGIC_LE32 				(0x464C457FU)

/**
 *	Size of 32-bit ELF header 
 */
#define TETHYS_ELF32_SECTION_HEADER_SIZE	(40U)

/**
 *	Max length of a section name
 */
#define TETHYS_SECTION_NAME_MAX				(64U)

/**
 *	Value of NOLOAD section addresses
 */
#define TETHYS_PMAS_SECTION_NOLOAD			(0xFFFFFFFF)

/**
 *	Maximum number of relocatable sections
 */
#define TETHYS_PMAS_MAX_REL_SECTIONS		TETHYS_MAX_SECTIONS

/**
 *	Name of module entry symbol
 */
#define TETHYS_ENTRY_STRING					"tethys_main"

/**
 *	ELF header typedef (support 32-bit only)
 */
typedef Elf32_Ehdr tethys_elf32_header_t;

/**
 *	ELF section header typedef (support 32-bit only)
 */
typedef Elf32_Shdr tethys_elf32_section_header_t;

/**
 *	ELF symbol typedef (support 32-bit only)
 */
typedef Elf32_Sym tethys_elf32_symbol_t;

/**
 *	ELF relocation table entry typedef (support 32-bit only)
 */
typedef Elf32_Rel tethys_elf32_relocation_entry_t;

/**
 *	Section types for TethysRT
 */
typedef enum _tethys_section_type
{
	TETHYS_SECTION_TYPE_NOLOAD = 0,
	TETHYS_SECTION_TYPE_CODE,
	TETHYS_SECTION_TYPE_DATA,
	TETHYS_SECTION_TYPE_BSS
} tethys_section_type_t;

/**
 *	Section header info
 *
 * 	Type determines if/ how to to load, size and offset for positioning.
 * 	Name for debug purposes only
 */
typedef struct _tethys_section_header_entry
{
	tethys_section_type_t			type;								// section type
	uint32_t						u32_size;							// size in bytes
	uint32_t						u32_offset;							// offset from module memory base address
#if TETHYS_DEBUG_ENABLED
	char 							pc_name[TETHYS_SECTION_NAME_MAX];	// section name
#endif // TETHYS_DEBUG_ENABLED
} tethys_section_header_entry_t;

/**
 *	Private Module Address Space (PMAS)
 *
 * 	Contains variables used specifically during the ELF parsing process
 */
typedef struct _tethys_pmas
{
	tethys_section_header_entry_t 	p_section_header_data[TETHYS_MAX_SECTIONS];

	uint16_t 						u16_symtab_index;								// index of .symtab (or 0xFFFF if none)
	uint16_t 						u16_strtab_index;								// index of the associated .strtab

	uint32_t 						u32_num_section_headers;						// the number of section headers in the ELF

	uint32_t 						u32_rel_indices[TETHYS_PMAS_MAX_REL_SECTIONS];	// array of reloc section indices 
	uint16_t 						u16_num_rel_indices;							// number of reloc sections found
} tethys_pmas_t;

/**
 *	Main info struct
 */
typedef struct _tethys_info
{
	tethys_symbol_t *				p_symbols;								// pointer to ABI
	uint32_t						u32_num_symbols;						// size of ABI
	tethys_pmas_t					pmas;									// Private Module Address Space
	uint8_t							tethys_io_buf[TETHYS_IO_BUFFER_SIZE];	// IO buffer for read callback
	tethys_elf32_section_header_t	section_header;							// section header for cacheing
} tethys_info_t;

/****************************************************************************************************
 *	P R I V A T E   F U N C T I O N   P R O T O T Y P E S
 ****************************************************************************************************/

static tethys_status_t	tethys_validate_header		( const tethys_elf32_header_t * kp_header, uint32_t u32_image_size );
static tethys_status_t	tethys_write_module			( const tethys_elf32_header_t * kp_header, const tethys_io_t * kp_io, tethys_module_t * p_module );
static tethys_status_t	tethys_apply_relocations	( const tethys_elf32_header_t * kp_header, const tethys_io_t * kp_io, tethys_module_t * p_module );
static tethys_status_t	tethys_apply_rel_sections	( const tethys_elf32_header_t * kp_header, const tethys_io_t * kp_io, tethys_module_t * p_module, uint32_t u32_symtab_off, uint32_t u32_symtab_entsize, uint32_t u32_strtab_off );

/****************************************************************************************************
 *	P R I V A T E   V A R I A B L E S
 ****************************************************************************************************/

static tethys_info_t info;

/****************************************************************************************************
 *	F U N C T I O N S
 ****************************************************************************************************/

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
 * 	`TETHYS_STATUS_INVALID_PARAMS` if ABI exceeds max size
 */
tethys_status_t tethys_init ( tethys_symbol_t * p_symbols, uint32_t u32_num_symbols )
{
	if (p_symbols == NULL || u32_num_symbols > TETHYS_MAX_SYMBOLS)
	{
		return TETHYS_STATUS_INVALID_PARAMS;
	}

	if (u32_num_symbols > 0)
	{
		TETHYS_DEBUG("Loading ABI (%u symbols)\n", u32_num_symbols);

		info.p_symbols = p_symbols;
		info.u32_num_symbols = u32_num_symbols;

		for (uint32_t i = 0; i < u32_num_symbols; i++)
		{
			TETHYS_VERBOSE("Symbol: %s at 0x%0lX\n", info.p_symbols[i].kpc_name, info.p_symbols[i].addr);
		}
	}

	return TETHYS_STATUS_OK;
}

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
tethys_status_t tethys_load_module ( const tethys_io_t * kp_io, tethys_module_t * p_module )
{
	tethys_elf32_header_t	header;
	const uint8_t			ku8_header_size = sizeof(tethys_elf32_header_t);
	const int32_t			ki32_image_size = kp_io->get_size(kp_io->pv_ctx);
	tethys_status_t			status = TETHYS_STATUS_FAILED;

	if (kp_io == NULL || p_module == NULL)
	{
		return TETHYS_STATUS_INVALID_PARAMS;
	}

	if (kp_io->get_size == NULL || kp_io->read == NULL)
	{
		return TETHYS_STATUS_INVALID_PARAMS;
	}

	if (ki32_image_size > 0)
	{
		TETHYS_DEBUG("Loading module file: %s (%u bytes)\n", p_module->kpc_name, (uint32_t)ki32_image_size);

		if (kp_io->read(kp_io->pv_ctx, &header, ku8_header_size, 0) == ku8_header_size)
		{
			status = tethys_validate_header(&header, (uint32_t)ki32_image_size);

			if (TETHYS_STATUS_OK == status)
			{
				status = tethys_write_module(&header, kp_io, p_module);
			}

			if (TETHYS_STATUS_OK == status)
			{
				status = tethys_apply_relocations(&header, kp_io, p_module);
			}
		}
		else
		{
			status = TETHYS_STATUS_READ_FAILED;
		}
	}
	else
	{
		status = TETHYS_STATUS_GET_SIZE_FAILED;
	}

	return status;
}

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
tethys_status_t tethys_get_module_size ( const tethys_io_t * kp_io, int32_t * pi32_size )
{
	const uint8_t			ku8_header_size = sizeof(tethys_elf32_header_t);
	tethys_elf32_header_t	header;
	uint32_t				u32_section_header_offset;
	bool					b_has_alloc_flag;
	bool					b_is_loadable_type;
	
	*pi32_size = -1;

	if (kp_io == NULL)
	{
		return TETHYS_STATUS_INVALID_PARAMS;
	}

	if (kp_io->get_size == NULL || kp_io->read == NULL)
	{
		return TETHYS_STATUS_INVALID_PARAMS;
	}

	if (kp_io->read(kp_io->pv_ctx, &header, ku8_header_size, 0) == ku8_header_size)
	{
		*pi32_size = 0;

		for (uint32_t i = 0; i < header.e_shnum; i++)
		{
			u32_section_header_offset = header.e_shoff + i * TETHYS_ELF32_SECTION_HEADER_SIZE;

			if (kp_io->read(kp_io->pv_ctx, &info.section_header, TETHYS_ELF32_SECTION_HEADER_SIZE, u32_section_header_offset) != TETHYS_ELF32_SECTION_HEADER_SIZE)
			{
				*pi32_size = -1;
				return TETHYS_STATUS_READ_FAILED;
			}

			b_has_alloc_flag = (info.section_header.sh_flags & SHF_ALLOC) != 0;
			b_is_loadable_type = (info.section_header.sh_type == SHT_PROGBITS || info.section_header.sh_type == SHT_NOBITS);

			if (!b_has_alloc_flag || !b_is_loadable_type)
			{
				continue;
			}

			*pi32_size += info.section_header.sh_size;
		}

		TETHYS_DEBUG("Module size: %d bytes\n", *pi32_size);
	}
	else
	{
		return TETHYS_STATUS_READ_FAILED;
	}

	return TETHYS_STATUS_OK;
}

/****************************************************************************************************
 *	P R I V A T E   F U N C T I O N S
 ****************************************************************************************************/

/**
 *	Performs basic sanity checks on the supplied ELF header
 *
 * 	@param[in] kp_header
 * 	the ELF header to validate
 * 
 * 	@param[in] u32_image_size
 * 	the size of the module whose header we're validating
 * 
 * 	@return
 * 	`TETHYS_STATUS_OK` upon success
 * 	`TETHYS_STATUS_BAD_HEADER` otherwise
 */
static tethys_status_t tethys_validate_header ( const tethys_elf32_header_t * kp_header, uint32_t u32_image_size )
{
	uint32_t		u32_bad = 0;
	uint32_t		u32_magic;

	// must at least contain the header
	u32_bad |= u32_image_size < (uint32_t)sizeof(*kp_header);

	// check magic bytes
	memcpy(&u32_magic, &kp_header->e_ident[EI_MAG0], sizeof(u32_magic));

	u32_bad |= (u32_magic != TETHYS_ELF_MAGIC_LE32);

	// aggregate checks...
	u32_bad |= (kp_header->e_ident[EI_CLASS] != ELFCLASS32);
	u32_bad |= (kp_header->e_ident[EI_DATA] != ELFDATA2LSB);
	u32_bad |= (kp_header->e_ident[EI_VERSION] != EV_CURRENT);

	u32_bad |= (kp_header->e_type != ET_REL);
	u32_bad |= (kp_header->e_machine != EM_ARM);

	u32_bad |= (kp_header->e_ehsize != (uint16_t)sizeof(*kp_header));

	return u32_bad ? TETHYS_STATUS_BAD_HEADER : TETHYS_STATUS_OK;
}

/**
 * 	Builds the Private Module Address Space (PMAS) according to the supplied ELF
 * 
 * 	@param[in] kp_header
 * 	the header of the supplied ELF
 * 
 * 	@param[in] kp_io
 * 	the user-defined IO interface
 * 
 * 	@param[out] p_module
 * 	a module reference which contains the module memory size and base address
 * 
 * 	@return
 * 	`TETHYS_STATUS_OK` upon success
 * 	`TETHYS_STATUS_MAX_SECTIONS_EXCEEDED` if TethysRT is configured with too few sections
 * 	`TETHYS_STATUS_READ_FAILED` if the IO read callback failed
 * 	`TETHYS_STATUS_MAX_REL_SECTIONS_EXCEEDED` if too many REL sections are found
 */
static tethys_status_t tethys_write_module ( const tethys_elf32_header_t * kp_header, const tethys_io_t * kp_io, tethys_module_t * p_module )
{	
	tethys_section_header_entry_t * p_entry;
	uint32_t						u32_section_header_offset;
	uint32_t						u32_cursor = 0;
	uint32_t						u32_align;

	uint32_t						u32_bytes_remaining;
	uint32_t						u32_chunk_size;
	uint32_t						u32_file_offset;
	uint8_t *						pu8_dest;

#if TETHYS_DEBUG_ENABLED
	uint32_t						u32_shstrtab_header_offset = kp_header->e_shoff + (kp_header->e_shstrndx * TETHYS_ELF32_SECTION_HEADER_SIZE);
	bool							b_found_strtab = false;
	uint32_t						u32_strtab_offset;
	tethys_elf32_section_header_t	strtab_section_header;
	uint32_t						u32_name_offset;
#endif // TETHYS_DEBUG_ENABLED

	if (kp_header->e_shnum > TETHYS_MAX_SECTIONS)
	{
		return TETHYS_STATUS_MAX_SECTIONS_EXCEEDED;
	}

	// initialize PMAS, sections all NOLOAD by default
	memset(&info.pmas, 0, sizeof(tethys_pmas_t));

	for (uint32_t i = 0; i < TETHYS_MAX_SECTIONS; i++)
	{
		info.pmas.p_section_header_data[i].u32_offset = TETHYS_PMAS_SECTION_NOLOAD;
	}

	// walk the section headers
	for (uint32_t i = 0; i < kp_header->e_shnum; i++)
	{
		// this is the current section header entry to which we're writing
		p_entry = &info.pmas.p_section_header_data[i];

		u32_section_header_offset = kp_header->e_shoff + i * TETHYS_ELF32_SECTION_HEADER_SIZE;

		if (kp_io->read(kp_io->pv_ctx, &info.section_header, TETHYS_ELF32_SECTION_HEADER_SIZE, u32_section_header_offset) != TETHYS_ELF32_SECTION_HEADER_SIZE)
		{
			return TETHYS_STATUS_READ_FAILED;
		}

#if TETHYS_DEBUG_ENABLED
		// read the section header for .shstrtab once on the first iteration (mainly to consolidate debug code)
		if (!b_found_strtab)
		{
			if (kp_io->read(kp_io->pv_ctx, &strtab_section_header, TETHYS_ELF32_SECTION_HEADER_SIZE, u32_shstrtab_header_offset) != TETHYS_ELF32_SECTION_HEADER_SIZE)
			{
				return TETHYS_STATUS_READ_FAILED;
			}
			else
			{
				u32_strtab_offset = strtab_section_header.sh_offset;
				b_found_strtab = true;
			}
		}

		// the name depends on the iteration, so don't get it from the dedicated strtab section header
		u32_name_offset  = info.section_header.sh_name;

		// populate the section name of the pmas section entry
		for (uint32_t j = 0; j < TETHYS_SECTION_NAME_MAX - 1; j++)
		{
			if (kp_io->read(kp_io->pv_ctx, &p_entry->pc_name[j], 1, u32_strtab_offset + u32_name_offset + j) != 1)
			{
				return TETHYS_STATUS_READ_FAILED;
			}

			if (p_entry->pc_name[j] == '\0')
			{
				break;
			}
		}

		p_entry->pc_name[TETHYS_SECTION_NAME_MAX - 1] = '\0';
#endif // TETHYS_DEBUG_ENABLED

		if (SHT_NULL == info.section_header.sh_type)
		{
			continue;
		}

		// if this is the symbol table, note it down, as well as the string table
		if (SHT_SYMTAB == info.section_header.sh_type)
		{
			info.pmas.u16_symtab_index = i;
			info.pmas.u16_strtab_index = info.section_header.sh_link;
			continue;
		}

		// capture the relocation sections
		if (SHT_REL == info.section_header.sh_type)
		{
			if (info.pmas.u16_num_rel_indices >= TETHYS_PMAS_MAX_REL_SECTIONS)
			{
				return TETHYS_STATUS_MAX_REL_SECTIONS_EXCEEDED;
			}
			else
			{
				info.pmas.u32_rel_indices[info.pmas.u16_num_rel_indices++] = i;
				continue;
			}
		}

		// if this section shouldn't have memory allocated for it, skip it
		if (!(info.section_header.sh_flags & SHF_ALLOC))
		{
			continue;
		}

		// set type to a known state
		p_entry->type = TETHYS_SECTION_TYPE_NOLOAD;

		// decide kind based on type + flags
		if (SHT_PROGBITS == info.section_header.sh_type)
		{
			if ((info.section_header.sh_flags & SHF_ALLOC) && !(info.section_header.sh_flags & SHF_WRITE))
			{
				p_entry->type = TETHYS_SECTION_TYPE_CODE;
			}
			else if (info.section_header.sh_flags & SHF_WRITE)
			{
				p_entry->type = TETHYS_SECTION_TYPE_DATA;
			}
		}
		else if (SHT_NOBITS == info.section_header.sh_type && (info.section_header.sh_flags & SHF_WRITE))
		{
			p_entry->type = TETHYS_SECTION_TYPE_BSS;
		}
		else
		{
			// some weird alloc section we don’t understand, treat as NOLOAD
			continue;
		}
		
		// the size of the section
		p_entry->u32_size = info.section_header.sh_size;

		// .text alignment might be 2 or 4, .data often 4. If compiler left alignment as 0, safe default is 4
		u32_align = info.section_header.sh_addralign == 0 ? 4: info.section_header.sh_addralign;

		// align cursor to next alignment multiple
		u32_cursor = (u32_cursor + (u32_align - 1)) & ~(u32_align - 1);

		// this is the offset from module base memory address
		p_entry->u32_offset = u32_cursor;

		// initialize bytes remaining
		u32_bytes_remaining = p_entry->u32_size;

		// this is the address to which we're writing
		pu8_dest = (uint8_t *)p_module->pv_base + p_entry->u32_offset;

		// this is the offset in the actual ELF file (for reading CODE/ DATA)
		u32_file_offset = info.section_header.sh_offset;

		// write the data into module address space (zero if .bss, pass on NOLOAD)
		switch (p_entry->type)
		{
			case TETHYS_SECTION_TYPE_NOLOAD:
				// do nothing
				break;
			case TETHYS_SECTION_TYPE_CODE:
			case TETHYS_SECTION_TYPE_DATA:
				while (u32_bytes_remaining > 0)
				{
					u32_chunk_size = (u32_bytes_remaining > TETHYS_IO_BUFFER_SIZE) ? TETHYS_IO_BUFFER_SIZE : u32_bytes_remaining;

					// read the next chunk
					if (kp_io->read(kp_io->pv_ctx, info.tethys_io_buf, u32_chunk_size, u32_file_offset) != (int32_t)u32_chunk_size)
					{
						return TETHYS_STATUS_READ_FAILED;
					}

					// copy chunk into module memory
					memcpy(pu8_dest, info.tethys_io_buf, u32_chunk_size);

					pu8_dest += u32_chunk_size;
					u32_file_offset += u32_chunk_size;
					u32_bytes_remaining -= u32_chunk_size;
				}
				break;
			case TETHYS_SECTION_TYPE_BSS:
				memset(pu8_dest, 0, u32_bytes_remaining);
				break;
			default:
				// shouldn't reach here
				break;
		}

		// move cursor past this section to get ready for the next one
		u32_cursor += info.section_header.sh_size;

		TETHYS_VERBOSE("Section %u: %s, Size: %u, Offset: %u\n", i, p_entry->pc_name, p_entry->u32_size, p_entry->u32_offset);
	}
	
	info.pmas.u32_num_section_headers = kp_header->e_shnum;

	// u32_cursor is now the total size of the module
	TETHYS_DEBUG("Module size: %u bytes\n", u32_cursor);

	return TETHYS_STATUS_OK;
}

/**
 *	Applies all relocation sections in the Private Module Address Space (PMAS)
 *	to the loaded module image.
 *
 *	Walks each SHT_REL section recorded in the PMAS, resolves the referenced symbols 
 *	using the .symtab, .strtab, and ABI table, and patches the corresponding locations 
 *	in the module image.
 *
 *	@param[in] kp_header
 *	the header of the supplied ELF
 *
 *	@param[in] kp_io
 *	the user-defined IO interface
 *
 *	@param[in,out] p_module
 *	a module reference which contains the module memory size and base address
 *
 *	@param[in] u32_symtab_off
 *	file offset of the ELF .symtab section
 *
 *	@param[in] u32_symtab_entsize
 *	size of each entry in the ELF .symtab section
 *
 *	@param[in] u32_strtab_off
 *	file offset of the ELF .strtab section
 *
 *	@return
 *	`TETHYS_STATUS_OK` on success
 *	`TETHYS_STATUS_BAD_HEADER` if a relocation section or symbol references invalid metadata  
 *	`TETHYS_STATUS_READ_FAILED` if the IO read callback failed  
 *	`TETHYS_STATUS_INVALID_PARAMS` if an imported symbol cannot be resolved via the ABI table
 */
static tethys_status_t tethys_apply_rel_sections ( const tethys_elf32_header_t * kp_header, const tethys_io_t * kp_io, tethys_module_t * p_module, uint32_t u32_symtab_off, uint32_t u32_symtab_entsize, uint32_t u32_strtab_off )
{
	uint32_t								u32_rel_sh_index;
	uint32_t								u32_rel_sh_offset;
	uint32_t								u32_num_reloc_records;
	uint32_t								u32_reloc_entry_offset;

	tethys_section_header_entry_t *			p_target_entry;
	uint8_t	*								pu8_target_base;

	tethys_elf32_relocation_entry_t			relocation_entry;
	uint32_t								u32_symbol_index;
	uint32_t								u32_symbol_offset;
	uint32_t								u32_symbol_reloc_type;
	tethys_elf32_symbol_t					symbol;
	uint32_t								u32_symbol_addr;

	uint8_t									pu8_name_buf[TETHYS_SYMBOL_NAME_MAX];
	uint32_t								u32_name_offset;
	uint32_t								u32_word;
	uint32_t								u32_addend;
	uint8_t *								pu8_patch;
	tethys_section_header_entry_t *			p_symbol_sh_entry;

	// No relocation sections? Nothing to do
	if (0 == info.pmas.u16_num_rel_indices)
	{
		return TETHYS_STATUS_OK;
	}

	for (uint32_t i = 0; i < info.pmas.u16_num_rel_indices; i++)
	{
		u32_rel_sh_index = info.pmas.u32_rel_indices[i];

		if (u32_rel_sh_index >= kp_header->e_shnum)
		{
			return TETHYS_STATUS_BAD_HEADER;
		}

		u32_rel_sh_offset = kp_header->e_shoff + (u32_rel_sh_index * TETHYS_ELF32_SECTION_HEADER_SIZE);

		if (kp_io->read(kp_io->pv_ctx, &info.section_header, TETHYS_ELF32_SECTION_HEADER_SIZE, u32_rel_sh_offset) != TETHYS_ELF32_SECTION_HEADER_SIZE)
		{
			return TETHYS_STATUS_READ_FAILED;
		}

		// ensure this actually is a relocation section
		if (SHT_REL != info.section_header.sh_type)
		{
			return TETHYS_STATUS_BAD_HEADER;
		}

		if (0 == info.section_header.sh_entsize)
		{
			return TETHYS_STATUS_BAD_HEADER;
		}

		u32_num_reloc_records = info.section_header.sh_size / info.section_header.sh_entsize;

		// sh_info is the target section index
		if (info.section_header.sh_info >= info.pmas.u32_num_section_headers)
		{
			return TETHYS_STATUS_BAD_HEADER;
		}

		p_target_entry = &info.pmas.p_section_header_data[info.section_header.sh_info];

		if (TETHYS_PMAS_SECTION_NOLOAD == p_target_entry->u32_offset)
		{
			// relocs targeting a section we didn't load means nothing to patch
			continue;
		}

		pu8_target_base = (uint8_t *)p_module->pv_base + p_target_entry->u32_offset;

		TETHYS_VERBOSE("Reloc section %u: target section %u (%s), number of relocation records: %u\n",
			u32_rel_sh_index, info.section_header.sh_info, p_target_entry->pc_name, u32_num_reloc_records);

		// walk the relocation records in this section
		for (uint32_t j = 0; j < u32_num_reloc_records; j++)
		{
			u32_reloc_entry_offset = info.section_header.sh_offset + (j * info.section_header.sh_entsize);

			if (kp_io->read(kp_io->pv_ctx, &relocation_entry, sizeof(relocation_entry), u32_reloc_entry_offset) != (int32_t)sizeof(relocation_entry))
			{
				return TETHYS_STATUS_READ_FAILED;
			}

			u32_symbol_index = ELF32_R_SYM(relocation_entry.r_info);
			u32_symbol_reloc_type  = ELF32_R_TYPE(relocation_entry.r_info);

			// only handle R_ARM_ABS32 for now
			if (R_ARM_ABS32 != u32_symbol_reloc_type)
			{
				TETHYS_VERBOSE("Unsupported relocation entry type in section %u: %u\n", u32_rel_sh_index, u32_symbol_reloc_type);
				return TETHYS_STATUS_BAD_HEADER;
			}

			// load the referenced symbol from .symtab
			u32_symbol_offset = u32_symtab_off + u32_symbol_index * u32_symtab_entsize;

			if (kp_io->read(kp_io->pv_ctx, &symbol, sizeof(symbol), u32_symbol_offset) != (int32_t)sizeof(symbol))
			{
				return TETHYS_STATUS_READ_FAILED;
			}

			/* Resolve symbol address:
			 *   internal symbol: via PMAS section + st_value
			 *   external (SHN_UNDEF): via ABI table (info.p_symbols)
			 */
			if (SHN_UNDEF == symbol.st_shndx)
			{
				// external / imported symbol – resolve by name using ABI table
				u32_name_offset = u32_strtab_off + symbol.st_name;

				memset(pu8_name_buf, 0, sizeof(pu8_name_buf));

				for (uint32_t k = 0; k < (TETHYS_SYMBOL_NAME_MAX - 1U); k++)
				{
					if (kp_io->read(kp_io->pv_ctx, &pu8_name_buf[k], 1U, u32_name_offset + k) != 1)
					{
						return TETHYS_STATUS_READ_FAILED;
					}

					if ('\0' == pu8_name_buf[k])
					{
						break;
					}
				}

				// linear search ABI table
				u32_symbol_addr = 0;

				for (uint32_t k = 0; k < info.u32_num_symbols; k++)
				{
					if (0 == strncmp(info.p_symbols[k].kpc_name, (const char *)pu8_name_buf, TETHYS_SYMBOL_NAME_MAX))
					{
						u32_symbol_addr = (uint32_t)info.p_symbols[k].addr;
						break;
					}
				}

				if (0 == u32_symbol_addr)
				{
					// unresolved external symbol
					TETHYS_DEBUG("%s\n", "Invalid params");
					return TETHYS_STATUS_INVALID_PARAMS;
				}
			}
			else
			{
				// internal symbol. st_shndx is section index
				if (symbol.st_shndx >= info.pmas.u32_num_section_headers)
				{
					return TETHYS_STATUS_BAD_HEADER;
				}

				p_symbol_sh_entry = &info.pmas.p_section_header_data[symbol.st_shndx];

				if (TETHYS_PMAS_SECTION_NOLOAD == p_symbol_sh_entry->u32_offset)
				{
					return TETHYS_STATUS_BAD_HEADER;
				}

				TETHYS_DIAG_PUSH();
				TETHYS_DIAG_IGNORED_PTR2INT();
				u32_symbol_addr = (uint32_t)((uint8_t *)p_module->pv_base + p_symbol_sh_entry->u32_offset + symbol.st_value);
				TETHYS_DIAG_POP();
			}

			/* Apply R_ARM_ABS32:
			 *   *word = sym_addr + addend
			 *   where addend is the original 32-bit contents at the patch site.
			 */
			pu8_patch = pu8_target_base + relocation_entry.r_offset;

			memcpy(&u32_word, pu8_patch, sizeof(u32_word));
			u32_addend = u32_word;
			u32_word = u32_symbol_addr + u32_addend;
			memcpy(pu8_patch, &u32_word, sizeof(u32_word));
		}
	}

	return TETHYS_STATUS_OK;
}

/**
 *	Performs all relocation processing for a loaded module and resolves its
 *	entry point symbol.
 *
 *	This function locates the ELF .symtab and .strtab sections, applies all
 *	relocation sections recorded in the PMAS, and then scans the symbol table
 *	to resolve the module entry symbol (`TETHYS_ENTRY_STRING`). On success, the
 *	entry function pointer is written to `p_module->entry` with the Thumb bit set.
 *
 *	@param[in] kp_header
 *	the header of the supplied ELF
 *
 *	@param[in] kp_io
 *	the user-defined IO interface
 *
 *	@param[in,out] p_module
 *	the module instance whose image will be relocated and whose entry point
 *	will be resolved
 *
 *	@return
 *	`TETHYS_STATUS_OK` upon success  
 *	`TETHYS_STATUS_BAD_HEADER` if .symtab/.strtab indices or relocation metadata are invalid  
 *	`TETHYS_STATUS_READ_FAILED` if the IO read callback failed  
 *	`TETHYS_STATUS_INVALID_PARAMS` if an imported symbol required by a relocation cannot be resolved
 */
static tethys_status_t tethys_apply_relocations ( const tethys_elf32_header_t * kp_header, const tethys_io_t * kp_io, tethys_module_t * p_module )
{
	uint32_t 							u32_symtab_sh_offset;
	uint32_t 							u32_strtab_sh_offset;

	uint32_t							u32_symtab_offset = 0;
	uint32_t							u32_symtab_size = 0;
	uint32_t							u32_symtab_entsize = 0;
	uint32_t							u32_strtab_offset = 0;
	uint32_t							u32_num_symtab_entries;

	uint32_t							u32_symbol_entry_offset;
	tethys_elf32_symbol_t				symbol;
	uint8_t								pu8_name_buf[TETHYS_SYMBOL_NAME_MAX];
	uint32_t							u32_name_offset;
	tethys_section_header_entry_t *		p_entry_sec;
	uint32_t							u32_entry_addr;

	tethys_status_t						status;

	// validate .symtab / .strtab indices
	if ((info.pmas.u16_symtab_index >= kp_header->e_shnum) || (info.pmas.u16_strtab_index >= kp_header->e_shnum))
	{
		return TETHYS_STATUS_BAD_HEADER;
	}

	// locate .symtab section
	u32_symtab_sh_offset = kp_header->e_shoff + (uint32_t)info.pmas.u16_symtab_index * TETHYS_ELF32_SECTION_HEADER_SIZE;

	if (kp_io->read(kp_io->pv_ctx, &info.section_header, TETHYS_ELF32_SECTION_HEADER_SIZE, u32_symtab_sh_offset) != TETHYS_ELF32_SECTION_HEADER_SIZE)
	{
		return TETHYS_STATUS_READ_FAILED;
	}

	u32_symtab_offset = info.section_header.sh_offset;
	u32_symtab_size = info.section_header.sh_size;
	u32_symtab_entsize = (info.section_header.sh_entsize != 0U) ? info.section_header.sh_entsize : (uint32_t)sizeof(tethys_elf32_symbol_t);

	// locate .strtab section
	u32_strtab_sh_offset = kp_header->e_shoff + (uint32_t)info.pmas.u16_strtab_index * TETHYS_ELF32_SECTION_HEADER_SIZE;

	if (kp_io->read(kp_io->pv_ctx, &info.section_header, TETHYS_ELF32_SECTION_HEADER_SIZE, u32_strtab_sh_offset) != TETHYS_ELF32_SECTION_HEADER_SIZE)
	{
		return TETHYS_STATUS_READ_FAILED;
	}

	u32_strtab_offset = info.section_header.sh_offset;

	// apply all relocation sections (if any)
	status = tethys_apply_rel_sections(kp_header, kp_io, p_module, u32_symtab_offset, u32_symtab_entsize, u32_strtab_offset);

	if (TETHYS_STATUS_OK != status)
	{
		return status;
	}

	// resolve module entry symbol, regardless of reloc presence
	if ((0 == u32_symtab_size) || (0 == u32_symtab_entsize))
	{
		return TETHYS_STATUS_BAD_HEADER;
	}

	u32_num_symtab_entries = u32_symtab_size / u32_symtab_entsize;

	for (uint32_t i = 0; i < u32_num_symtab_entries; i++)
	{
		u32_symbol_entry_offset = u32_symtab_offset + (i * u32_symtab_entsize);

		if (kp_io->read(kp_io->pv_ctx, &symbol, sizeof(symbol), u32_symbol_entry_offset) != (int32_t)sizeof(symbol))
		{
			return TETHYS_STATUS_READ_FAILED;
		}

		// skip undefined or nameless
		if ((0 == symbol.st_name) || (SHN_UNDEF == symbol.st_shndx))
		{
			continue;
		}

		// read symbol name from .strtab
		u32_name_offset = u32_strtab_offset + symbol.st_name;
		memset(pu8_name_buf, 0, sizeof(pu8_name_buf));

		for (uint32_t j = 0; j < (TETHYS_SYMBOL_NAME_MAX - 1U); j++)
		{
			if (kp_io->read(kp_io->pv_ctx, &pu8_name_buf[j], 1U, u32_name_offset + j) != 1)
			{
				return TETHYS_STATUS_READ_FAILED;
			}

			if ('\0' == pu8_name_buf[j])
			{
				break;
			}
		}

		TETHYS_VERBOSE("SYM[%u]: name=\"%s\"\n", i, pu8_name_buf);

		if (0 != strncmp((const char *)pu8_name_buf, TETHYS_ENTRY_STRING, TETHYS_SYMBOL_NAME_MAX))
		{
			continue;
		}

		// found entry symbol
		if (symbol.st_shndx >= info.pmas.u32_num_section_headers)
		{
			return TETHYS_STATUS_BAD_HEADER;
		}

		p_entry_sec = &info.pmas.p_section_header_data[symbol.st_shndx];

		if (TETHYS_PMAS_SECTION_NOLOAD == p_entry_sec->u32_offset)
		{
			return TETHYS_STATUS_BAD_HEADER;
		}

		// compute Thumb entry address
		TETHYS_DIAG_PUSH();
		TETHYS_DIAG_IGNORED_PTR2INT();
		u32_entry_addr = (uint32_t)((uint8_t *)p_module->pv_base + p_entry_sec->u32_offset + symbol.st_value);
		TETHYS_DIAG_POP();

		// thumb bit
		u32_entry_addr |= 1;

		TETHYS_DIAG_PUSH();
		TETHYS_DIAG_IGNORED_INT2PTR()
		p_module->entry = (tethys_entry_t)u32_entry_addr;
		TETHYS_DIAG_POP();

		TETHYS_VERBOSE("Module entry resolved: %s at %p\n", TETHYS_ENTRY_STRING, p_module->entry);
		break;
	}

	return TETHYS_STATUS_OK;
}
