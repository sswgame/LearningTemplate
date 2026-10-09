#include "pch.h"

#include "Engine/UI/Layout/GridPanel.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/UI/Layout/UiLayoutPass.h"

namespace sw
{
    SW_LOG_CALLER( "GridPanel" );

    namespace
    {
        /** @brief 자식 하나가 덮는 칸입니다(트랙 범위로 묶은 뒤). */
        struct GridPlacement
        {
            Widget* _pChild{ nullptr };
            uint32  _column{ 0 };
            uint32  _row{ 0 };
            uint32  _columnSpan{ 1 };
            uint32  _rowSpan{ 1 };
        };

        struct GridPanelInternal
        {
            /** @brief 트랙 정의가 없을 때 쓸 트랙 수 — 자식들의 최대 (행 · 열 + 넓이), 적어도 1 입니다. */
            static void countTracks( const PanelWidget& panel, uint32& outColumnCount, uint32& outRowCount )
            {
                outColumnCount = 1;
                outRowCount    = 1;
                for ( uint32 index = 0; index < panel.getChildCount(); ++index )
                {
                    const WidgetLayoutSlot& slot = panel.getChild( index )->getLayoutSlot();
                    outColumnCount               = MathUtil::max( outColumnCount, static_cast<uint32>( slot._column ) + MathUtil::max<uint32>( 1u, slot._columnSpan ) );
                    outRowCount                  = MathUtil::max( outRowCount, static_cast<uint32>( slot._row ) + MathUtil::max<uint32>( 1u, slot._rowSpan ) );
                }
            }

            /** @brief 앞 @p count 개 트랙과 그 뒤 간격까지의 길이 — 트랙 @p count 의 시작 자리입니다. */
            static float32 computeTrackOffset( const vector<float32>& listSize, uint32 count, float32 spacing )
            {
                float32 offset = spacing * static_cast<float32>( count );
                for ( uint32 index = 0; index < count; ++index )
                {
                    offset += listSize[index];
                }
                return offset;
            }

            /** @brief 트랙 정의가 비었으면 @p count 개의 Fill 트랙을, 아니면 정의를 그대로 씁니다. */
            static void makeTracks( const vector<UiGridTrack>& listDefined, uint32 count, vector<UiGridTrack>& outListTrack )
            {
                if ( listDefined.empty() == false )
                {
                    outListTrack = listDefined;
                    return;
                }
                outListTrack.assign( count, UiGridTrack{} );
            }

            /** @brief 한 축의 칸 시작 · 넓이를 트랙 수 안으로 묶습니다. 밖이면 true(마지막 트랙으로 옮겼다). */
            static bool clampSpan( uint32 trackCount, uint32& inoutStart, uint32& inoutSpan )
            {
                const bool bOutOfRange = trackCount <= inoutStart;
                if ( bOutOfRange )
                    inoutStart = trackCount - 1;
                inoutSpan = MathUtil::clamp( inoutSpan, 1u, trackCount - inoutStart );
                return bOutOfRange;
            }

            /** @brief 덮은 트랙 길이 합 + 사이 간격입니다. */
            static float32 sumSpan( const vector<float32>& listSize, uint32 start, uint32 span, float32 spacing )
            {
                float32 total = spacing * static_cast<float32>( span - 1 );
                for ( uint32 index = start; index < start + span; ++index )
                {
                    total += listSize[index];
                }
                return total;
            }

            /** @brief 덮은 트랙이 모두 Fixed 면 그 합(가용 길이로 쓴다), 아니면 무한입니다. */
            static float32 computeFixedSpan( const vector<UiGridTrack>& listTrack, uint32 start, uint32 span, float32 spacing )
            {
                float32 total = spacing * static_cast<float32>( span - 1 );
                for ( uint32 index = start; index < start + span; ++index )
                {
                    if ( listTrack[index]._kind != UiGridTrackKind::Fixed )
                        return kUiUnbounded;
                    total += listTrack[index]._value;
                }
                return total;
            }

