/**
 * @file UIBindingExpression.h
 * @brief 문서 속성 값의 바인딩 식(`{bind:_health, mode=TwoWay}`)을 푼 결과입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /** @brief 바인딩 값이 어디서 오는가입니다. */
    enum class UIBindingSource : uint8
    {
        ViewModel, ///< `{bind:필드}` — 화면 뷰모델의 필드, 바뀐 필드 알림으로만 갱신(UE5 MVVM)
        Poll,      ///< `{poll:필드}` — 화면 뷰모델의 필드를 매 프레임 견준다(개발 편의 — 비용 카운터 UI.PollBindings)
        Setting    ///< `{setting:설정 id}` — 사용자 설정(`UserSettingsManager`)의 값 · 범위 · 선택지 · 사용 가능
    };

    /** @brief 값이 흐르는 방향입니다. */
    enum class UIBindingMode : uint8
    {
        OneWay, ///< 소스 → 위젯
        TwoWay  ///< 소스 → 위젯, 사용자가 위젯 값을 바꾸면 위젯 → 소스(슬라이더 · 체크 · 콤보 · 글 입력)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIBindingExpression
     * @brief 바인딩 식 하나를 푼 것입니다. 꼴: `{종류:경로[, 키=값]*}` — 종류 `bind` · `poll` · `setting`, 키 `mode`(OneWay · TwoWay) ·
     *        `format`(현지화 키 — 그 메시지 패턴에 `{value}` 로 넣는다) · `converter`(변환기 이름 — `UIBindingConverterRegistry`).
     * @details 설정 바인딩은 양방향이 기본입니다(옵션 메뉴의 줄은 값을 바꾸는 것이 일이다). 공백은 무시합니다.
     */
    struct SW_API UIBindingExpression
    {
        string          _path{};      ///< 뷰모델 필드 경로(`_health` · `_stats._armor`) 또는 설정 id
        hashed_string   _format{};    ///< 현지화 메시지 패턴의 키(비면 없음)
        hashed_string   _converter{}; ///< 변환기 이름(비면 없음)
        UIBindingSource _source{ UIBindingSource::ViewModel };
        UIBindingMode   _mode{ UIBindingMode::OneWay };

        /**
         * @brief 식 @p text 를 풉니다.
         * @return 꼴이 틀리면(중괄호 · 모르는 종류 · 빈 경로 · 모르는 키 · 모르는 mode) false 이고 @p outError 에 이유(영어)를 둡니다.
         */
        [[nodiscard]] static bool parse( string_view text, UIBindingExpression& outExpression, string& outError );
    };
} // namespace sw
