/**
 * @file EditorDisplayNameUtil.h
 * @brief 코드 이름(`_maxSpeed` · `bIsVisible` · `m_speed` · `max_speed`)을 에디터에 보일 이름("Max Speed")으로 다듬습니다(ImGui 에 의존하지 않습니다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @struct EditorDisplayNameUtil
     * @brief 표시 이름(`DisplayName`)이 적히지 않은 이름의 이름표입니다 — 리플렉션 인스펙터와 머티리얼 패널이 같이 씁니다.
     * @details 규칙은 이 한 곳에만 둔다(패널마다 다시 만들면 규칙이 조금씩 갈라진다).
     *          ① 앞 밑줄과 멤버 · 정적 접두어(`m_` · `s_`)를 뗀다. ② 바로 뒤가 대문자인 `b`(불리언) · `p` · `pp`(포인터) 접두어를 뗀다(`base` · `point` 는 그대로).
     *          ③ `_` 와 띄어쓰기는 단어 경계다. 소문자 뒤 대문자(`max|HP`), 약어 끝(`HP|Max`), 글자 뒤 숫자(`Texture|2D`)에서 나누고,
     *          숫자 뒤 대문자는 그 뒤가 소문자일 때만 나눈다(`2D` 는 붙이고 `0|Distance` 는 나눈다). ④ 단어마다 첫 글자를 대문자로 쓴다.
     *          이미 다듬은 이름("Max Speed")은 그대로 나온다. 글자 분류는 ASCII 만 본다(다른 글자는 단어 안의 글자로 둔다).
     *          만든 이름은 번역 키가 아니다 — 번역 수집(`LocalizationTools`)은 코드의 `SW_LOCTEXT` · `SW_LOCKEY` 와 에셋만 읽고 이 결과를 보지 않는다.
     */
    struct SW_EDITOR_API EditorDisplayNameUtil
    {
        /** @brief @p name 을 보일 이름으로 다듬습니다. 밑줄뿐이라 남는 것이 없으면(`_` · `__`) @p name 그대로입니다. */
        [[nodiscard]] static string makeDisplayName( string_view name );

        /** @brief 적힌 표시 이름 @p displayName 이 있으면 그것을, 비었으면 @p name 을 다듬은 이름을 반환합니다. */
        [[nodiscard]] static string chooseLabel( string_view displayName, string_view name );
    };
} // namespace sw::editor
