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
     * @details 형식은 `"_backgroundColor 0.12 EaseOut 0.05, _opacity 0.2"` 이다. 쉼표로 나눈 항목마다 필드 이름(또는 보간되는 모든 필드를 뜻하는 `all`), 길이(초), 곡선(`BlendCurve`
     *          이름이며 없으면 `EaseOut`), 지연(초, `transition-delay` — 시작을 늦춘다. 그동안은 옛 값에 머문다)을 적는다. 길이 다음에 숫자가 바로 오면 곡선 없이 지연이다(`"_opacity 0.2 0.1"`,
     *          CSS 가 시간 둘을 길이, 지연 순으로 읽는 것과 같다). 뒤 항목이 앞 항목을 덮어쓴다. 보간되는 필드는 실수 필드(색, 모서리, 두께, 그림자, 여백, 글자 크기, 외곽선, 불투명도)이고
     *          폰트와 전환 자신은 받지 않는다(로드 오류).
     */
    struct SW_API UIStyleTransitionSpec
    {
        struct Entry
        {
            UIStyleField _field{ UIStyleField::Count };
            float32      _duration{ 0.0f };
            BlendCurve   _curve{ BlendCurve::EaseOut };
            float32      _delay{ 0.0f }; ///< 시작을 늦추는 시간(초)입니다.
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
            float32      _delay{ 0.0f }; ///< 이 시간이 지나야 보간이 시작합니다(`_elapsed` 가 지연 + 길이에 닿으면 끝).
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
     *          (진행 중에 다시 바뀌면 그 위치에서 새 목표로 바꾼다). 적지 않은 필드, 처음 맞추는 위젯, `gv_uiReduceMotion` 일 때는 바로 바뀐다.
     *          스타일 계산은 상태가 바뀐 프레임에 한 번만 하고, 그 뒤 프레임마다는 값만 보간한다(필드 종류에 따라 kPaint, kLayout, kTransform).
     *          **한쪽만 정한 필드.** 옛 스타일과 새 스타일 중 한쪽만 그 필드를 정했으면 정하지 않은 쪽 값은 위젯이 자기 필드로 그리던 값이다(`Widget::tryGetOwnStyleField`) —
     *          그 값을 읽을 수 있는 위젯(`BorderPanel`, `TextWidget`)은 그 값과 시트 값 사이를 옮긴다. 읽지 못하는 위젯은 바로 바뀐다.
     *          **물려받는 텍스트 필드.** 공유하는 계산된 스타일은 부모의 목표 값을 물려받는다(보간 값을 내리면 공유 키가 프레임마다 바뀐다). 그 대신 부모가 그 필드를 옮기는 중이고 자식의 새 값이 부모의 목표와 같으며
     *          자식이 그 필드에 자기 전환을 적지 않았으면, 자식도 부모와 같은 출발값 · 길이 · 곡선 · 지연 · 진행으로 채널을 만든다(같은 시간표라 같은 값을 보인다 — CSS 에서 물려받은 값이 부모의 보간을 따라가는 것과 같다).
     */
    class SW_API UIStyleTransition
    {
    public:
        /**
         * @brief 위젯의 계산된 스타일이 @p pOld 에서 @p pNew 로 바뀌었을 때 부릅니다(바뀐 필드 @p changedFields). 전환을 시작하거나 새 목표로 바꾸거나 지웁니다.
         * @details 스타일 순회(`UIStylePass`)가 새 스타일을 적기 전에 부른다. 지금 보이는 값은 위젯의 전환 상태에서 읽는다.
         *          @p oldStates · @p newStates 는 두 스타일을 맞춘 위젯 상태(`UIStyleState`)다 — 한쪽만 정한 필드의 자기 값을 그 상태로 묻는다(버튼의 상태 브러시).
         */
        static void onStyleChanged( Widget& widget, const UIComputedStyle* pOld, const UIComputedStyle* pNew, uint32 changedFields, uint32 oldStates, uint32 newStates );
        /** @brief 트리의 전환을 @p deltaSeconds 만큼 진행해 보이는 값을 쓰고 필드 종류에 맞게 무효화합니다. 끝난 위젯은 상태를 지웁니다. 진행한 위젯 수를 반환합니다. */
        static uint32 update( WidgetTree& tree, float32 deltaSeconds );

    private:
        /**
         * @brief 부모가 필드 @p field 를 옮기는 중이고 자식의 새 값(@p newArr)이 그 목표와 같으면 부모의 채널을 @p outChannel 에 복사합니다(물려받은 값은 부모의 시간표를 따른다).
         * @details 부모의 채널이 방금 만들어졌든(같은 스타일 패스) 진행 중이든 같은 출발값 · 지연 · 진행을 그대로 가져와 두 위젯이 같은 값을 보이게 한다.
         */
        static bool findParentChannel( const Widget& widget, UIStyleField field, const float32 ( &newArr )[4], UIStyleTransitionState::Channel& outChannel );
        /**
         * @brief 바뀐 필드 하나의 새 채널을 만듭니다. 목표값은 새 스타일이 정했으면 그 값, 아니면 위젯이 자기 필드로 그리는 값입니다.
         * @param inoutFrom 출발값입니다. @p bHaveFrom 이 참이면(진행 중이던 채널의 지금 값) 그대로 쓰고, 아니면 옛 스타일 · 위젯의 옛 값으로 채웁니다.
         * @return 새 채널이 필요 없으면 false 입니다(전환을 적지 않은 필드 · 읽지 못하는 위젯 · 출발값과 목표가 같음 — 바로 바뀐다).
         */
        [[nodiscard]] static bool makeChannel( Widget& widget, const UIComputedStyle& oldStyle, const UIComputedStyle& newStyle, UIStyleField field, const UIStyleTransitionSpec& spec,
                                               uint32 oldStates, uint32 newStates, bool bHaveFrom, float32 ( &inoutFrom )[4], UIStyleTransitionState::Channel& outChannel );
        /** @brief 채널이 남은 상태의 보이는 값을 새 스타일에서 다시 만들고(채널의 지금 값을 얹는다), 새로 들어온 위젯이면 트리의 전환 목록에 올립니다. */
        static void refreshShownStyle( Widget& widget, UIStyleTransitionState& state, const UIComputedStyle& newStyle );
    };
} // namespace sw
