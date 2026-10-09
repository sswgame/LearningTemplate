/**
 * @file UiStyleTransition.h
 * @brief 스타일 전환(transition) — 계산된 스타일이 바뀔 때 `_transition` 에 적힌 칸만 지금 값에서 새 값으로 보간합니다(유니티 USS · CSS `transition`).
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
     * @struct UiStyleTransitionSpec
     * @brief `WidgetStyle::_transition` 글을 읽은 것 — 칸마다 길이 · 곡선입니다.
     * @details 형식은 `"_backgroundColor 0.12 EaseOut, _opacity 0.2"` — 쉼표로 나눈 항목마다 칸 이름(또는 `all` — 보간되는 모든 칸) · 길이(초) · 곡선(`BlendCurve`
     *          이름, 없으면 `EaseOut`). 뒤 항목이 앞 항목을 덮습니다. 보간되는 칸은 실수 칸(색 · 모서리 · 두께 · 그림자 · 여백 · 글자 크기 · 외곽선 · 불투명도)이고
     *          글꼴 · 전환 자신은 받지 않습니다(로드 오류).
     */
    struct SW_API UiStyleTransitionSpec
    {
        struct Entry
        {
            UiStyleField _field{ UiStyleField::Count };
            float32      _duration{ 0.0f };
            BlendCurve   _curve{ BlendCurve::EaseOut };
        };

        vector<Entry> _listEntry{}; ///< 칸마다 하나(칸 순서)

        /** @brief 칸 @p field 의 항목입니다. 없으면 nullptr 입니다. */
        const Entry* findEntry( UiStyleField field ) const;

        /** @brief 글 @p text 를 읽습니다. 모르는 칸 · 보간할 수 없는 칸 · 읽지 못한 길이 · 모르는 곡선이면 false 이고 @p outError 에 이유(영어)를 둡니다. */
        [[nodiscard]] static bool parse( string_view text, UiStyleTransitionSpec& outSpec, string& outError );
        /** @brief 칸 @p field 가 보간되는 칸(실수 성분)이면 true 입니다. */
        static bool isInterpolable( UiStyleField field );
    };
} // namespace sw

namespace sw
{
    /**
     * @struct UiStyleTransitionState
     * @brief 전환 중인 위젯 하나의 상태 — 보이는 계산된 스타일(목표 + 칸마다 보간 값)과 칸마다 진행입니다. 위젯이 소유하고, 전환이 끝나면 지웁니다.
     */
    struct SW_API UiStyleTransitionState
    {
        struct Channel
        {
            float32      _arrFrom[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            float32      _arrTo[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
            float32      _elapsed{ 0.0f };
            float32      _duration{ 0.0f };
            UiStyleField _field{ UiStyleField::Count };
            BlendCurve   _curve{ BlendCurve::EaseOut };
        };

        UiComputedStyle _shown{};       ///< 위젯이 읽는 값(`Widget::getComputedStyle`)
        vector<Channel> _listChannel{}; ///< 도는 칸
    };
} // namespace sw

namespace sw
{
    /**
     * @class UiStyleTransition
     * @brief 스타일 걷기가 부르는 전환 시작과, 애니메이션 단계가 부르는 진행입니다.
     * @details 시작 규칙(CSS 와 같다): 새 계산된 스타일의 `_transition` 이 그 칸을 적었고, 옛 · 새 스타일 둘 다 그 칸을 정했으면 **지금 보이는 값**에서 새 값으로
     *          (진행 중에 다시 바뀌면 그 자리에서 새 목표로). 적지 않은 칸 · 처음 맞추는 위젯 · 한쪽만 정한 칸 · `gv_uiReduceMotion` 은 바로 바뀝니다.
     *          스타일 계산은 상태가 바뀐 프레임 한 번이고, 그 뒤 프레임마다는 값만 보간합니다(칸 종류대로 kPaint · kLayout · kTransform).
     *          물려받는 글 칸은 부모의 **목표** 값을 물려받습니다(부모의 보간 값을 자식에 내리지 않는다 — 자식이 자기 전환을 적는다).
     */
    class SW_API UiStyleTransition
    {
    public:
        /**
         * @brief 위젯의 계산된 스타일이 @p pOld 에서 @p pNew 로 바뀌었다(바뀐 칸 @p changedFields). 전환을 시작 · 다시 겨누거나 지웁니다.
         * @details 스타일 걷기(`UiStylePass`)가 새 스타일을 적기 전에 부릅니다 — 지금 보이는 값은 위젯의 전환 상태에서 읽습니다.
         */
        static void onStyleChanged( Widget& widget, const UiComputedStyle* pOld, const UiComputedStyle* pNew, uint32 changedFields );
        /** @brief 트리의 전환을 @p deltaSeconds 만큼 진행해 보이는 값을 쓰고 칸 종류대로 무효화합니다. 끝난 위젯은 상태를 지웁니다. 진행한 위젯 수입니다. */
        static uint32 update( WidgetTree& tree, float32 deltaSeconds );
    };
} // namespace sw
