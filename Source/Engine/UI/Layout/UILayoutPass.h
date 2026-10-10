/**
 * @file UILayoutPass.h
 * @brief 레이아웃 두 걷기입니다 — measure(아래 → 위, 가용 크기를 받는다) · arrange(위 → 아래, 슬롯을 적용한다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/UI/Base/WidgetTypes.h"
#include "Engine/UI/Layout/WidgetLayoutSlot.h"

namespace sw
{
    struct UIActionGlyphSource;
    struct WidgetGeometry;

    class LocalizationManager;
    class TextLayoutEngine;
    class Widget;
    class WidgetTree;
} // namespace sw

namespace sw
{
    /** @brief 무한 가용 크기입니다 — 그 축으로는 "원하는 만큼" 을 잽니다. 비교는 `UILayoutPass::isUnbounded` 로(여백을 빼도 무한으로 남게). */
    inline constexpr float32 kUIUnbounded = 1.0e9f;

    /**
     * @struct UILayoutContext
     * @brief 레이아웃이 묻는 것 — 글 측정 · 배율 · 뷰포트 · 안전 영역입니다.
     */
    struct UILayoutContext
    {
        TextLayoutEngine*          _pTextLayout{ nullptr };   ///< 글 측정(없으면 글 위젯이 크기 0)
        float4                     _safeInsets{};             ///< 뷰포트 안전 영역(왼 · 위 · 오른 · 아래, UI 단위)
        float2                     _viewportSize{};           ///< 뷰포트 크기(UI 단위) — 트리 루트가 놓이는 사각형
        float32                    _uiScale{ 1.0f };          ///< UI 단위 → 물리 픽셀. 픽셀 맞춤에 쓴다
        float32                    _textScale{ 1.0f };        ///< 글자 크기 배율(gv_uiTextScale) — 글 측정에만 곱한다
        bool                       _bRightToLeft{ false };    ///< 문화권이 오른쪽에서 왼쪽인가(`UILayoutPass::isCultureRightToLeft`) — 루트의 Inherit 이 따른다
        const LocalizationManager* _pLocalization{ nullptr }; ///< 글 위젯이 키를 푸는 문화권(없으면 글 그대로)
        uint32                     _textRevision{ 0 };        ///< 그 문화권의 글 판(`getTextRevision`) — 글 위젯의 풀이 캐시 열쇠
        const UIActionGlyphSource* _pActionGlyphs{ nullptr }; ///< `[action=이름]` 태그의 글리프 출처(없으면 태그를 풀지 않는다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class UILayoutPass
     * @brief 트리의 레이아웃을 돌립니다 — 더러운 뿌리만 measure → arrange 합니다(언리얼 Slate · WPF · Yoga 의 두 걷기).
     * @details measure 는 가용 크기를 받습니다(WPF · Yoga 와 같다 — 높이가 너비에 기대는 줄 바꿈 글이 같은 프레임에 맞는다. Slate 는 지난 프레임의 너비를 쓴다).
     *          결과는 위젯에 캐시되고(같은 가용 크기 · 더럽지 않으면 다시 재지 않는다), arrange 는 기하가 바뀐 위젯만 그리기 더러움으로 만듭니다.
     *          회전 · 기울임이 없는 축은 물리 픽셀에 맞춥니다(Slate 의 픽셀 스냅 — 1 px 테두리가 번지지 않는다). 게임 스레드만.
     */
    class SW_API UILayoutPass
    {
    public:
        /**
         * @brief 트리의 더러운 뿌리만 다시 잽니다. 루트는 뷰포트 전체에, 다른 뿌리(레이아웃 경계 · 배치만 더러운 위젯)는 지난 arrange 의 자리에 다시 놓습니다.
         * @details 배율 · 글자 배율이 지난 걷기와 다르면 트리 전체를 다시 잽니다. 뷰포트 크기가 바뀌면 루트부터 다시 잽니다.
         * @return 이번에 다시 잰 위젯 수입니다(`computeDesiredSize` 호출 수 — 프로파일 카운터 UI.LayoutWidgets).
         */
        static uint32 update( WidgetTree& tree, const UILayoutContext& context );
        /**
         * @brief 위젯 하나를 가용 크기로 잽니다. 더럽지 않고 같은 가용 크기면 캐시를 돌려줍니다.
         * @details 슬롯 크기 덮어쓰기 · 최소 · 최대를 적용한 값입니다(여백은 넣지 않는다 — 부모가 더한다). Collapsed 면 0 입니다.
         */
        static float2 measure( Widget& widget, const UILayoutContext& context, const float2& availableSize );
        /**
         * @brief 위젯을 부모 로컬 사각형(@p slotPosition · @p slotSize)에 놓습니다 — 슬롯 여백 · 정렬을 적용하고, 패널이면 자식을 놓습니다.
         * @param parentGeometry 부모의 기하(루트면 뷰포트 사각형). 자식 기하 = 부모 기하 ∘ 자리 이동 ∘ 자식 렌더 변환.
         */
        static void arrange( Widget& widget, const UILayoutContext& context, const WidgetGeometry& parentGeometry, const float2& slotPosition,
                             const float2& slotSize );
        /** @brief 흐름 방향 @p flowDirection 을 부모의 방향(@p bParentRightToLeft — 루트면 문화권)으로 풉니다. */
        static bool resolveRightToLeft( UIFlowDirection flowDirection, bool bParentRightToLeft )
        {
            return flowDirection == UIFlowDirection::Inherit ? bParentRightToLeft : flowDirection == UIFlowDirection::RightToLeft;
        }
        /** @brief 문화권이 오른쪽에서 왼쪽인가입니다(`UILayoutContext::_bRightToLeft` 를 채운다). @p pLocalization 이 nullptr 이면 바인딩된 엔진 서비스, 그것도 없으면 false. */
        static bool isCultureRightToLeft( const LocalizationManager* pLocalization = nullptr );
        /** @brief 트리의 모든 위젯을 레이아웃 더러움으로 표시합니다(배율 · 글자 배율 · 글꼴이 바뀜). */
        static void invalidateAllLayout( WidgetTree& tree );

        /** @brief 길이가 무한 가용 크기인가(무한에서 여백을 뺀 값도 무한이다). */
        static bool isUnbounded( float32 length ) { return length >= kUIUnbounded * 0.5f; }
        /** @brief 가용 길이에서 @p used 를 뺍니다. 무한은 무한으로 남고, 음수는 0 입니다. */
        static float32 computeRemaining( float32 available, float32 used );
        /**
         * @brief 슬롯 사각형 안에 위젯 사각형을 정합니다(여백 · 정렬 · 크기 덮어쓰기 · 최대).
         * @param desired 위젯의 원하는 크기(measure 결과). Fill 정렬이면 쓰지 않는다.
         */
        static void applySlot( const WidgetLayoutSlot& slot, const float2& desired, const float2& slotPosition, const float2& slotSize, float2& outPosition,
                               float2& outSize );
        /** @brief 정렬이 남은 공간 @p freeSpace 에서 앞쪽으로 띄우는 길이입니다(Fill · Start 0, Center 절반, End 전부). */
        static float32 computeAlignmentOffset( UIAlignment alignment, float32 freeSpace );

        // 주축 · 교차축 도우미(상자 · 흐름 패널) — 가로면 주축이 x 입니다.
        static float32 getMainAxis( UIOrientation orientation, const float2& value ) { return orientation == UIOrientation::Horizontal ? value._x : value._y; }
        static float32 getCrossAxis( UIOrientation orientation, const float2& value ) { return orientation == UIOrientation::Horizontal ? value._y : value._x; }
        static float2  makeAxisVector( UIOrientation orientation, float32 main, float32 cross )
        {
            return orientation == UIOrientation::Horizontal ? float2{ main, cross } : float2{ cross, main };
        }
        /** @brief 여백(왼 · 위 · 오른 · 아래)의 주축 합입니다. */
        static float32 getMainPadding( UIOrientation orientation, const float4& padding )
        {
            return orientation == UIOrientation::Horizontal ? padding._x + padding._z : padding._y + padding._w;
        }
        /** @brief 여백의 교차축 합입니다. */
        static float32 getCrossPadding( UIOrientation orientation, const float4& padding )
        {
            return orientation == UIOrientation::Horizontal ? padding._y + padding._w : padding._x + padding._z;
        }

    private:
        /** @brief 이번 걷기에서 다시 재야 하는가(잰 적 없음 · 레이아웃 더러움이 이번 걷기 전의 것). */
        static bool isMeasureStale( const Widget& widget );
        /** @brief 레이아웃 비트를 지웁니다 — 비트가 있는 위젯만 따라 내려갑니다. Collapsed 는 내려가지 않고 kChildLayout 을 남깁니다(다시 보일 때 그 아래를 다시 재게). */
        static void clearLayoutFlags( Widget& widget );
        /** @brief 트리 루트를 뷰포트 전체에 잽니다 · 놓습니다. */
        static void layoutTreeRoot( Widget& root, const UILayoutContext& context );
    };
} // namespace sw
