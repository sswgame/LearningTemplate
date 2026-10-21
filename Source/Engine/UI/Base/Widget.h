/**
 * @file Widget.h
 * @brief 런타임 UI 의 노드 하나를 정의합니다. 이름, 보임, 사용 여부, 불투명도, 렌더 변환, 무효화, 이벤트 가상 함수를 가집니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Base/UIEvents.h"
#include "Engine/UI/Base/WidgetNavigation.h"
#include "Engine/UI/Base/WidgetTypes.h"
#include "Engine/UI/Layout/WidgetLayoutSlot.h"

namespace sw
{
    enum class UIStyleField : uint8;

    struct PropertyInfo;
    struct TypeInfo;
    struct UIComputedStyle;
    struct UILayoutContext;
    struct UIPaintContext;
    struct UIStyleTransitionState;
    struct WidgetPaintCache;

    class CanvasPainter;
    class PanelWidget;
    class WidgetTree;
} // namespace sw

namespace sw
{
    /**
     * @class Widget
     * @brief 런타임 UI 의 노드 하나입니다(언리얼의 SWidget/UWidget, 유니티의 VisualElement, Godot 의 Control 에 해당).
     * @details 수명: 부모 패널이 `unique_ptr` 로 소유한다. 트리 밖에서 위젯을 오래 가리킬 때는 `WidgetID` 를 보관하고 `WidgetTree::findWidgetByID` 로 대상을 찾는다
     *          (포인터는 그 호출 안에서만 쓴다). 값이 바뀌면 `invalidate( 이유 )` 를 부르며, 세터는 값이 같으면 아무것도 하지 않는다(무효화가 쏟아지지 않게 하기 위해서다).
     *          리플렉션 파생 위젯은 `getTypeInfo()` 를 자기 `StaticType()` 으로 재정의한다(`castTo`, 문서, 인스펙터가 이것으로 동적 타입을 안다. RTTI 는 쓰지 않는다).
     *          스레드: 게임 스레드에서만 쓴다. 렌더 스레드는 그리기 목록(패킷)만 본다.
     */
    REFLECT( Abstract )
    class SW_API Widget
    {
    public:
        REFLECT_BODY();

        Widget();
        virtual ~Widget();
        Widget( const Widget& )            = delete;
        Widget& operator=( const Widget& ) = delete;

        /** @brief 동적 리플렉션 타입입니다. 리플렉션 파생 위젯은 자기 `StaticType()` 을 반환하도록 재정의합니다. */
        virtual const TypeInfo* getTypeInfo() const;

        // --- 트리 ---------------------------------------------------------------
        WidgetID             getID() const { return _id; }
        const hashed_string& getName() const { return _name; }
        /** @brief 이름을 바꿉니다. 트리에 붙어 있으면 트리의 이름 인덱스도 고칩니다. */
        void         setName( const hashed_string& name );
        PanelWidget* getParent() const { return _pParent; }
        WidgetTree*  getTree() const { return _pTree; }
        /** @brief 스타일 클래스(공백으로 구분한 이름들)입니다. 바뀌면 kStyle 로 무효화합니다. */
        const string& getStyleClass() const { return _styleClass; }
        void          setStyleClass( const string& styleClass );

        // --- 보임과 상태 -----------------------------------------------------------
        void             setVisibility( WidgetVisibility visibility );
        WidgetVisibility getVisibility() const { return _visibility; }
        /** @brief 보이는지 나타냅니다(Visible, HitTestInvisible, SelfHitTestInvisible). Hidden 은 레이아웃 공간만 차지하고, Collapsed 는 공간도 차지하지 않습니다. */
        bool isVisible() const;
        void setEnabled( bool bEnabled );
        bool isEnabled() const { return _bEnabled; }
        /** @brief 자기와 모든 조상이 켜져 있으면 true 입니다(꺼진 패널 아래는 모두 꺼진 것으로 봅니다). */
        bool                         isEnabledInHierarchy() const;
        void                         setOpacity( float32 opacity );
        float32                      getOpacity() const { return _opacity; }
        void                         setRenderTransform( const WidgetRenderTransform& transform );
        const WidgetRenderTransform& getRenderTransform() const { return _renderTransform; }
        /** @brief 부모가 이 위젯을 배치하는 규칙입니다. 바뀌면 kLayout 입니다. */
        void                    setLayoutSlot( const WidgetLayoutSlot& slot );
        const WidgetLayoutSlot& getLayoutSlot() const { return _slot; }
        /** @brief 흐름 방향입니다. 바뀌면 kArrange 이며, 자기와 자손의 위치를 다시 배치합니다(크기는 그대로). */
        void            setFlowDirection( UIFlowDirection flowDirection );
        UIFlowDirection getFlowDirection() const { return _flowDirection; }
        /**
         * @brief 오른쪽에서 왼쪽으로 배치되는지 반환합니다. 자기 흐름 방향을 부모(루트면 문화권)에 맞춰 해석한 결과이며, 마지막 arrange 가 정합니다(배치되기 전에는 false).
         * @details 이 위젯이 자식을 배치하는 방향(패널의 거울 배치)이자 텍스트 위젯의 문단 방향이다. 자기 슬롯의 여백과 정렬은 부모의 방향을 따른다.
         */
        bool isRightToLeft() const { return _bRightToLeft; }
        /** @brief 방향마다 지정하는 포커스 탐색 규칙입니다. 바뀌어도 무효화하지 않습니다(그리기와 배치에 영향이 없습니다). */
        void                    setNavigation( const WidgetNavigation& navigation ) { _navigation = navigation; }
        const WidgetNavigation& getNavigation() const { return _navigation; }

        // --- 무효화 ---------------------------------------------------------------
        /** @brief 바뀐 이유(WidgetDirty 비트)를 알립니다. 트리에 붙어 있지 않으면 표시만 해 두고, 붙을 때 트리가 모읍니다. */
        void   invalidate( uint32 dirtyReason );
        uint32 getDirtyFlags() const { return _dirtyFlags; }
        /**
         * @brief 크기가 자식에 의존하지 않는 위젯이면 true 입니다(레이아웃 경계). 자식의 kLayout 은 여기서 멈춥니다.
         * @details 기본값은 슬롯의 크기 덮어쓰기가 두 축 모두 있을 때 true 이다. 위젯이 1 만 개여도 텍스트 하나가 바뀐 비용을 국소적으로 만든다.
         */
        virtual bool isLayoutBoundary() const { return _slot.hasFixedSize(); }

        // --- 레이아웃 ---------------------------------------------------------------
        /** @brief 원하는 크기입니다(마지막 measure 결과, UI 단위). */
        const float2&         getDesiredSize() const { return _desiredSize; }
        const WidgetGeometry& getGeometry() const { return _geometry; }
        /** @brief 마지막 배치의 슬롯 위치입니다(부모 로컬, 렌더 변환 전). */
        const float2& getSlotPosition() const { return _lastSlotPosition; }
        /**
         * @brief 배치 결과(레이아웃 사각형과 누적 렌더 변환)를 기록합니다. 레이아웃 순회가 부르며, 레이아웃 없이 고정 배치로 만드는 테스트도 부릅니다.
         * @details 기하가 바뀌면 kPaint 로 무효화한다(같으면 아무것도 하지 않는다).
         */
        void setArrangedGeometry( const WidgetGeometry& geometry );

        // --- 그리기 ---------------------------------------------------------------
        /** @brief 자식을 가지는 패널이면 그 패널, 아니면 nullptr 를 반환합니다. 그리기 순회가 위젯마다 묻는데, `castTo` 의 타입 조회보다 쌉니다. */
        virtual PanelWidget* asPanel() { return nullptr; }
        /** @brief 글리프 아틀라스를 쓰는 위젯(텍스트 위젯)이면 true 입니다. 아틀라스 페이지를 비우면(세대가 오르면) 그리기 캐시를 다시 칠합니다. */
        virtual bool usesGlyphAtlas() const { return false; }
        /**
         * @brief 이 위젯과 자손이 칠하는 사각형을 화면 앵커 @p anchor(`CanvasAnchor` 번호)에 붙입니다. 없으면 invalid_index::kUint32 입니다.
         * @details 화면 마커가 쓴다. 위젯은 앵커 점을 원점으로 배치 · 칠하고, 앵커가 움직여도 다시 배치 · 칠하지 않는다(셰이더가 옮긴다).
         *          앵커 아래에서는 자르기를 쓸 수 없다(가위는 대상 픽셀이라 앵커를 따라가지 않는다). 바뀌면 자기와 자손을 다시 칠한다.
         */
        void   setPaintAnchor( uint32 anchor );
        uint32 getPaintAnchor() const { return _paintAnchor; }

        // --- 바인딩과 애니메이션 ---------------------------------------------------------------
        /**
         * @brief 바인딩이나 애니메이션이 필드 @p property(경로의 맨 위 필드)에 리플렉션으로 값을 썼을 때 부릅니다. 세터를 거치지 않았으므로 그 필드의 세터와 같은 무효화를 합니다.
         * @details 기본 구현은 `Widget` 의 필드(불투명도 → kTransform, 렌더 변환 → kTransform | kArrange, 보임 → kVisibility, 슬롯 → kLayout 등)를 알고, 모르는 필드는
         *          kLayout | kPaint(보수적)로 처리한다. 파생 클래스는 그리기만 바뀌는 필드(색)와 캐시를 가진 필드(텍스트, 범위)를 재정의한다.
         *          필드 종류를 PROPERTY 메타로 적지 않는 이유는 커스텀 메타가 Shipping 에서 지워지기 때문이다.
         */
        virtual void onBoundPropertyChanged( const PropertyInfo& property );
        /** @brief 텍스트 버전(현지화: 언어 변경, 테이블 다시 읽기)이 바뀌었을 때 부릅니다. 텍스트 위젯은 키를 다시 찾고 kLayout 으로 무효화합니다. 기본은 아무것도 하지 않습니다. */
        virtual void onTextRevisionChanged();
        /** @brief 입력 장치 종류(글리프: 키보드, 패드)가 바뀌었을 때 부릅니다. 액션 태그(`[action=이름]`)를 가진 텍스트 위젯은 다시 해석하고 kLayout 으로 무효화합니다. 기본은 아무것도 하지 않습니다. */
        virtual void onInputGlyphsChanged();

        // --- 포커스 ---------------------------------------------------------------
        /** @brief 포커스를 받을 수 있는 종류면 true 입니다(버튼, 슬라이더, 입력 필드). 꺼졌거나 보이지 않으면 받지 않으며, 이는 트리가 따로 확인합니다. */
        virtual bool supportsFocus() const { return false; }
        /** @brief 포커스를 가지면 키보드 글자를 받는지 나타냅니다(텍스트 입력 필드). 그동안 게임은 키를 보지 못합니다. */
        virtual bool supportsTextInput() const { return false; }
        /** @brief 지금 포커스를 가지고 있으면 true 입니다. */
        bool hasFocus() const;

        // --- 이벤트: 기본 구현은 모두 "처리 안 함" ---------------------------------
        /** @brief 포인터 이벤트입니다(히트 테스트 경로, 터널링 다음 버블링). */
        virtual UIReply onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase );
        /** @brief 액션 이벤트입니다(포커스 경로, 터널링 다음 버블링). */
        virtual UIReply onActionEvent( const UIActionEvent& event, UIRoutePhase phase );
        /** @brief 글자 이벤트입니다(포커스 위젯 하나에만 보냅니다). */
        virtual UIReply onTextEvent( const UITextEvent& event );
        /** @brief 포커스를 얻거나 잃었을 때의 알림입니다. 상태 스타일 무효화(kStyle)는 부르는 쪽이 이미 했습니다. */
        virtual void onFocusChanged( bool bFocused );
        /** @brief 포인터가 들어오거나 나갔을 때의 알림입니다. 상태 스타일 무효화(kStyle)는 부르는 쪽이 이미 했습니다. */
        virtual void onHoverChanged( bool bHovered );

        // --- 스타일 ---------------------------------------------------------------
        /** @brief 포인터가 이 위젯(또는 자손) 위에 있으면 true 입니다(호버 경로, 스타일 `:hover`). */
        bool isHovered() const { return _bHovered; }

        // --- 툴팁과 커서 -----------------------------------------------------------
        /**
         * @brief 포인터가 머물면 띄울 설명 글입니다(비면 없음). 바뀌어도 무효화하지 않습니다(이 위젯의 그림이 아니다). 툴팁 서비스가 호버 경로의 가장 가까운 것을 띄웁니다.
         * @details 글은 그대로 보인다 — 현지화는 문서의 키 바인딩(`{loc:…}`)이 이 칸에 쓴다.
         */
        const string& getTooltip() const { return _tooltip; }
        void          setTooltip( const string& tooltip ) { _tooltip = tooltip; }
        /** @brief 포인터가 이 위젯 위에 있을 때의 커서 모양입니다(`Inherit` 면 부모를 따른다). 바뀌어도 무효화하지 않습니다. */
        UICursorShape getCursorShape() const { return _cursorShape; }
        void          setCursorShape( UICursorShape shape ) { _cursorShape = shape; }

        // --- 툴팁과 커서 -----------------------------------------------------------
        /**
         * @brief 포인터가 머물면 띄울 설명 글입니다(비면 없음). 바뀌어도 무효화하지 않습니다(이 위젯의 그림이 아니다). 툴팁 서비스가 호버 경로의 가장 가까운 것을 띄웁니다.
         * @details 글은 그대로 보인다 — 현지화는 문서의 키 바인딩(`{loc:…}`)이 이 칸에 쓴다.
         */
        const string& getTooltip() const { return _tooltip; }
        void          setTooltip( const string& tooltip ) { _tooltip = tooltip; }
        /** @brief 포인터가 이 위젯 위에 있을 때의 커서 모양입니다(`Inherit` 면 부모를 따른다). 바뀌어도 무효화하지 않습니다. */
        UICursorShape getCursorShape() const { return _cursorShape; }
        void          setCursorShape( UICursorShape shape ) { _cursorShape = shape; }
        /**
         * @brief 스타일 선택자가 보는 이 위젯의 상태(`UIStyleState` 비트)입니다. 기본은 호버, 포커스, 꺼짐이고, 누름, 켜짐, 선택은 해당 위젯이 더합니다.
         * @details 상태가 바뀌면 위젯이 `kStyle` 로 무효화한다(스타일 순회가 다시 계산한다).
         */
        virtual uint32 computeStyleStates() const;
        /**
         * @brief 계산된 스타일입니다. 스타일 순회가 정하며, 스타일 시트 규칙과 부모에게서 물려받은 텍스트 필드로 이루어집니다. 순회 전이면 nullptr 입니다.
         * @details 스타일 전환(`_transition`) 중이면 보간 중인 값이다(위젯이 가진 복사본). 전환이 끝나면 다시 공유하는 계산된 스타일을 가리킨다.
         */
        const UIComputedStyle* getComputedStyle() const;
        /**
         * @brief 계산된 스타일이 필드 @p field 를 정하지 않았을 때 이 위젯이 상태 @p styleStates(`UIStyleState` 비트)에서 자기 필드로 그리는 값을 @p outArr 에 읽습니다
         *        (성분 수는 필드가 정합니다). 그런 값이 없으면 false 입니다.
         * @details 스타일 전환이 "한쪽만 정한 필드"(위젯 자기 겉모습 ↔ 시트 값)를 옮기려고 묻습니다. 기본은 불투명도(스타일 값이 곱해지는 값이라 1)만 답합니다.
         *          위젯 타입마다 자기가 그리는 필드(`BorderPanel` 의 배경 브러시, `TextWidget` 의 색 등)를 재정의합니다. 상태별로 달라지는 값(버튼의 호버 · 누름 · 꺼짐 브러시)은
         *          @p styleStates 의 것을 답한다 — 전환은 옛 쪽을 옛 스타일의 상태로, 새 쪽을 새 상태로 묻는다(`_styleStates`).
         */
        [[nodiscard]] virtual bool tryGetOwnStyleField( UIStyleField field, uint32 styleStates, float32 ( &outArr )[4] ) const;
        /** @brief 불투명도에 계산된 스타일의 `_opacity` 를 곱한 값입니다(그리기 순회가 씁니다). */
        float32 computeEffectiveOpacity() const;

    protected:
        /**
         * @brief 원하는 크기를 측정합니다(레이아웃 measure). 패널은 여기서 자식을 `UILayoutPass::measure` 로 측정합니다. 기본은 0 입니다.
         * @param availableSize 슬롯이 줄 수 있는 크기입니다(여백과 덮어쓰기를 적용한 뒤, UI 단위). 축이 `kUIUnbounded` 면 그 축은 원하는 만큼 쓸 수 있습니다.
         */
        virtual float2 computeDesiredSize( const UILayoutContext& context, const float2& availableSize ) const;
        /**
         * @brief 자기 그림을 칠합니다(그리기, `UIPaintPass`). 자식은 트리가 칠합니다. 기본은 아무것도 칠하지 않습니다.
         * @details 그리기 도구의 변환은 이 위젯의 기하다. 로컬 (0, 0) 부터 크기(`getGeometry()._size`, UI 단위)까지 칠한다. 결과는 위젯의 그리기 캐시에 남으며,
         *          그리기 더티(`kPaint`, `kStyle`, 조상의 `kTransform`)가 없으면 다시 부르지 않는다.
         */
        virtual void paint( CanvasPainter& painter, const UIPaintContext& context ) const;
        /** @brief 자식 위에 칠합니다(스크롤 막대, 패널 테두리). 자르기 영역 밖에 그립니다. 기본은 아무것도 칠하지 않습니다. */
        virtual void paintOverChildren( CanvasPainter& painter, const UIPaintContext& context ) const;
        /** @brief 트리에 붙었을 때 부릅니다(바인딩과 애니메이션이 여기서 붙습니다). 자손은 부모 다음에 불립니다. */
        virtual void onAttachedToTree();
        /** @brief 트리에서 떨어지기 직전에 부릅니다(바인딩과 애니메이션을 뗍니다). */
        virtual void onDetachedFromTree();
        /**
         * @brief 사용자 입력이 필드 @p propertyName 의 값을 바꿨음을 화면에 알립니다. 양방향 바인딩이 소스(뷰모델, 설정)에 값을 되씁니다.
         * @details 슬라이더, 체크, 콤보, 텍스트 입력 필드가 사용자 입력 경로에서 부른다(코드가 세터로 바꾼 값은 알리지 않는다). 화면 밖 트리면 아무것도 하지 않는다.
         */
        void notifyValueEdited( const hashed_string& propertyName );

    private:
        friend class PanelWidget;
        friend class WidgetTree;
        friend class UILayoutPass;
        friend class UIPaintPass;
        friend class UIPointerState;
        friend class UIStylePass;
        friend class UIStyleTransition;

        /** @brief 이 위젯과 자손을 @p pTree 에 붙입니다. 번호를 이름에 연결하고 밀려 있던 무효화를 트리에 넘깁니다. */
        void attachToTree( WidgetTree* pTree, PanelWidget* pParent );
        /** @brief 이 위젯과 자손을 트리에서 뗍니다(자손 먼저). 부모 링크는 그대로 둡니다. */
        void detachFromTree();

    private:
        PROPERTY( DisplayName = "Name", Tooltip = "Name used by findWidget, style selectors (#name) and explicit navigation" )
        hashed_string _name;
        PROPERTY( DisplayName = "Style Class", Tooltip = "Space-separated style classes (.primary .danger)", Meta = "NotLocalizable" )
        string _styleClass;
        PROPERTY( DisplayName = "Render Transform" )
        WidgetRenderTransform _renderTransform;
        PROPERTY( DisplayName = "Layout", Tooltip = "How the parent panel places this widget" )
        WidgetLayoutSlot _slot;
        PROPERTY( DisplayName = "Navigation" )
        WidgetNavigation _navigation;
        PROPERTY( DisplayName = "Tooltip", Tooltip = "Text shown when the pointer rests on this widget (empty: none; the nearest ancestor's tooltip shows instead)" )
        string _tooltip;

        unique_ptr<WidgetPaintCache>       _paintCache;       ///< 마지막으로 칠한 사각형(물리 픽셀)입니다. `UIPaintPass` 가 채우며, 처음 칠할 때 만듭니다.
        shared_ptr<const UIComputedStyle>  _computedStyle;    ///< 계산된 스타일입니다(같은 조건의 위젯이 공유하며, `UIStylePass` 가 정합니다).
        unique_ptr<UIStyleTransitionState> _styleTransition;  ///< 스타일 전환 중의 보이는 값입니다(전환이 없으면 비어 있으며, `UIStyleTransition` 이 정합니다).
        uint64                             _styleAncestorKey; ///< 이 위젯에 맞는 "조상 쪽" 선택자 조각의 해시입니다. 바뀌면 자손을 다시 맞춥니다(`UIStylePass`).
        uint32                             _styleStates;      ///< `_computedStyle` 을 맞출 때의 상태(`computeStyleStates`)입니다. 스타일 전환이 옛 쪽 자기 값을 묻는 데 씁니다(`UIStylePass`).

        WidgetGeometry _geometry;          ///< 마지막 arrange 결과입니다.
        float2         _desiredSize;       ///< 마지막 measure 결과입니다.
        float2         _lastAvailableSize; ///< 마지막 measure 의 가용 크기입니다(덮어쓰기 적용 뒤). 캐시 키이자 루트 재측정에 씁니다.
        float2         _lastSlotPosition;  ///< 마지막 arrange 의 슬롯 위치입니다(부모 로컬). 루트 재배치에 씁니다.
        float2         _lastSlotSize;      ///< 마지막 arrange 의 슬롯 크기입니다.
        PanelWidget*   _pParent;           ///< 소유자입니다(구조 링크이며 원시 포인터).
        WidgetTree*    _pTree;             ///< 붙은 트리입니다(떨어져 있으면 nullptr).
        WidgetID       _id;
        uint32         _dirtyFlags;   ///< WidgetDirty 비트입니다.
        uint32         _layoutSerial; ///< 마지막으로 측정한 레이아웃 순회 번호입니다(0 이면 아직 측정한 적 없음).
        uint32         _paintAnchor;  ///< 칠한 사각형을 붙일 화면 앵커 번호입니다(없으면 invalid_index::kUint32, `setPaintAnchor`).

        PROPERTY( DisplayName = "Opacity", Min = 0.0, Max = 1.0 )
        float32 _opacity;
        PROPERTY( DisplayName = "Visibility" )
        WidgetVisibility _visibility;
        PROPERTY( DisplayName = "Flow Direction", Tooltip = "Inherit follows the parent (the culture at the root); fix LeftToRight for numbers and clocks" )
        UIFlowDirection _flowDirection;
        PROPERTY( DisplayName = "Cursor", Tooltip = "Mouse cursor over this widget; Inherit follows the parent (Arrow at the root)" )
        UICursorShape _cursorShape;
        PROPERTY( DisplayName = "Enabled" )
        bool _bEnabled;
        bool _bRightToLeft; ///< 마지막 arrange 에서 해석한 흐름 방향입니다(UILayoutPass 가 기록합니다).
        bool _bHovered;     ///< 호버 경로 안에 있는지 나타냅니다(UIPointerState 가 기록합니다).
    };
} // namespace sw
