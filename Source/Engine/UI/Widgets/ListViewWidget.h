/**
 * @file ListViewWidget.h
 * @brief 항목이 아주 많은 세로 목록입니다 — 보이는 줄만 위젯을 만들어 다시 씁니다(UMG ListView · 유니티 ListView · Godot ItemList 의 가상화).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Core/PanelWidget.h"

namespace sw
{
    /**
     * @class ListViewWidget
     * @brief 줄 높이가 고정인 가상 목록입니다. 보이는 줄 수 + 1 만큼만 줄 위젯을 만들고(`setRowFactory`), 항목 k 는 늘 줄 위젯 k % 줄 수가 맡습니다
     *        (스크롤이 한 줄 내려가면 한 위젯만 다시 묶는다 — `setRowBinder`). 그래서 보이는 동안 항목과 위젯이 바뀌지 않아 포커스가 항목을 따라간다.
     * @details 휠(버블)과 포커스 탐색(`scrollIntoView` — 줄이 맡은 항목이 보이게)으로 스크롤하고 자식을 자릅니다. 원하는 크기 높이 = 항목 수 × 줄 높이
     *          (부모가 Fill 로 줄인다). 항목이 줄 수보다 적으면 남는 줄 위젯은 접는다(Collapsed). 줄 위젯은 이 패널이 만들고 소유한다 — 밖에서 붙이지 않는다.
     */
    REFLECT( Category = "UI", DisplayName = "List View", Tooltip = "Virtualized vertical list: only visible rows have widgets" )
    class SW_API ListViewWidget : public PanelWidget
    {
    public:
        REFLECT_BODY();

        /** @brief 줄 위젯 하나를 만듭니다(보이는 줄 수가 늘 때만 불린다). */
        using RowFactoryDelegate = Delegate<unique_ptr<Widget>()>;
        /** @brief 줄 위젯 @p row 를 항목 @p itemIndex 로 채웁니다(그 줄이 다른 항목을 맡게 될 때만 불린다). */
        using RowBinderDelegate = Delegate<void( Widget& row, uint32 itemIndex )>;

        ListViewWidget();
        ~ListViewWidget() override;

        const TypeInfo* getTypeInfo() const override;

        void setRowFactory( RowFactoryDelegate factory ) { _rowFactory = std::move( factory ); }
        void setRowBinder( RowBinderDelegate binder ) { _rowBinder = std::move( binder ); }
        /** @brief 항목 수를 바꿉니다 — 모든 줄을 다시 묶는다. kLayout. */
        void   setItemCount( uint32 itemCount );
        uint32 getItemCount() const { return _itemCount; }
        /** @brief 줄 높이(UI 단위)를 바꿉니다. kLayout. */
        void    setRowHeight( float32 rowHeight );
        float32 getRowHeight() const { return _rowHeight; }

        float32 getScrollOffset() const { return _scrollOffset; }
        /** @brief 오프셋을 바꿉니다([0, 항목 × 줄 높이 − 보이는 높이]). 바뀌면 kArrange. */
        void setScrollOffset( float32 scrollOffset );
        /** @brief 항목 @p itemIndex 가 보이도록 최소한만 옮깁니다. */
        void scrollToItem( uint32 itemIndex );
        /** @brief 줄 위젯 @p row 가 지금 맡은 항목입니다(없으면 `invalid_index::kUint32`). */
        uint32 findItemIndex( const Widget& row ) const;
        /** @brief 지금까지 만든 줄 위젯 수입니다(시험 — 보이는 줄 + 1 을 넘지 않는다). */
        uint32 getCreatedRowCount() const { return static_cast<uint32>( _listRowItem.size() ); }
        /** @brief 지금까지 줄을 다시 묶은 수입니다(시험). */
        uint32 getBindCount() const { return _bindCount; }

        bool canScrollIntoView() const override { return true; }
        bool scrollIntoView( const Widget& widget ) override;

        UiReply onPointerEvent( const UiPointerEvent& event, UiRoutePhase phase ) override;

    protected:
        float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const override;
        void   arrangeChildren( const UiLayoutContext& context, const float2& size ) override;

    private:
        /** @brief 보이는 줄 수 + 1 개까지 줄 위젯을 만듭니다(레이아웃 안 — 새 줄은 곧바로 놓인다). */
        void    ensureRowCount( uint32 rowCount );
        float32 getMaxScrollOffset() const;

    private:
        RowFactoryDelegate _rowFactory;
        RowBinderDelegate  _rowBinder;
        vector<uint32>     _listRowItem; ///< 줄 위젯 자리마다 지금 맡은 항목(없으면 invalid)
        float32            _scrollOffset;
        float32            _viewportHeight; ///< 지난 배치의 보이는 높이
        uint32             _itemCount;
        uint32             _bindCount;
        PROPERTY( DisplayName = "Row Height", Min = 1.0, Meta = "Units=ui" )
        float32 _rowHeight;
        PROPERTY( DisplayName = "Wheel Step", Min = 0.0, Meta = "Units=ui" )
        float32 _wheelStep;
    };
} // namespace sw
