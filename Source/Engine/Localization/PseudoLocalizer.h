/**
 * @file PseudoLocalizer.h
 * @brief 의사 로컬라이제이션을 정의합니다. 원문을 악센트 글자로 바꾸고 늘리고 괄호로 감싸(거울 방식은 오른쪽에서 왼쪽 표시로 한 번 더 감쌉니다) 잘림과 하드코딩 텍스트를 찾게 합니다.
 * @details 언리얼 `-culture=en-US-POSIX`(의사 로컬라이즈), 유니티 Pseudo-Locale, 윈도 `qps-ploc` / `qps-plocm` 과 같은 일이다.
 *          메시지 구문(`{name}`, plural 키워드, `#`)은 건드리지 않고 텍스트 조각만 바꾼다(`TextFormatter::mapLiteralText`). 그래서 의사 문화권에서도 포맷이 그대로 해석된다.
 *          화면에 괄호 없이 보이는 텍스트는 테이블을 거치지 않은 하드코딩 텍스트이고, `]` 가 잘려 보이면 UI 가 번역의 길이를 수용하지 못하는 것이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Localization/CultureInfo.h"

namespace sw
{
    /**
     * @struct PseudoLocalizer
     * @brief 상태가 없는 변환입니다.
     */
    struct SW_API PseudoLocalizer
    {
        static constexpr uint32 kRightToLeftOverride  = 0x202E; ///< RLO 이며 거울 방식의 시작입니다.
        static constexpr uint32 kPopDirectionalFormat = 0x202C; ///< PDF 이며 거울 방식의 끝입니다.

        /** @brief 메시지 패턴 @p pattern 을 @p mode 로 바꿉니다. `None` 이면 그대로입니다. */
        static string transform( string_view pattern, PseudoLocaleMode mode );
        /**
         * @brief 텍스트 조각 하나를 변환합니다. ASCII 글자는 악센트 글자로 바꾸고, 모음은 한 번 더 써서 길이를 약 40 % 늘립니다. 구문 문자(`{` `}` `'` `#`)는 그대로 둡니다.
         * @details 리치 텍스트 표기(`[b]`, `[color=…]`, `[[`. `MarkupTagScanner` 참고)도 그대로 둔다.
         */
        static string accentLiteral( string_view literalText );
        /** @brief 의사 변환을 거친 텍스트인지(앞뒤 표시가 있는지) 확인합니다. 하드코딩 텍스트 찾기와 테스트가 씁니다. */
        static bool isPseudoText( string_view text );

    private:
        /** @brief 표기가 없는 글 조각의 ASCII 글자를 바꿉니다. */
        static string accentPlainText( string_view plainText );
    };
} // namespace sw
