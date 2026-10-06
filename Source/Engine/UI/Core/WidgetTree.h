/**
 * @file WidgetTree.h
 * @brief 위젯 트리 하나입니다 — 루트 · 번호표 · 이름표 · 더러운 목록 · 이 트리의 포커스 위젯.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/Core/WidgetTypes.h"

namespace sw
{
    class UiFocusManager;
    class UiScreen;

    /** @brief 위젯이 낸 명령(버튼의 `_command`)을 받는 함수입니다 — 화면(`UiScreen`)이 자기 트리에 겁니다. */
    using UiCommandDelegate = Delegate<void( const hashed_string&, Widget& )>;

    /**
     * @class WidgetTree
     * @brief 루트 위젯 하나와 그 아래 모든 위젯의 번호표 · 이름표, 무효화 목록을 듭니다(언리얼 UWidgetTree · 유니티 패널).
     * @details 화면(`UiScreen`) 하나가 트리 하나를 가집니다. 무효화는 이유별 목록으로 모입니다 — 레이아웃은 부모 쪽으로 레이아웃 경계까지 올라가 그
     *          "다시 잴 뿌리" 만 적고(`getLayoutDirtyRoots`), 그리기 · 스타일은 위젯 자신만 적습니다. 목록은 번호를 듭니다 — 그 사이 떨어진 위젯은 걷는 쪽이 건너뜁니다.
     *          포커스: 포커스 관리자(`UiFocusManager`)가 이 트리에 포커스를 두면 그 위젯 번호가 여기 적힙니다. 그 위젯이 떨어지면 트리가 포커스를 풀고,
     *          트리가 지워지면 관리자에게 알립니다.
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

        /** @brief 이 트리를 소유한 화면입니다(화면 밖의 트리 — 시험 · 월드 위젯 — 면 nullptr). 팝업을 여는 위젯이 그 화면의 UI 시스템을 찾는다. */
        UiScreen* getScreen() const { return _pScreen; }

        /** @brief 이 트리에서 포커스를 쥔 위젯입니다(포커스가 다른 트리에 있거나 없으면 무효). */
        WidgetId getFocusedWidget() const { return _focusedWidget; }

        /** @brief 뿌리부터 모든 위젯을 깊이 우선 문서 순서(부모 다음 자식, 자식은 앞에서부터)로 @p outListWidget 에 담습니다. */
        void collectWidgetsInDocumentOrder( vector<Widget*>& outListWidget ) const;

        /** @brief 위젯 명령을 받을 함수를 겁니다(화면이 만들 때 건다). 비우면 명령은 버려집니다. */
        void setCommandHandler( const UiCommandDelegate& handler ) { _commandHandler = handler; }
        /** @brief 위젯 @p source 가 명령 @p command 를 냅니다(버튼 클릭 · 확인). 받을 함수가 없으면 아무것도 하지 않습니다. */
        void dispatchCommand( const hashed_string& command, Widget& source );

    private:
        friend class Widget;
        friend class UiFocusManager;
        friend class UiLayoutPass;
        friend class UiPaintPass;
        friend class UiScreen;

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
        UiCommandDelegate                                              _commandHandler; ///< 위젯 명령을 받는 함수(화면이 건다)
        UiFocusManager*                                                _pFocusManager;  ///< 지금 이 트리에 포커스를 둔 관리자(없으면 nullptr — 트리가 지워질 때 알린다)
        UiScreen*                                                      _pScreen;        ///< 소유한 화면(UiScreen 생성자가 적는다)
        WidgetId                                                       _focusedWidget;
        float32                                                        _layoutUiScale;        ///< 지난 레이아웃 걷기의 UI 배율(바뀌면 전체 다시 — UiLayoutPass)
        float32                                                        _layoutTextScale;      ///< 지난 레이아웃 걷기의 글자 배율
        float4                                                         _layoutSafeInsets;     ///< 지난 레이아웃 걷기의 안전 영역(바뀌면 루트부터 다시 놓는다)
        float32                                                        _paintUiScale;         ///< 지난 그리기 걷기의 UI 배율(바뀌면 모든 그림 캐시를 다시 — 픽셀이 바뀐다)
        uint32                                                         _paintAtlasGeneration; ///< 지난 그리기 걷기의 글리프 아틀라스 세대(바뀌면 글 위젯을 다시)
    };
} // namespace sw
