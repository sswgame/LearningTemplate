/**
 * @file DebugHUDSectionFormat.h
 * @brief `DebugHUDSectionWriter::addLineFormat` 의 정의입니다. 서식 줄을 쓰는 섹션 .cpp 만 include 합니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Console/DebugHUDRegistry.h"
#include "Core/Container/formatString.h"
#include "Core/String/fixed_string.h"

#if SW_DEV_COMMANDS_ENABLED

namespace sw
{
    template <typename... Args>
    void DebugHUDSectionWriter::addLineFormat( string_view label, string_view format, Args&&... args )
    {
        fixed_string<constant::kMaxBuffer128> text;
        formatstring( text.data(), text.capacity(), format, std::forward<Args>( args )... );
        addLine( label, text.c_str() );
    }
} // namespace sw

#endif
