// clang-format off
/*
 * KDiff3 - Text Diff And Merge Tool
 *
 * SPDX-FileCopyrightText: 2018-2020 Michael Reeves reeves.87@gmail.com
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
// clang-format on
#ifndef LINEREF_H
#define LINEREF_H

#include "TypeUtils.h"

#include <stdlib.h>
#include <type_traits>

#include <QtGlobal>

class LineRef
{
  public:
    typedef qint32 LineType;

    static constexpr LineType invalid = -1;
    constexpr inline LineRef() = default;
    //cppcheck-suppress noExplicitConstructor
    constexpr inline LineRef(const LineType i) noexcept { mLineNumber = i; }
    //cppcheck-suppress noExplicitConstructor
    constexpr inline LineRef(const quint64 i) noexcept
    {
        if(i <= limits<LineType>::max())
            mLineNumber = (LineType)i;
        else
            mLineNumber = invalid;
    }
    //cppcheck-suppress noExplicitConstructor
    constexpr inline LineRef(const qint64 i) noexcept
    {
        if(i <= limits<LineType>::max() && i >= 0)
            mLineNumber = (LineType)i;
        else
            mLineNumber = invalid;
    }

    inline operator LineType() const noexcept { return mLineNumber; }

    inline operator SafeInt<LineType>() const noexcept { return mLineNumber; }

    inline LineRef& operator=(const LineType lineIn) noexcept
    {
        mLineNumber = lineIn;
        return *this;
    }

    inline LineRef& operator+=(const LineType& inLine) noexcept
    {
        mLineNumber += inLine;
        return *this;
    };

    LineRef& operator++() noexcept
    {
        ++mLineNumber;
        return *this;
    };

    const LineRef operator++(int) noexcept
    {
        LineRef line(*this);
        ++mLineNumber;
        return line;
    };

    LineRef& operator--() noexcept
    {
        --mLineNumber;
        return *this;
    };

    const LineRef operator--(int) noexcept
    {
        LineRef line(*this);
        --mLineNumber;
        return line;
    };
    inline void invalidate() noexcept { mLineNumber = invalid; }
    [[nodiscard]] inline bool isValid() const noexcept { return mLineNumber != invalid; }

  private:
    SafeInt<LineType> mLineNumber = invalid;
};

/*
    This is here because its easy to unknowingly break these conditions. The resulting
    compiler errors are numerous and may be a bit cryptic if you aren't familiar with the C++ language.
    Also some IDEs with clangd or ccls integration can automatically check static_asserts
    without doing a full compile.
*/
static_assert(std::is_copy_constructible<LineRef>::value, "LineRef must be copy constructible.");
static_assert(std::is_copy_assignable<LineRef>::value, "LineRef must copy assignable.");
static_assert(std::is_move_constructible<LineRef>::value, "LineRef must be move constructible.");
static_assert(std::is_move_assignable<LineRef>::value, "LineRef must be move assignable.");
static_assert(std::is_convertible<LineRef, int>::value, "Can not convert LineRef to int.");
static_assert(std::is_convertible<int, LineRef>::value, "Can not convert int to LineRef.");

using LineType = LineRef::LineType;

//Break in an obvious way if way can't get LineCounts from Qt supplied ints without overflow issues.
static_assert(sizeof(LineType) >= sizeof(QtNumberType)); //Generally assumed by KDiff3
#endif
