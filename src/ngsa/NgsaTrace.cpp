/* Copyright (C) 2026 André Walsh

   This file is part of bonSQL. bonSQL is free software: you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the Free Software Foundation, either version 3
   of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see
   COPYING. */
/*
 * NGSA build: optional file trace, see include/ngsa/NgsaTrace.h.
 */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "ngsa/NgsaTrace.h"

void
NgsaTrace(const char* fmt, ...)
{
    static char     path[MAX_PATH];
    static int      state = 0;      /* 0 = unknown, 1 = on, -1 = off */
    FILE*           f;
    SYSTEMTIME      st;
    va_list         ap;

    if(state == 0)
        state = (GetEnvironmentVariableA("NGSA_TRACE", path, sizeof(path)) > 0) ? 1 : -1;

    if(state < 0 || !(f = fopen(path, "a")))
        return;

    GetLocalTime(&st);
    fprintf(f, "%02d:%02d:%02d.%03d [%lu] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId());
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}
