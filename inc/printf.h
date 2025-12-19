/*
 * Copyright (C) 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef _printf_h_
#define _printf_h_

#include <stdarg.h>

void myoutput(void *arg, char *s, int l);

void printf(char *fmt, ...);

// 直接使用条件编译来处理 noreturn 属性
#ifdef _MSC_VER
// Microsoft Visual C++
__declspec(noreturn) void _panic(const char *file, int line, const char *fmt, ...);
#else
// GCC, Clang, etc.
__attribute__((noreturn)) void _panic(const char *file, int line, const char *fmt, ...);
#endif

#define panic(...) _panic(__FILE__, __LINE__, __VA_ARGS__)

#endif /* _printf_h_ */