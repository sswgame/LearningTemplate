/**
 * @file WidgetTree.h
 * @brief 위젯 트리 하나를 정의합니다. 루트, 위젯 번호, 이름, 더티 목록, 이 트리의 포커스 위젯을 관리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTypes.h"

namespace sw
{
    struct CanvasDrawList;

    class UIFocusManager;
    class UIScreen;

    /**
     * @class WidgetTree
     * @brief 루트 위젯 하나와 그 아래 모든 위젯의 번호, 이름, 무효화 목록을 가집니다(언리얼의 UWidgetTree, 유니티의 패널에 해당).
     * @details 화면(`UIScreen`) 하나가 트리 하나를 가진다. 무효화는 이유별 목록으로 모인다. 레이아웃은 부모 쪽으로 레이아웃 경계까지 올라가 그
     *          "다시 측정할 루트" 만 적고(`getLayoutDirtyRoots`), 그리기와 스타일은 위젯 자신만 적는다. 목록은 번호를 가지므로, 그 사이 떨어진 위젯은 순회하는 쪽이 건너뛴다.
     *          포커스: 포커스 관리자(`UIFocusManager`)가 이 트리에 포커스를 두면 그 위젯 번호가 여기에 적힌다. 그 위젯이 떨어지면 트리가 포커스를 해제하고,
     *          트리가 지워지면 관리자에게 알린다.
     */
    class SW_API WidgetTree
    {
    public:
        WidgetTree();
        ~WidgetTree();
        WidgetTree( const WidgetTree& )            = delete;
        WidgetTree& operator=( const WidgetTree& ) = delete;

        /** @brief 루트를 바꿉니다(기존 루트는 떼어 지웁니다). 새 루트 전체가 레이아웃, 그리기, 스타일 더티로 붙습니다. */
        void    setRoot( unique_ptr<Widget> root );
        Widget* getRoot() const { return _root.get(); }

        /** @brief 이름으로 찾습니다(같은 이름이 둘이면 먼저 붙은 것이며, 붙일 때 경고를 한 번 남깁니다). */
        Widget* findWidgetByName( const hashed_string& name ) const;
        /** @brief 이름으로 찾고 @p WidgetType 으로 내립니다. 없거나 그 타입이 아니면 nullptr 입니다. */
        template <typename WidgetType>
        WidgetType* findWidget( const hashed_string& name ) const
        {
            Widget* pWidget = findWidgetByName( name );
            return pWidget != nullptr ? castTo<WidgetType>( pWidget ) : nullptr;
        }
        /** @brief 번호로 찾습니다. 떨어졌거나 지워졌으면 nullptr 입니다. */
        Widget* findWidgetByID( WidgetID id ) const;
        uint32  getWidgetCount() const { return static_cast<uint32>( _mapIDToWidget.size() ); }

        /** @brief 위젯이 무효화를 알립니다(`Widget::invalidate` 만 부릅니다). */
        void notifyDirty( Widget& widget, uint32 dirtyReason );
        /** @brief 무효화 뒤 처리할 것(레이아웃, 그리기, 스타일)이 있으면 true 입니다. 없으면 프레임이 트리를 건너뜁니다. */
        bool hasPendingWork() const;
        /** @brief 다시 측정할 루트(레이아웃 경계, 루트)의 번호입니다. 레이아웃 순회가 읽고 비웁니다. */
        const vector<WidgetID>& getLayoutDirtyRoots() const { return _listLayoutDirtyRoot; }
        /** @brief 그림(또는 변환, 불투명도)이 바뀐 위젯 번호입니다. 그리기 순회가 읽고 비웁니다. */
        const vector<WidgetID>& getPaintDirtyWidgets() const { return _listPaintDirty; }
        /** @brief 계산된 스타일을 다시 구할 위젯 번호입니다. 스타일 순회가 읽고 비웁니다. */
        const vector<WidgetID>& getStyleDirtyWidgets() const { return _listStyleDirty; }
        /**
         * @brief 모든 무효화를 처리한 것으로 표시합니다. 위젯의 더티 비트와 세 목록을 비웁니다.
         * @details 순회하는 쪽(레이아웃, 스타일, 그리기)이 할 일을 마친 뒤 부른다. 순회가 아직 없는 단계에서는 프레임 끝에 부른다.
         */
        void clearAllDirty();

        /** @brief 이 트리를 소유한 화면입니다(화면 밖의 트리, 곧 테스트나 월드 위젯이면 nullptr). 팝업을 여는 위젯이 그 화면의 UI 시스템을 찾는 데 씁니다. */
        UIScreen* getScreen() const { return _pScreen; }

        /** @brief 이 트리에서 포커스를 가진 위젯입니다(포커스가 다른 트리에 있거나 없으면 무효). */
        WidgetID getFocusedWidget() const { return _focusedWidget; }

        /** @brief 루트부터 모든 위젯을 깊이 우선 문서 순서(부모 다음 자식, 자식은 앞에서부터)로 @p outListWidget 에 담습니다. */
        void collectWidgetsInDocumentOrder( vector<Widget*>& outListWidget ) const;

    private:
        friend class Widget;
        friend class UIFocusManager;
        friend class UILayoutPass;
        friend class UIPaintPass;
        friend class UIScreen;
        friend class UIStylePass;
        friend class UIStyleTransition;

        /** @brief 번호와 이름을 등록합니다(`Widget::attachToTree` 가 부릅니다). */
        void registerWidget( Widget& widget );
        /** @brief 번호와 이름을 제거합니다(`Widget::detachFromTree` 가 부릅니다). 이 트리의 포커스 위젯이면 포커스를 해제합니다. */
        void unregisterWidget( Widget& widget );
        /** @brief 이름을 등록합니다. 같은 이름이 이미 있으면 경고하고 먼저 등록된 것을 둡니다. */
        void registerName( Widget& widget );
        /** @brief 이름을 제거합니다(이 위젯이 그 이름의 주인일 때만). */
        void unregisterName( Widget& widget );
        /** @brief @p widget 을 다시 측정할 루트로 적습니다(이미 적혀 있으면 아무것도 하지 않습니다). */
        void addLayoutRoot( Widget& widget );

    private:
        unique_ptr<Widget>                                             _root;
        unordered_map<WidgetID, Widget*>                               _mapIDToWidget;
        unordered_map<hashed_string, Widget*, hashed_string::HashFunc> _mapNameToWidget;
        vector<WidgetID>                                               _listLayoutDirtyRoot; ///< kLayout 이 올라가다 멈춘 위치입니다(레이아웃 경계, 루트).
        vector<WidgetID>                                               _listPaintDirty;
        vector<WidgetID>                                               _listStyleDirty;
        unordered_set<WidgetID>                                        _uniqueStyleTransition; ///< 스타일 전환 중인 위젯(`UIStyleTransition` 이 진행한다). 집합이라 같은 위젯이 두 번 들어오지 않고 순서는 상관없다.
        UIFocusManager*                                                _pFocusManager;         ///< 지금 이 트리에 포커스를 둔 관리자입니다(없으면 nullptr 이며, 트리가 지워질 때 알립니다).
        UIScreen*                                                      _pScreen;               ///< 소유한 화면입니다(UIScreen 생성자가 적습니다).
        WidgetID                                                       _focusedWidget;
        float32                                                        _layoutUIScale;        ///< 지난 레이아웃 순회의 UI 배율입니다(바뀌면 전체를 다시 처리합니다. UILayoutPass).
        float32                                                        _layoutTextScale;      ///< 지난 레이아웃 순회의 글자 배율입니다.
        float4                                                         _layoutSafeInsets;     ///< 지난 레이아웃 순회의 안전 영역입니다(바뀌면 루트부터 다시 배치합니다).
        float32                                                        _paintUIScale;         ///< 지난 그리기 순회의 UI 배율입니다(바뀌면 픽셀이 바뀌므로 모든 그리기 캐시를 다시 만듭니다).
        uint32                                                         _paintAtlasGeneration; ///< 지난 그리기 순회의 글리프 아틀라스 세대입니다(바뀌면 텍스트 위젯을 다시 칠합니다).
        unique_ptr<CanvasDrawList>                                     _paintOutput;          ///< 지난 그리기 순회가 이어 붙인 트리 전체의 목록입니다(바뀐 것이 없으면 순회하지 않고 이것을 냅니다).
        uint8                                                          _bPaintOutputStale;    ///< 지난 순회 뒤 무효화가 하나라도 있었는지 나타냅니다(구조, 레이아웃, 스타일, 그리기 무엇이든).
    };
} // namespace sw
