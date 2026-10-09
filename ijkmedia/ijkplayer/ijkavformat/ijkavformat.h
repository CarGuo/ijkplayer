/*
 * ijkavformat.h
 *
 * Copyright (c) 2003 Bilibili
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFORMAT_IJKAVFORMAT_H
#define AVFORMAT_IJKAVFORMAT_H

#include "libavformat/avformat.h"

#define AV_PKT_FLAG_DISCONTINUITY 0x0100

/* Snapshot of the actual opened inner format, never an inner-context pointer.
 * Thread-safe while the caller retains avf. As with every AVFormatContext API,
 * the caller must join readers before closing/freeing avf. False while opening,
 * after an error, for known recordings, or without the opt-in stable-stream mode. */
int ijklivehook_is_live_rtsp(const AVFormatContext *avf);
/* True for a latched safety failure or cancellation in the opt-in mode. Normal
 * wrapper transport failures still use the wrapper's existing retry policy. */
int ijklivehook_is_terminal_error(const AVFormatContext *avf, int error);

#endif
