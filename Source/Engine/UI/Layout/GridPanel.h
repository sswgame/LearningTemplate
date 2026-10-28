/**
 * @file GridPanel.h
 * @brief 열과 행 트랙으로 자식을 격자 칸에 놓는 패널입니다(UMG 의 GridPanel, Godot 의 GridContainer, CSS Grid 의 단순화한 버전).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"

namespace sw
{
    /** @brief 격자 트랙(열 또는 행 하나)의 크기 규칙입니다. */
    ENUM()
    enum class UIGridTrackKind : uint8
    {
        Auto,  ///< 그 트랙에만 걸친(넓이 1) 자식들의 원하는 크기 중 최댓값입니다.
        Fixed, ///< `_value` UI 단위입니다.
        Fill   ///< 남은 길이를 `_value` 가중치로 나눕니다.
    };
} // namespace sw

namespace sw
{
    /** @struct UIGridTrack @brief 격자의 열 또는 행 하나입니다. */
    REFLECT()
    struct SW_API UIGridTrack
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Kind" )
        UIGridTrackKind _kind{ UIGridTrackKind::Fill };
        PROPERTY( DisplayName = "Value", Tooltip = "Fixed: size in ui units. Fill: weight." )
        float32 _value{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GridPanel
     * @brief 자식 슬롯의 행, 열, 넓이(`_row`, `_column`, `_rowSpan`, `_columnSpan`)로 칸에 배치합니다.
     * @details 트랙 크기는 Fixed 면 값, Auto 면 넓이 1 인 자식들의 (원하는 크기 + 여백) 최댓값, Fill 이면 남은 길이를 가중치로 나눈 값이다.
     *          넓이가 2 이상인 자식은 덮은 트랙의 합이 모자라면, 모자란 만큼을 덮은 Auto 트랙들에 고르게 더한다(CSS Grid 의 완전한 알고리즘은 아니다).
     *          열을 먼저 정하고, 자식을 그 열 너비로 다시 측정해서 행을 정한다(줄 바꿈 텍스트가 칸 너비로 높이를 정한다).
     *          트랙 정의가 없으면 자식들의 최대 행, 열 + 1 개의 Fill 트랙을 쓴다.
     *          트랙 밖의 행과 열 번호는 경고를 한 번 남기고 마지막 트랙에 놓는다. 원하는 크기를 구할 때 Fill 트랙은 Auto 처럼 내용 크기를 쓴다.
     */
    REFLECT( Category = "Layout", DisplayName = "Grid Panel", Tooltip = "Places children in cells of column and row tracks" )
    class SW_API GridPanel : public PanelWidget
    {
    public:
        REFLECT_BODY();

        GridPanel();
        ~GridPanel() override;

        const TypeInfo* getTypeInfo() const override;

        const vector<UIGridTrack>& getColumns() const { return _listColumn; }
        /** @brief 열 트랙을 바꿉니다. kLayout 입니다. */
        void                       setColumns( const vector<UIGridTrack>& listColumn );
        const vector<UIGridTrack>& getRows() const { return _listRow; }
        /** @brief 행 트랙을 바꿉니다. kLayout 입니다. */
        void          setRows( const vector<UIGridTrack>& listRow );
        const float2& getCellSpacing() const { return _cellSpacing; }
        /** @brief 칸 사이 간격(열, 행, UI 단위)을 바꿉니다. kLayout 입니다. */
        void setCellSpacing( const float2& cellSpacing );
        /** @brief 마지막 measure 에서 트랙 밖이라 마지막 트랙으로 옮긴 자식 수입니다(테스트와 진단용). */
        uint32 getOutOfRangeCellCount() const { return _outOfRangeCellCount; }
        /**
         * @brief 마지막 arrange 의 열 너비 · 행 높이입니다(UI 단위, 트랙 순서). 에디터의 칸 손잡이가 놓은 점의 칸을 고릅니다.
         * @details 자식 사각형으로 줄을 거꾸로 추정하면 걸친 칸(span)이 섞이고 자식 없는 줄을 고를 수 없어, 배치가 정한 값을 그대로 낸다.
         *          줄 시작은 왼쪽 위에서 앞 줄들의 합 + 간격이다(RTL 거울은 열 순서만 뒤집는다 — `PanelWidget::arrangeChild`).
         */
        const vector<float32>& getArrangedColumnSizes() const { return _listArrangedColumnSize; }
        const vector<float32>& getArrangedRowSizes() const { return _listArrangedRowSize; }

    protected:
        float2 computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UILayoutContext& context, const float2& size ) override;

    private:
        PROPERTY( DisplayName = "Columns" )
        vector<UIGridTrack> _listColumn;
        PROPERTY( DisplayName = "Rows" )
        vector<UIGridTrack> _listRow;
        PROPERTY( DisplayName = "Cell Spacing", Meta = "Units=ui" )
        float2 _cellSpacing;

        vector<float32> _listArrangedColumnSize; ///< 마지막 arrange 의 열 너비입니다.
        vector<float32> _listArrangedRowSize;    ///< 마지막 arrange 의 행 높이입니다.

        mutable uint32 _outOfRangeCellCount; ///< 마지막 measure 에서 트랙 밖에 있던 자식 수입니다.
        mutable bool   _bWarnedOutOfRange;   ///< 트랙 밖 경고를 이미 남겼는지 나타냅니다(패널마다 한 번).
    };
} // namespace sw
