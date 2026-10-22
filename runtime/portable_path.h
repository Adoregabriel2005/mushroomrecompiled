#pragma once
#include <stdio.h>
#include <wchar.h>
FILE *mushroom_wfopen(const wchar_t *path, const wchar_t *mode);
FILE *mushroom_fopen_utf8(const char *path, const char *mode);
