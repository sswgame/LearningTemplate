/**
 * @file SourceLocation.h
 * @brief 부른 자리(파일 · 줄)를 값으로 받는 `SourceLocation` 입니다(C++20 `std::source_location` 자리, C++17).
 * @details 함수의 기본 인자에 `SourceLocation::current()` 를 두면 부른 자리가 들어온다 — `__FILE__` · `__LINE__` 을 넘기려고 매크로로 감싸지 않아도 된다.
 *          `__builtin_FILE` · `__builtin_LINE` 은 clang · MSVC(2019 16.6 부터)가 C++17 에서도 부른 자리로 푼다.
 *          로그 매크로(`SW_LOG_*`)는 이것으로 함수가 되지 못한다 — 태그(`SW_LOG_TAG`)가 타깃마다 PCH 뒤에 다시 정의돼 헤더의 기본 인자에는 PCH 의 것이 굳고, 함수 인자는 상세도 검사 전에 평가된다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @struct SourceLocation @brief 소스 한 자리입니다. 파일이 nullptr 이면 모르는 자리다. */
    struct SourceLocation
    {
        const utf8* _pFile{ nullptr };
        int32       _line{ 0 };

        /** @brief 부른 자리입니다. 기본 인자로 쓰면 그 함수를 부른 자리다. 인자는 넘기지 않는다. */
        [[nodiscard]] static constexpr SourceLocation current( const utf8* pFile = __builtin_FILE(), int32 line = static_cast<int32>( __builtin_LINE() ) ) noexcept
        {
            return SourceLocation{ pFile, line };
        }
    };
} // namespace sw
