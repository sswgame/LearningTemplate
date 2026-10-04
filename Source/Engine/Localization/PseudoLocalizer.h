/**
 * @file PseudoLocalizer.h
 * @brief 의사 로컬라이제이션 — 원문을 악센트 글자로 바꾸고 늘리고 괄호로 감싸(거울 방식은 오른쪽→왼쪽 표시로 더 감싸) 잘림 · 하드코딩 글자를 찾게 합니다.
 * @details 언리얼 `-culture=en-US-POSIX`(의사 로컬라이즈) · 유니티 Pseudo-Locale · 윈도 `qps-ploc` / `qps-plocm` 과 같은 일입니다.
 *          메시지 구문(`{name}` · plural 키워드 · `#`)은 건드리지 않고 글자 조각만 바꿉니다(`TextFormatter::mapLiteralText`) — 의사 문화권에서도 포맷이 그대로 풀립니다.
 *          화면에 괄호 없이 보이는 글은 표를 지나지 않은 하드코딩 글이고, `]` 가 잘려 보이면 칸이 번역의 길이를 못 받는 것입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Localization/CultureInfo.h"

namespace sw
{
    /**
     * @struct PseudoLocalizer
     * @brief 상태가 없는 변환입니다.
     */
    struct SW_API PseudoLocalizer
    {
        static constexpr uint32 kRightToLeftOverride  = 0x202E; ///< RLO — 거울 방식의 시작
        static constexpr uint32 kPopDirectionalFormat = 0x202C; ///< PDF — 거울 방식의 끝

        /** @brief 메시지 패턴 @p pattern 을 @p mode 로 바꿉니다. `None` 이면 그대로입니다. */
        static string transform( string_view pattern, PseudoLocaleMode mode );
        /** @brief 글자 조각 하나 — ASCII 글자는 악센트 글자로, 모음은 한 번 더 써서 길이를 약 40 % 늘립니다. 구문 문자(`{` `}` `'` `#`)는 그대로입니다. */
        static string accentLiteral( string_view literalText );
        /** @brief 의사 변환을 거친 글인지(앞뒤 표시가 있는지)입니다. 하드코딩 글 찾기 · 시험이 씁니다. */
        static bool isPseudoText( string_view text );
    };
} // namespace sw
