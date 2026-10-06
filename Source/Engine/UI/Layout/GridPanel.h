/**
 * @file GridPanel.h
 * @brief 열 · 행 트랙으로 자식을 칸에 놓는 패널입니다(UMG GridPanel · Godot GridContainer · CSS Grid 의 단순판).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Core/PanelWidget.h"

namespace sw
{
    /** @brief 격자 트랙(열 · 행 하나)의 크기 규칙입니다. */
    ENUM()
    enum class UiGridTrackKind : uint8
    {
        Auto,  ///< 그 트랙에만 든(넓이 1) 자식들의 원하는 크기 최대
        Fixed, ///< `_value` UI 단위
        Fill   ///< 남은 길이를 `_value` 무게로 나눈다
    };
} // namespace sw

namespace sw
{
    /** @struct UiGridTrack @brief 격자의 열 · 행 하나입니다. */
    REFLECT()
    struct SW_API UiGridTrack
    {
        REFLECT_BODY();

        PROPERTY( DisplayName = "Kind" )
        UiGridTrackKind _kind{ UiGridTrackKind::Fill };
        PROPERTY( DisplayName = "Value", Tooltip = "Fixed: size in ui units. Fill: weight." )
        float32 _value{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class GridPanel
     * @brief 자식 슬롯의 행 · 열 · 넓이(`_row` · `_column` · `_rowSpan` · `_columnSpan`)로 칸에 놓습니다.
     * @details 트랙 크기: Fixed = 값, Auto = 넓이 1 인 자식들의 (원하는 크기 + 여백) 최대, Fill = 남은 길이를 무게로. 넓이 2 이상 자식은 덮은 트랙 합이
     *          모자라면 **모자란 만큼을 덮은 Auto 트랙들에 고르게** 더합니다(CSS Grid 의 완전한 알고리즘은 아니다). 열을 먼저 정하고, 자식을 그 열 너비로
     *          다시 재서 행을 정합니다(줄 바꿈 글이 칸 너비로 높이를 정한다). 트랙 정의가 없으면 자식들의 최대 행 · 열 + 1 개의 Fill 트랙입니다.
     *          트랙 밖 행 · 열 번호는 경고 한 번 + 마지막 트랙으로 놓습니다. 원하는 크기에서 Fill 트랙은 내용 크기(Auto 처럼)입니다.
     */
    REFLECT( Category = "Layout", DisplayName = "Grid Panel", Tooltip = "Places children in cells of column and row tracks" )
    class SW_API GridPanel : public PanelWidget
    {
    public:
        REFLECT_BODY();

        GridPanel();
        ~GridPanel() override;

        const TypeInfo* getTypeInfo() const override;

        const vector<UiGridTrack>& getColumns() const { return _listColumn; }
        /** @brief 열 트랙을 바꿉니다. kLayout. */
        void                       setColumns( const vector<UiGridTrack>& listColumn );
        const vector<UiGridTrack>& getRows() const { return _listRow; }
        /** @brief 행 트랙을 바꿉니다. kLayout. */
        void          setRows( const vector<UiGridTrack>& listRow );
        const float2& getCellSpacing() const { return _cellSpacing; }
        /** @brief 칸 사이 간격(열 · 행, UI 단위)을 바꿉니다. kLayout. */
        void setCellSpacing( const float2& cellSpacing );
        /** @brief 마지막 measure 에서 트랙 밖이라 마지막 트랙으로 옮긴 자식 수입니다(시험 · 진단). */
        uint32 getOutOfRangeCellCount() const { return _outOfRangeCellCount; }

    protected:
        float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UiLayoutContext& context, const float2& size ) override;

    private:
        PROPERTY( DisplayName = "Columns" )
        vector<UiGridTrack> _listColumn;
        PROPERTY( DisplayName = "Rows" )
        vector<UiGridTrack> _listRow;
        PROPERTY( DisplayName = "Cell Spacing", Meta = "Units=ui" )
        float2 _cellSpacing;

        mutable uint32 _outOfRangeCellCount; ///< 마지막 measure 의 트랙 밖 자식 수
        mutable bool   _bWarnedOutOfRange;   ///< 트랙 밖 경고를 이미 남겼다(패널마다 한 번)
    };
} // namespace sw
