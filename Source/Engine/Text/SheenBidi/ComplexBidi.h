/**
 * @file ComplexBidi.h
 * @brief 전체 UAX #9 양방향 수준(포개진 방향 제어 · 숫자 앞뒤 기호 W2 ~ W6 포함)을 정하는 창구입니다. 구현은 이 폴더 안에서만 양방향 라이브러리를 봅니다.
 * @details `SW_ENABLE_COMPLEX_TEXT` 가 켜진 빌드에서 `TextBidi::resolveLevels` 가 이것을 부릅니다(꺼진 빌드는 `TextBidi` 의 단순 버전).
 *          줄 안 재배열(L2) · 거울 문자(L4)는 `TextBidi` 가 그대로 합니다 — 수준만 바뀌면 같은 계산이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Text/ITextShaper.h"

namespace sw
{
    /**
     * @struct ComplexBidiUtil
     * @brief 전체 UAX #9 수준입니다.
     */
    struct ComplexBidiUtil
    {
        /** @brief `TextBidi::resolveLevels` 와 같은 계약입니다(@p outListLevel 은 앞을 비우고 @p listCodepoint 와 같은 길이로 채운다, 단락 구분마다 새 문단). */
        static void resolveLevels( const vector<uint32>& listCodepoint, TextDirection paragraphDirection, vector<uint8>& outListLevel );
    };
} // namespace sw
