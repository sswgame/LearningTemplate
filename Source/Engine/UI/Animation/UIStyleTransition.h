/**
 * @file UIStyleTransition.h
 * @brief 스타일 전환(transition)을 정의합니다. 계산된 스타일이 바뀔 때 `_transition` 에 적힌 필드만 지금 값에서 새 값으로 보간합니다(유니티 USS, CSS 의 `transition` 에 해당).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Animation/Graph/BlendCurve.h"
#include "Engine/UI/Style/WidgetStyle.h"

namespace sw
{
    class Widget;
    class WidgetTree;

    /**
     * @struct UIStyleTransitionSpec
     * @brief `WidgetStyle::_transition` 텍스트를 읽은 결과입니다. 필드마다 길이와 곡선을 가집니다.
     * @details 형식은 `"_backgroundColor 0.12 EaseOut, _opacity 0.2"` 이다. 쉼표로 나눈 항목마다 필드 이름(또는 보간되는 모든 필드를 뜻하는 `all`), 길이(초), 곡선(`BlendCurve`
     *          이름이며 없으면 `EaseOut`)을 적는다. 뒤 항목이 앞 항목을 덮어쓴다. 보간되는 필드는 실수 필드(색, 모서리, 두께, 그림자, 여백, 글자 크기, 외곽선, 불투명도)이고
     *          폰트와 전환 자신은 받지 않는다(로드 오류).
     */
    struct SW_API UIStyleTransitionSpec
    {
        struct Entry
        {
            UIStyleField _field{ UIStyleField::Count };
            float32      _duration{ 0.0f };
            BlendCurve   _curve{ BlendCurve::EaseOut };
        };

        vector<Entry> _listEntry{}; ///< 필드마다 하나입니다(필드 순서).

        /** @brief 필드 @p field 의 항목입니다. 없으면 nullptr 입니다. */
        const Entry* findEntry( UIStyleField field ) const;

        /** @brief 텍스트 @p text 를 읽습니다. 모르는 필드, 보간할 수 없는 필드, 읽지 못한 길이, 모르는 곡선이면 false 를 반환하고 @p outError 에 이유(영어)를 둡니다. */
        [[nodiscard]] static bool parse( string_view text, UIStyleTransitionSpec& outSpec, string& outError );
        /** @brief 필드 @p field 가 보간되는 필드(실수 성분)이면 true 입니다. */
        static bool isInterpolable( UIStyleField field );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UIStyleTransitionState
     * @brief 전환 중인 위젯 하나의 상태입니다. 보이는 계산된 스타일(목표와 필드마다의 보간 값)과 필드마다의 진행을 가지며, 위젯이 소유하고 전환이 끝나면 지웁니다.
     */
    struct SW_API UIStyleTransitionState
    {
        struct Channel
        {
            float32      _arrFrom[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            float32      _arrTo[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            float32      _elapsed{ 0.0f };
            float32      _duration{ 0.0f };
            UIStyleField _field{ UIStyleField::Count };
            BlendCurve   _curve{ BlendCurve::EaseOut };
        };

        UIComputedStyle _shown{};       ///< 위젯이 읽는 값입니다(`Widget::getComputedStyle`).
        vector<Channel> _listChannel{}; ///< 진행 중인 필드입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class UIStyleTransition
     * @brief 스타일 순회가 부르는 전환 시작과, 애니메이션 단계가 부르는 진행을 담당합니다.
     * @details 시작 규칙은 CSS 와 같다. 새 계산된 스타일의 `_transition` 이 그 필드를 적었고, 기존 스타일과 새 스타일이 모두 그 필드를 정했으면 지금 보이는 값에서 새 값으로 보간한다
     *          (진행 중에 다시 바뀌면 그 위치에서 새 목표로 바꾼다). 적지 않은 필드, 처음 맞추는 위젯, 한쪽만 정한 필드, `gv_uiReduceMotion` 일 때는 바로 바뀐다.
     *          스타일 계산은 상태가 바뀐 프레임에 한 번만 하고, 그 뒤 프레임마다는 값만 보간한다(필드 종류에 따라 kPaint, kLayout, kTransform).
     *          물려받는 텍스트 필드는 부모의 목표 값을 물려받는다(부모의 보간 값을 자식에게 내리지 않으며, 자식이 자기 전환을 적는다).
     */
    class SW_API UIStyleTransition
    {
    public:
        /**
         * @brief 위젯의 계산된 스타일이 @p pOld 에서 @p pNew 로 바뀌었을 때 부릅니다(바뀐 필드 @p changedFields). 전환을 시작하거나 새 목표로 바꾸거나 지웁니다.
         * @details 스타일 순회(`UIStylePass`)가 새 스타일을 적기 전에 부른다. 지금 보이는 값은 위젯의 전환 상태에서 읽는다.
         */
        static void onStyleChanged( Widget& widget, const UIComputedStyle* pOld, const UIComputedStyle* pNew, uint32 changedFields );
        /** @brief 트리의 전환을 @p deltaSeconds 만큼 진행해 보이는 값을 쓰고 필드 종류에 맞게 무효화합니다. 끝난 위젯은 상태를 지웁니다. 진행한 위젯 수를 반환합니다. */
        static uint32 update( WidgetTree& tree, float32 deltaSeconds );
    };
} // namespace sw