            /**
             * @brief 한 축의 트랙 길이를 정합니다.
             * @param listExtent 자리마다 그 축의 (원하는 크기 + 여백).
             * @param bShareFill true 면 Fill 트랙이 남은 길이를 무게로 나눈다(가용 길이가 무한이면 내용 크기). false 면 Fill 도 내용 크기(원하는 크기 계산).
             */
            static void resolveTracks( const vector<UiGridTrack>& listTrack, float32 available, float32 spacing, const vector<GridPlacement>& listPlacement,
                                       const vector<float32>& listExtent, bool bColumn, bool bShareFill, vector<float32>& outListSize )
            {
                const uint32 trackCount = static_cast<uint32>( listTrack.size() );
                outListSize.assign( trackCount, 0.0f );
                for ( uint32 index = 0; index < trackCount; ++index )
                {
                    if ( listTrack[index]._kind == UiGridTrackKind::Fixed )
                        outListSize[index] = MathUtil::max( 0.0f, listTrack[index]._value );
                }
                // 1) 넓이 1 자식 — Auto · Fill 트랙의 내용 크기.
                for ( uint32 index = 0; index < static_cast<uint32>( listPlacement.size() ); ++index )
                {
                    const GridPlacement& placement = listPlacement[index];
                    const uint32         start     = bColumn ? placement._column : placement._row;
                    const uint32         span      = bColumn ? placement._columnSpan : placement._rowSpan;
                    if ( span != 1 || listTrack[start]._kind == UiGridTrackKind::Fixed )
                        continue;
                    outListSize[start] = MathUtil::max( outListSize[start], listExtent[index] );
                }
                // 2) 넓이 2 이상 — 모자란 만큼을 덮은 Auto 트랙들에 고르게.
                for ( uint32 index = 0; index < static_cast<uint32>( listPlacement.size() ); ++index )
                {
                    const GridPlacement& placement = listPlacement[index];
                    const uint32         start     = bColumn ? placement._column : placement._row;
                    const uint32         span      = bColumn ? placement._columnSpan : placement._rowSpan;
                    if ( span < 2 )
                        continue;
                    const float32 shortfall = listExtent[index] - sumSpan( outListSize, start, span, spacing );
                    uint32        autoCount = 0;
                    for ( uint32 track = start; track < start + span; ++track )
                    {
                        if ( listTrack[track]._kind == UiGridTrackKind::Auto )
                            ++autoCount;
                    }
                    if ( shortfall <= 0.0f || autoCount == 0 )
                        continue;
                    const float32 share = shortfall / static_cast<float32>( autoCount );
                    for ( uint32 track = start; track < start + span; ++track )
                    {
                        if ( listTrack[track]._kind == UiGridTrackKind::Auto )
                            outListSize[track] += share;
                    }
                }
                // 3) Fill — 남은 길이를 무게로.
                if ( bShareFill == false || UiLayoutPass::isUnbounded( available ) )
                    return;
                float32 used        = spacing * static_cast<float32>( trackCount > 0 ? trackCount - 1 : 0 );
                float32 totalWeight = 0.0f;
                for ( uint32 index = 0; index < trackCount; ++index )
                {
                    if ( listTrack[index]._kind == UiGridTrackKind::Fill )
                        totalWeight += MathUtil::max( 0.0f, listTrack[index]._value );
                    else
                        used += outListSize[index];
                }
                const float32 remaining = MathUtil::max( 0.0f, available - used );
                for ( uint32 index = 0; index < trackCount; ++index )
                {
                    if ( listTrack[index]._kind != UiGridTrackKind::Fill )
                        continue;
                    outListSize[index] = totalWeight > 0.0f ? remaining * MathUtil::max( 0.0f, listTrack[index]._value ) / totalWeight : 0.0f;
                }
            }

