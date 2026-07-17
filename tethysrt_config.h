// SPDX-License-Identifier: Apache-2.0

#ifndef TETHYSRT_CONFIG_H
#define TETHYSRT_CONFIG_H

/****************************************************************************************************
 *	D E F I N E S   &   T Y P E D E F S
 ****************************************************************************************************/

/**
 *	Controls debug logging
 *
 * 	0 - disabled (`printf` implementation not required)
 * 	1 - debug logging only
 * 	2 - verbose logging
 */
#ifndef TETHYS_DEBUG_LEVEL
#define TETHYS_DEBUG_LEVEL			(0)
#endif // TETHYS_DEBUG_LEVEL

/**
 *	The printf-style function to use for debugging
 *
 * 	Change to your for debugging if required
 */
#define TETHYS_PRINTF(...)			// printf(__VA_ARGS__)

/**
 *	The maximum number of sections permitted per module
 */
#define TETHYS_MAX_SECTIONS			(16U)

/**
 *	The maximum number of symbols permitted per module
 */
#define TETHYS_MAX_SYMBOLS			(16U)

/**
 *	The maximum size of a symbol in bytes
 */
#define TETHYS_SYMBOL_NAME_MAX		(64U)

/**
 *	Size of the IO buffer in bytes
 */
#define	TETHYS_IO_BUFFER_SIZE		(512U)

/**
 *	Config sanity checks 
 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define TETHYS_STATIC_ASSERT(cond, msg)	_Static_assert(cond, msg)

// TODO: review these
TETHYS_STATIC_ASSERT(TETHYS_MAX_SECTIONS > 0, "TETHYS_MAX_SECTIONS must be > 0");
TETHYS_STATIC_ASSERT(TETHYS_MAX_SYMBOLS  > 0, "TETHYS_MAX_SYMBOLS must be > 0");
TETHYS_STATIC_ASSERT(TETHYS_IO_BUFFER_SIZE > 0, "TETHYS_IO_BUFFER_SIZE must be > 0");

#undef TETHYS_STATIC_ASSERT
#endif // (__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)

#endif // TETHYSRT_CONFIG_H
