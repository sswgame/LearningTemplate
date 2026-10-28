/**
 * @file UIBindingExpression.h
 * @brief 문서 속성 값의 바인딩 식(`{bind:_health, mode=TwoWay}`)을 해석한 결과입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /** @brief 바인딩 값의 출처입니다. */
    enum class UIBindingSource : uint8
    {
        ViewModel, ///< `{bind:필드}` 입니다. 화면 뷰모델의 필드이며, 바뀐 필드 알림으로만 갱신합니다(UE5 MVVM).
        Poll,      ///< `{poll:필드}` 입니다. 화면 뷰모델의 필드를 매 프레임 비교합니다(개발 편의용이며 비용 카운터는 UI.PollBindings).
        Setting    ///< `{setting:설정 id}` 입니다. 사용자 설정(`UserSettingsManager`)의 값, 범위, 선택지, 사용 가능 여부입니다.
    };

    /** @brief 값이 흐르는 방향입니다. */
    enum class UIBindingMode : uint8
    {
        OneWay, ///< 소스에서 위젯으로만 흐릅니다.
        TwoWay  ///< 소스에서 위젯으로 흐르고, 사용자가 위젯 값을 바꾸면 위젯에서 소스로도 흐릅니다(슬라이더, 체크, 콤보, 텍스트 입력).
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIBindingExpression
     * @brief 바인딩 식 하나를 해석한 결과입니다. 형식은 `{종류:경로[, 키=값]*}` 이고, 종류는 `bind`, `poll`, `setting`, 키는 `mode`(OneWay, TwoWay),
     *        `format`(현지화 키이며 그 메시지 패턴에 `{value}` 로 넣습니다), `converter`(변환기 이름이며 `UIBindingConverterRegistry` 에서 찾습니다)입니다.
     * @details 설정 바인딩은 양방향이 기본이다(옵션 메뉴의 줄은 값을 바꾸는 것이 일이기 때문이다). 공백은 무시한다.
     */
    struct SW_API UIBindingExpression
    {
        string          _path{};      ///< 뷰모델 필드 경로(`_health`, `_stats._armor`) 또는 설정 id 입니다.
        hashed_string   _format{};    ///< 현지화 메시지 패턴의 키입니다(비면 없음).
        hashed_string   _converter{}; ///< 변환기 이름입니다(비면 없음).
        UIBindingSource _source{ UIBindingSource::ViewModel };
        UIBindingMode   _mode{ UIBindingMode::OneWay };

        /**
         * @brief 식 @p text 를 해석합니다.
         * @return 형식이 틀리면(중괄호, 모르는 종류, 빈 경로, 모르는 키, 모르는 mode) false 이고 @p outError 에 이유(영어)를 둡니다.
         */
        [[nodiscard]] static bool parse( string_view text, UIBindingExpression& outExpression, string& outError );
    };
} // namespace sw
