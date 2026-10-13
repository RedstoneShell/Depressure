// SPDX-License-Identifier: LicenseRef-Depressure-BSL-1.1
// Copyright (c) 2026 RedstoneShell
//
// Change Date: October 11, 2158
// Change License: GPL-3.0-or-later
// Additional Use Grant: Compilation of third-party software using these headers is permitted without restrictions.

#ifndef DDEF_H
#define DDEF_H
#include "dtypes.h"
#define doffsetof(type, member)  ((usize)&(((type *)0)->member))
#define dcontainer_of(ptr, type, member) \
    ((type *)((uptr)(ptr) - doffsetof(type, member)))
#define dcountof(arr)  (sizeof(arr) / sizeof((arr)[0]))
#endif /* DDEF_H */