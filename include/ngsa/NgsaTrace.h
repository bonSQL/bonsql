/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA build: optional file trace for harness diagnosis. Set the environment variable
 * NGSA_TRACE=<path> before starting SQLyog; lines are appended with a timestamp. No cost when unset.
 */
#ifndef NGSA_TRACE_H
#define NGSA_TRACE_H

void NgsaTrace(const char* fmt, ...);

#endif
