/**
 * @file WidgetTree.h
 * @brief 위젯 트리 하나입니다 — 루트 · 번호표 · 이름표 · 더러운 목록 · 이 트리의 포커스 위젯.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Core/WidgetTypes.h"

namespace sw
{
    class UiFocusManager;

    /**
     * @class WidgetTree
     * @brief 루트 위젯 하나와 그 아래 모든 위젯의 번호표 · 이름표, 무효화 목록을 듭니다(언리얼 UWidgetTree · 유니티 패널).
     * @details 화면(`UiScreen`) 하나가 트리 하나를 가집니다. 무효화는 이유별 목록으로 모입니다 — 레이아웃은 부모 쪽으로 레이아웃 경계까지 올라가 그
     *          "다시 잴 뿌리" 만 적고(`getLayoutDirtyRoots`), 그리기 · 스타일은 위젯 자신만 적습니다. 목록은 번호를 듭니다 — 그 사이 떨어진 위젯은 걷는 쪽이 건너뜁니다.
     *          포커스: 트리마다 포커스 위젯이 하나 있고(덮인 화면이면 다시 맨 위가 될 때 돌려줄 위젯), 포커스 관리자(`UiFocusManager`)가 옮깁니다.
     *          그 위젯이 떨어지면 트리가 포커스를 풉니다.
     */
    class SW_API WidgetTree
    {
    public:
        WidgetTree();
        ~WidgetTree();
        WidgetTree( const WidgetTree& )            = delete;
        WidgetTree& operator=( const WidgetTree& ) = delete;

        /** @brief 루트를 바꿉니다(옛 루트는 떨어져 지워진다). 새 루트 전체가 레이아웃 · 그리기 · 스타일 더러움으로 붙습니다. */
        void    setRoot( unique_ptr<Widget> root );
        Widget* getRoot() const { return _root.get(); }

        /** @brief 이름으로 찾습니다(같은 이름이 둘이면 먼저 붙은 것 — 붙일 때 경고 한 번). */
        Widget* findWidgetByName( const hashed_string& name ) const;
        /** @brief 이름으로 찾고 @p WidgetType 으로 내립니다. 없거나 그 타입이 아니면 nullptr 입니다. */
        template <typename WidgetType>
        WidgetType* findWidget( const hashed_string& name ) const
        {
            Widget* pWidget = findWidgetByName( name );
            return pWidget != nullptr ? castTo<WidgetType>( pWidget ) : nullptr;
        }
        /** @brief 번호로 찾습니다. 떨어졌거나 지워졌으면 nullptr 입니다. */
        Widget* findWidgetById( WidgetId id ) const;
        uint32  getWidgetCount() const { return static_cast<uint32>( _mapIdToWidget.size() ); }

        /** @brief 위젯이 무효화를 알립니다(`Widget::invalidate` 만 부른다). */
        void notifyDirty( Widget& widget, uint32 dirtyReason );
        /** @brief 무효화 뒤 처리할 것(레이아웃 · 그리기 · 스타일)이 있는가. 없으면 프레임이 트리를 건너뜁니다. */
        bool hasPendingWork() const;
        /** @brief 다시 잴 뿌리(레이아웃 경계 · 루트)의 번호입니다 — 레이아웃 걷기가 읽고 비웁니다. */
        const vector<WidgetId>& getLayoutDirtyRoots() const { return _listLayoutDirtyRoot; }
        /** @brief 그림(또는 변환 · 불투명도)이 바뀐 위젯 번호입니다 — 그리기 걷기가 읽고 비웁니다. */
        const vector<WidgetId>& getPaintDirtyWidgets() const { return _listPaintDirty; }
        /** @brief 계산된 스타일을 다시 할 위젯 번호입니다 — 스타일 걷기가 읽고 비웁니다. */
        const vector<WidgetId>& getStyleDirtyWidgets() const { return _listStyleDirty; }
        /**
         * @brief 모든 무효화를 처리한 것으로 표시합니다 — 위젯의 더러움 비트와 세 목록을 비웁니다.
         * @details 걷는 쪽(레이아웃 · 스타일 · 그리기)이 할 일을 마친 뒤 부릅니다. 걷기가 아직 없는 단계에서는 프레임 끝에 부릅니다.
         */
        void clearAllDirty();

        /** @brief 이 트리의 포커스 위젯입니다(없으면 무효). 화면이 덮여 있으면 다시 맨 위가 될 때 돌려줄 위젯입니다. */
        WidgetId getFocusedWidget() const { return _focusedWidget; }

        /** @brief 뿌리부터 모든 위젯을 깊이 우선 문서 순서(부모 다음 자식, 자식은 앞에서부터)로 @p outListWidget 에 담습니다. */
        void collectWidgetsInDocumentOrder( vector<Widget*>& outListWidget ) const;

    private:
        friend class Widget;
        friend class UiFocusManager;
        friend class UiLayoutPass;
        friend class UiPaintPass;

        /** @brief 번호 · 이름을 올립니다(`Widget::attachToTree` 가 부른다). */
        void registerWidget( Widget& widget );
        /** @brief 번호 · 이름을 내립니다(`Widget::detachFromTree` 가 부른다). 이 트리의 포커스 위젯이면 포커스를 풉니다. */
        void unregisterWidget( Widget& widget );
        /** @brief 이름표에 올립니다. 같은 이름이 이미 있으면 경고하고 먼저 것을 둡니다. */
        void registerName( Widget& widget );
        /** @brief 이름표에서 내립니다(이 위젯이 그 이름의 주인일 때만). */
        void unregisterName( Widget& widget );
        /** @brief @p widget 을 다시 잴 뿌리로 적습니다(이미 적혔으면 아무것도 하지 않는다). */
        void addLayoutRoot( Widget& widget );

    private:
        unique_ptr<Widget>                                             _root;
        unordered_map<WidgetId, Widget*>                               _mapIdToWidget;
        unordered_map<hashed_string, Widget*, hashed_string::HashFunc> _mapNameToWidget;
        vector<WidgetId>                                               _listLayoutDirtyRoot; ///< kLayout 이 올라가다 멈춘 자리(레이아웃 경계 · 루트)
        vector<WidgetId>                                               _listPaintDirty;
        vector<WidgetId>                                               _listStyleDirty;
        WidgetId                                                       _focusedWidget;
    };
} // namespace sw
