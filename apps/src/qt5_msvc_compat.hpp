// Copyright 2026 MVKit contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QtCore/qglobal.h>

// Use the pointer form Qt already uses on other standard libraries.
// qglobal.h sets the MSVC-specific macros before Qt's containers use them.
#undef QT_MAKE_UNCHECKED_ARRAY_ITERATOR
#undef QT_MAKE_CHECKED_ARRAY_ITERATOR
#define QT_MAKE_UNCHECKED_ARRAY_ITERATOR(pointer) (pointer)
#define QT_MAKE_CHECKED_ARRAY_ITERATOR(pointer, count) (pointer)