            /** @brief 트랙 길이 합 + 간격입니다. */
            static float32 sumAll( const vector<float32>& listSize, float32 spacing )
            {
                return listSize.empty() ? 0.0f : sumSpan( listSize, 0, static_cast<uint32>( listSize.size() ), spacing );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    GridPanel::GridPanel()
        : PanelWidget{}
        , _listColumn{}
        , _listRow{}
        , _cellSpacing{}
        , _outOfRangeCellCount{ 0 }
        , _bWarnedOutOfRange{ false }
    {
    }

    GridPanel::~GridPanel() = default;

    const TypeInfo* GridPanel::getTypeInfo() const
    {
        return StaticType();
    }

    void GridPanel::setColumns( const vector<UiGridTrack>& listColumn )
    {
        _listColumn = listColumn;
        invalidate( WidgetDirty::kLayout );
    }

    void GridPanel::setRows( const vector<UiGridTrack>& listRow )
    {
        _listRow = listRow;
        invalidate( WidgetDirty::kLayout );
    }

    void GridPanel::setCellSpacing( const float2& cellSpacing )
    {
        if ( _cellSpacing == cellSpacing )
            return;
        _cellSpacing = cellSpacing;
        invalidate( WidgetDirty::kLayout );
    }

    float2 GridPanel::computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const
    {
        uint32 columnCount = 0;
        uint32 rowCount    = 0;
        GridPanelInternal::countTracks( *this, columnCount, rowCount );
        vector<UiGridTrack> listColumnTrack;
        vector<UiGridTrack> listRowTrack;
        GridPanelInternal::makeTracks( _listColumn, columnCount, listColumnTrack );
        GridPanelInternal::makeTracks( _listRow, rowCount, listRowTrack );

        vector<GridPlacement> listPlacement;
        _outOfRangeCellCount = 0;
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            Widget* const pChild = getChild( index );
            if ( pChild->getVisibility() == WidgetVisibility::Collapsed )
                continue;
            const WidgetLayoutSlot& slot = pChild->getLayoutSlot();
            GridPlacement           placement{ pChild, slot._column, slot._row, slot._columnSpan, slot._rowSpan };
            const bool              bColumnOut = GridPanelInternal::clampSpan( static_cast<uint32>( listColumnTrack.size() ), placement._column, placement._columnSpan );
            const bool              bRowOut    = GridPanelInternal::clampSpan( static_cast<uint32>( listRowTrack.size() ), placement._row, placement._rowSpan );
            if ( bColumnOut || bRowOut )
                ++_outOfRangeCellCount;
            listPlacement.push_back( placement );
        }
        if ( _outOfRangeCellCount > 0 && _bWarnedOutOfRange == false )
        {
            _bWarnedOutOfRange = true;
            SW_LOG_WARNING( "[Ui] Grid panel '%#' has %# children outside its tracks - they are placed in the last track", getName().c_str(), _outOfRangeCellCount );
        }

        // 1) 열: 덮은 트랙이 모두 Fixed 면 그 너비, 아니면 무한으로 잰다.
        vector<float32> listExtent( listPlacement.size(), 0.0f );
        for ( uint32 index = 0; index < static_cast<uint32>( listPlacement.size() ); ++index )
        {
            const GridPlacement& placement       = listPlacement[index];
            const float4&        padding         = placement._pChild->getLayoutSlot()._padding;
            const float32        padX            = padding._x + padding._z;
            const float32        padY            = padding._y + padding._w;
            const float32        columnAvailable = GridPanelInternal::computeFixedSpan( listColumnTrack, placement._column, placement._columnSpan, _cellSpacing._x );
            const float32        rowAvailable    = GridPanelInternal::computeFixedSpan( listRowTrack, placement._row, placement._rowSpan, _cellSpacing._y );
            const float2         desired         = UiLayoutPass::measure( *placement._pChild, context,
                                                                          float2{ UiLayoutPass::computeRemaining( columnAvailable, padX ), UiLayoutPass::computeRemaining( rowAvailable, padY ) } );
            listExtent[index]                    = desired._x + padX;
        }
        vector<float32> listColumnSize;
        GridPanelInternal::resolveTracks( listColumnTrack, availableSize._x, _cellSpacing._x, listPlacement, listExtent, true, true, listColumnSize );
        vector<float32> listColumnContent;
        GridPanelInternal::resolveTracks( listColumnTrack, availableSize._x, _cellSpacing._x, listPlacement, listExtent, true, false, listColumnContent );

        // 2) 행: 정한 열 너비로 다시 잰다(줄 바꿈 글의 높이).
        for ( uint32 index = 0; index < static_cast<uint32>( listPlacement.size() ); ++index )
        {
            const GridPlacement& placement    = listPlacement[index];
            const float4&        padding      = placement._pChild->getLayoutSlot()._padding;
            const float32        padX         = padding._x + padding._z;
            const float32        padY         = padding._y + padding._w;
            const float32        columnWidth  = GridPanelInternal::sumSpan( listColumnSize, placement._column, placement._columnSpan, _cellSpacing._x );
            const float32        rowAvailable = GridPanelInternal::computeFixedSpan( listRowTrack, placement._row, placement._rowSpan, _cellSpacing._y );
            const float32        width        = UiLayoutPass::isUnbounded( availableSize._x ) ? kUiUnbounded : columnWidth;
            const float2         desired      = UiLayoutPass::measure( *placement._pChild, context,
                                                                       float2{ UiLayoutPass::computeRemaining( width, padX ), UiLayoutPass::computeRemaining( rowAvailable, padY ) } );
            listExtent[index]                 = desired._y + padY;
        }
        vector<float32> listRowContent;
        GridPanelInternal::resolveTracks( listRowTrack, availableSize._y, _cellSpacing._y, listPlacement, listExtent, false, false, listRowContent );
        return float2{ GridPanelInternal::sumAll( listColumnContent, _cellSpacing._x ), GridPanelInternal::sumAll( listRowContent, _cellSpacing._y ) };
    }

