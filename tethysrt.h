// SPDX-License-Identifier: Apache-2.0

#ifndef TETHYSRT_H
#define TETHYSRT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/****************************************************************************************************
 *	D E F I N E S   &   T Y P E D E F S
 ****************************************************************************************************/

#ifndef LONG_CALL
/**
 * Hint the compiler to generate a long call.
 *
 * This does not guarantee R_ARM_ABS32 for direct calls.
 * To gurantee ABS32-only usage, call via a function pointer.
 */
#define LONG_CALL	__attribute__((long_call))
#endif // LONG_CALL

/**
 *	Symbol definition utility
 */
#define TETHYS_SYMBOL(sym)				\
	{									\
		.kpc_name 	= #sym, 			\
		.addr		= (uintptr_t)(sym)	\
	}

/**
 *	Module status codes
 */
typedef enum _tethys_status
{
	TETHYS_STATUS_OK = 0,
	TETHYS_STATUS_FAILED,
	TETHYS_STATUS_BAD_HEADER,
	TETHYS_STATUS_INVALID_PARAMS,
	TETHYS_STATUS_READ_FAILED,
	TETHYS_STATUS_GET_SIZE_FAILED,
	TETHYS_STATUS_MAX_SECTIONS_EXCEEDED,
	TETHYS_STATUS_MAX_REL_SECTIONS_EXCEEDED,
	////////////////////
	TETHYS_NUM_STATUSES
} tethys_status_t;

/**
 *	Read callback typedef
 *	
 * 	@param[in] pv_ctx
 * 	implementation-defined context (file pointer, etc.)
 * 
 * 	@param[out] pv_buffer_out
 * 	a buffer in which data will be written
 * 
 *	@param[in] u32_num_bytes
 *	the number of bytes to read
 *
 *	@param[in] u32_offset
 *	offset bytes from where to read
 * 
 * 	@return
 * 	the number of bytes read or -1 on failure
 */
typedef int32_t ( * tethys_read_cb_t )
(
	void *,		// pv_ctx
	void *,		// pv_buffer_out
	uint32_t,	// u32_num_bytes
	uint32_t	// u32_offset
);

/**
 *	Get size callback typedef
 *
 * 	@param[in] pv_ctx
 * 	implementation-defined context (file pointer, etc.)
 *	
 * 	@return
 * 	the size of the module file in bytes or -1 on failure
 */
typedef int32_t ( * tethys_get_size_cb_t )
(
	void *		// pv_ctx,
);

/**
 *	Module entry point function
 *	
 *	Will be relocated internally upon successful module loading
 *
 * 	@param[in, out] pv_args
 * 	generic argument(s)
 * 
 *	@return
 *	application-specific return code
 */
typedef int32_t ( * tethys_entry_t )
(
	void *		// pv_args
);

/**
 *	Symbol entry typedef
 */
typedef struct _tethys_symbol
{
	const char *			kpc_name;	// symbol name string
	uintptr_t				addr;		// the address of the symbol
} tethys_symbol_t;

/**
 *	IO struct passed to `tethys_load` for image provisioning
 *
 * 	Contains user-defined callbacks and an accompanying generic context
 */
typedef struct _tethys_io
{
	tethys_read_cb_t		read;		// user-defined read function pointer
	tethys_get_size_cb_t	get_size;	// user-defined get_size function pointer
	void *					pv_ctx;		// context to be used by callbacks
} tethys_io_t;

/**
 *	Module struct passed to `tethys_load` for image size validation
 *	and module entry function relocation.
 */
typedef struct _tethys_module
{
	void *					pv_base;	// base of module address space provided 
	uint32_t				u32_size;	// size of the address space
	tethys_entry_t			entry;		// module entry point (`tethys_main`, relocated internally)
	const char *			kpc_name;	// name of the module (used for debugging)
} tethys_module_t;

/****************************************************************************************************
 *	F U N C T I O N S
 ****************************************************************************************************/

tethys_status_t		tethys_init				( tethys_symbol_t * p_symbols, uint32_t u32_num_symbols );
tethys_status_t		tethys_load_module		( const tethys_io_t * kp_io, tethys_module_t * p_module );
tethys_status_t		tethys_get_module_size	( const tethys_io_t * kp_io, int32_t * pi32_size );

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // TETHYSRT_H
