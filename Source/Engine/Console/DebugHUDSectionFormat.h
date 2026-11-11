/**
 * @file DebugHUDSectionFormat.h
 * @brief `DebugHUDSectionWriter::addLineFormat` 의 정의입니다. 서식 줄을 쓰는 섹션 .cpp 만 include 합니다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Console/DebugHUDRegistry.h"

// Shipping 에서도 정의한다 — 섹션 본문은 등록만 꺼지고 컴파일은 되므로, 여기서 빼면 Shipping 에서만 컴파일 오류(undefined-func-template)가 난다(부르지 않으면 코드가 남지 않는다).
namespace sw
{
    template <typename... Args>
    void DebugHUDSectionWriter::addLineFormat( string_view label, string_view format, Args&&... args )
    {
        fixed_string<constant::buffer::kMax128> text;
        formatstring( text.data(), text.capacity(), format, std::forward<Args>( args )... );
        addLine( label, text.c_str() );
    }
} // namespace sw
