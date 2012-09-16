/* WAV-PRG: a program for converting C64 tapes into files suitable
 * for emulators and back.
 *
 * Copyright (c) Fabrizio Gennari, 1998-2003
 *
 * The program is distributed under the GNU General Public License.
 * See file LICENSE.TXT for details.
 *
 * prg2wav_core.h : header file for prg2wav_core.c
 *
 * This file belongs to the prg->wav part
 * This file is part of WAV-PRG core processing files
 */

#ifndef PRG2WAV_H_INCLUDED
#define PRG2WAV_H_INCLUDED

#include <stdint.h>

struct simple_block_list_element;
struct audiotap;
struct prg2wav_display_interface;
struct display_interface_internal;

void prg2wav_convert(struct simple_block_list_element *program,
                     struct audiotap *file,
                     char fast,
                     char raw,
                     uint16_t threshold,
                     int machine,
                     struct prg2wav_display_interface *display_interface,
                     struct display_interface_internal *internal);

#endif /* PRG2WAV_H_INCLUDED */