    void GridPanel::arrangeChildren( const UiLayoutContext& context, const float2& size )
    {
        uint32 columnCount = 0;
        uint32 rowCount    = 0;
        GridPanelInternal::countTracks( *this, columnCount, rowCount );
        vector<UiGridTrack> listColumnTrack;
        vector<UiGridTrack> listRowTrack;
        GridPanelInternal::makeTracks( _listColumn, columnCount, listColumnTrack );
        GridPanelInternal::makeTracks( _listRow, rowCount, listRowTrack );

        vector<GridPlacement> listPlacement;
        vector<float32>       listExtentX;
        vector<float32>       listExtentY;
        for ( uint32 index = 0; index < getChildCount(); ++index )
        {
            Widget* const pChild = getChild( index );
            if ( pChild->getVisibility() == WidgetVisibility::Collapsed )
                continue;
            const WidgetLayoutSlot& slot = pChild->getLayoutSlot();
            GridPlacement           placement{ pChild, slot._column, slot._row, slot._columnSpan, slot._rowSpan };
            (void)GridPanelInternal::clampSpan( static_cast<uint32>( listColumnTrack.size() ), placement._column, placement._columnSpan );
            (void)GridPanelInternal::clampSpan( static_cast<uint32>( listRowTrack.size() ), placement._row, placement._rowSpan );
            listPlacement.push_back( placement );
            listExtentX.push_back( pChild->getDesiredSize()._x + slot._padding._x + slot._padding._z );
            listExtentY.push_back( pChild->getDesiredSize()._y + slot._padding._y + slot._padding._w );
        }
        vector<float32> listColumnSize;
        vector<float32> listRowSize;
        GridPanelInternal::resolveTracks( listColumnTrack, size._x, _cellSpacing._x, listPlacement, listExtentX, true, true, listColumnSize );
        GridPanelInternal::resolveTracks( listRowTrack, size._y, _cellSpacing._y, listPlacement, listExtentY, false, true, listRowSize );

        for ( const GridPlacement& placement : listPlacement )
        {
            const float32 x      = GridPanelInternal::computeTrackOffset( listColumnSize, placement._column, _cellSpacing._x );
            const float32 y      = GridPanelInternal::computeTrackOffset( listRowSize, placement._row, _cellSpacing._y );
            const float32 width  = GridPanelInternal::sumSpan( listColumnSize, placement._column, placement._columnSpan, _cellSpacing._x );
            const float32 height = GridPanelInternal::sumSpan( listRowSize, placement._row, placement._rowSpan, _cellSpacing._y );
            arrangeChild( context, *placement._pChild, float2{ x, y }, float2{ width, height } );
        }
    }
} // namespace sw
