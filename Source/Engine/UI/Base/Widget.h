/**
 * @file Widget.h
 * @brief 런타임 UI 의 노드 하나입니다 — 이름 · 보임 · 사용 · 불투명도 · 렌더 변환 · 무효화 · 사건 가상 함수.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Base/UiEvents.h"
#include "Engine/UI/Base/WidgetNavigation.h"
#include "Engine/UI/Base/WidgetTypes.h"
#include "Engine/UI/Layout/WidgetLayoutSlot.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;
    struct UiComputedStyle;
    struct UiLayoutContext;
    struct UiPaintContext;
    struct UiStyleTransitionState;
    struct WidgetPaintCache;

    class CanvasPainter;
    class PanelWidget;
    class WidgetTree;
} // namespace sw

namespace sw
{
    /**
     * @class Widget
     * @brief 런타임 UI 의 노드 하나입니다(언리얼 SWidget/UWidget · 유니티 VisualElement · Godot Control 자리).
     * @details 수명: 부모 패널이 `unique_ptr` 로 소유합니다. 트리 밖에서 위젯을 오래 가리킬 때는 `WidgetID` 로 들고 `WidgetTree::findWidgetByID` 로 풉니다
     *          (포인터는 그 호출 안에서만). 값이 바뀌면 `invalidate( 이유 )` — 세터는 값이 같으면 아무것도 하지 않습니다(무효화가 쏟아지지 않게).
     *          리플렉션 파생 위젯은 `getTypeInfo()` 를 자기 `StaticType()` 으로 덮어씁니다(`castTo` · 문서 · 인스펙터가 동적 타입을 이것으로 안다 — RTTI 없음).
     *          스레드: 게임 스레드만. 렌더 스레드는 그리기 목록(패킷)만 봅니다.
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

        /** @brief 동적 리플렉션 타입입니다. 리플렉션 파생 위젯은 자기 `StaticType()` 을 돌려주도록 덮어씁니다. */
        virtual const TypeInfo* getTypeInfo() const;

        // --- 트리 ---------------------------------------------------------------
        WidgetID             getID() const { return _id; }
        const hashed_string& getName() const { return _name; }
        /** @brief 이름을 바꿉니다. 트리에 붙어 있으면 이름표도 고칩니다. */
        void         setName( const hashed_string& name );
        PanelWidget* getParent() const { return _pParent; }
        WidgetTree*  getTree() const { return _pTree; }
        /** @brief 스타일 클래스(공백으로 나눈 이름들)입니다. 바뀌면 kStyle. */
        const string& getStyleClass() const { return _styleClass; }
        void          setStyleClass( const string& styleClass );

        // --- 보임 · 상태 -----------------------------------------------------------
        void             setVisibility( WidgetVisibility visibility );
        WidgetVisibility getVisibility() const { return _visibility; }
        /** @brief 보이는가(Visible · HitTestInvisible · SelfHitTestInvisible). Hidden 은 자리만, Collapsed 는 자리도 없습니다. */
        bool isVisible() const;
        void setEnabled( bool bEnabled );
        bool isEnabled() const { return _bEnabled; }
        /** @brief 자기와 모든 조상이 켜져 있으면 true 입니다(끈 패널 아래는 모두 꺼진 것으로 본다). */
        bool                         isEnabledInHierarchy() const;
        void                         setOpacity( float32 opacity );
        float32                      getOpacity() const { return _opacity; }
        void                         setRenderTransform( const WidgetRenderTransform& transform );
        const WidgetRenderTransform& getRenderTransform() const { return _renderTransform; }
        /** @brief 부모가 이 위젯을 놓는 규칙입니다. 바뀌면 kLayout. */
        void                    setLayoutSlot( const WidgetLayoutSlot& slot );
        const WidgetLayoutSlot& getLayoutSlot() const { return _slot; }
        /** @brief 흐름 방향입니다. 바뀌면 kArrange — 자기와 자손의 자리를 다시 놓는다(크기는 그대로). */
        void            setFlowDirection( UiFlowDirection flowDirection );
        UiFlowDirection getFlowDirection() const { return _flowDirection; }
        /**
         * @brief 오른쪽에서 왼쪽으로 배치되는가 — 자기 흐름 방향을 부모(루트면 문화권)로 푼 결과입니다. 마지막 arrange 가 정합니다(놓이기 전에는 false).
         * @details 이 위젯이 자식을 놓는 방향이고(패널의 거울 배치), 글 위젯의 문단 방향입니다. 자기 슬롯의 여백 · 정렬은 부모의 방향을 따릅니다.
         */
        bool isRightToLeft() const { return _bRightToLeft; }
        /** @brief 방향마다 포커스 탐색 규칙입니다. 바뀌어도 무효화하지 않는다(그림 · 자리에 닿지 않는다). */
        void                    setNavigation( const WidgetNavigation& navigation ) { _navigation = navigation; }
        const WidgetNavigation& getNavigation() const { return _navigation; }

        // --- 무효화 ---------------------------------------------------------------
        /** @brief 바뀐 이유를 알립니다(WidgetDirty 비트). 트리에 붙어 있지 않으면 표시만 하고, 붙을 때 트리가 모은다. */
        void   invalidate( uint32 dirtyReason );
        uint32 getDirtyFlags() const { return _dirtyFlags; }
        /**
         * @brief 크기가 자식에 기대지 않는 위젯이면 true 입니다(레이아웃 경계). 자식의 kLayout 이 여기서 멈춥니다.
         * @details 기본은 슬롯의 크기 덮어쓰기가 두 축 다 있을 때입니다 — 위젯 1 만 개에서 글 하나 바뀐 비용을 국소로 만든다.
         */
        virtual bool isLayoutBoundary() const { return _slot.hasFixedSize(); }

        // --- 레이아웃 ---------------------------------------------------------------
        /** @brief 원하는 크기입니다(마지막 measure 결과, UI 단위). */
        const float2&         getDesiredSize() const { return _desiredSize; }
        const WidgetGeometry& getGeometry() const { return _geometry; }
        /** @brief 마지막 배치의 슬롯 자리(부모 로컬, 렌더 변환 전)입니다. */
        const float2& getSlotPosition() const { return _lastSlotPosition; }
        /**
         * @brief 놓인 결과(레이아웃 사각형 + 누적 렌더 변환)를 적습니다. 레이아웃 걷기가 부르고, 레이아웃 없이 고정 배치로 짓는 시험도 부릅니다.
         * @details 기하가 바뀌면 kPaint 입니다(같으면 아무것도 하지 않는다).
         */
        void setArrangedGeometry( const WidgetGeometry& geometry );

        // --- 그리기 ---------------------------------------------------------------
        /** @brief 자식을 드는 패널이면 그 패널, 아니면 nullptr 입니다 — 그리기 걷기가 위젯마다 묻는다(`castTo` 의 타입 조회보다 싸다). */
        virtual PanelWidget* asPanel() { return nullptr; }
        /** @brief 글리프 아틀라스를 쓰는 위젯이면 true 입니다(글 위젯) — 아틀라스 페이지를 비우면(세대가 오르면) 그림 캐시를 다시 칠한다. */
        virtual bool usesGlyphAtlas() const { return false; }

        // --- 바인딩 · 애니메이션 ---------------------------------------------------------------
        /**
         * @brief 바인딩 · 애니메이션이 칸 @p property(경로의 맨 위 칸)에 리플렉션으로 값을 썼다 — 세터를 거치지 않았으므로 그 칸의 세터와 같은 무효화를 합니다.
         * @details 기본은 `Widget` 의 칸(불투명도 → kTransform, 렌더 변환 → kTransform | kArrange, 보임 → kVisibility, 슬롯 → kLayout …)을 알고, 모르는 칸은
         *          kLayout | kPaint(보수적)입니다. 파생은 그리기만 바뀌는 칸(색)과 캐시를 가진 칸(글 · 범위)을 덮어씁니다. 칸 종류를 PROPERTY 메타로 적지 않는 이유:
         *          커스텀 메타는 Shipping 에서 지워진다.
         */
        virtual void onBoundPropertyChanged( const PropertyInfo& property );
        /** @brief 글 판(현지화 — 언어 변경 · 표 다시 읽기)이 바뀌었다. 글 위젯은 키를 다시 풀고 kLayout 입니다. 기본은 아무것도 하지 않습니다. */
        virtual void onTextRevisionChanged();
        /** @brief 입력 장치 종류(글리프 — 키보드 · 패드)가 바뀌었다. 행동 태그(`[action=이름]`)를 든 글 위젯은 다시 풀고 kLayout 입니다. 기본은 아무것도 하지 않습니다. */
        virtual void onInputGlyphsChanged();

        // --- 포커스 ---------------------------------------------------------------
        /** @brief 포커스를 받을 수 있는 종류인가 — 버튼 · 슬라이더 · 입력 칸이 true. 꺼졌거나 안 보이면 받지 않는다(트리가 따로 본다). */
        virtual bool supportsFocus() const { return false; }
        /** @brief 포커스를 쥐면 키보드 글자를 받는가(글 입력 칸). 그동안 게임은 키를 보지 못합니다. */
        virtual bool supportsTextInput() const { return false; }
        /** @brief 지금 포커스를 쥐고 있으면 true 입니다. */
        bool hasFocus() const;

        // --- 사건 — 기본은 모두 "처리 안 함" ---------------------------------
        /** @brief 포인터 사건입니다(히트 테스트 경로 — 터널링 다음 버블링). */
        virtual UiReply onPointerEvent( const UiPointerEvent& event, UiRoutePhase phase );
        /** @brief 행동 사건입니다(포커스 경로 — 터널링 다음 버블링). */
        virtual UiReply onActionEvent( const UiActionEvent& event, UiRoutePhase phase );
        /** @brief 글자 사건입니다(포커스 위젯 하나에만). */
        virtual UiReply onTextEvent( const UiTextEvent& event );
        /** @brief 포커스를 얻었다 · 잃었다(알림). 상태 스타일 무효화(kStyle)는 부르는 쪽이 이미 했다. */
        virtual void onFocusChanged( bool bFocused );
        /** @brief 포인터가 들어왔다 · 나갔다(알림). 상태 스타일 무효화(kStyle)는 부르는 쪽이 이미 했다. */
        virtual void onHoverChanged( bool bHovered );

        // --- 스타일 ---------------------------------------------------------------
        /** @brief 포인터가 이 위젯(또는 자손) 위에 있는가입니다(호버 경로 — 스타일 `:hover`). */
        bool isHovered() const { return _bHovered; }
        /**
         * @brief 스타일 선택자가 보는 이 위젯의 상태(`UiStyleState` 비트)입니다. 기본은 호버 · 포커스 · 꺼짐이고, 누름 · 켜짐 · 고름은 그 위젯이 더합니다.
         * @details 상태가 바뀌면 위젯이 `kStyle` 을 겁니다(스타일 걷기가 다시 계산한다).
         */
        virtual uint32 computeStyleStates() const;
        /**
         * @brief 계산된 스타일입니다(스타일 걷기가 정한다 — 스타일 시트 규칙과 부모에서 물려받은 글 칸). 걷기 전이면 nullptr 입니다.
         * @details 스타일 전환(`_transition`) 중이면 보간 중인 보이는 값입니다(위젯이 든 복사본). 전환이 끝나면 다시 나눠 쓰는 계산된 스타일입니다.
         */
        const UiComputedStyle* getComputedStyle() const;
        /** @brief 불투명도에 계산된 스타일의 `_opacity` 를 곱한 값입니다(그리기 걷기가 쓴다). */
        float32 computeEffectiveOpacity() const;

    protected:
        /**
         * @brief 원하는 크기를 잽니다(레이아웃 measure). 패널은 여기서 자식을 `UiLayoutPass::measure` 로 잽니다. 기본은 0 입니다.
         * @param availableSize 슬롯이 줄 수 있는 크기(여백 · 덮어쓰기 적용 뒤, UI 단위). 축이 `kUiUnbounded` 면 그 축은 원하는 만큼.
         */
        virtual float2 computeDesiredSize( const UiLayoutContext& context, const float2& availableSize ) const;
        /**
         * @brief 자기 그림을 칠합니다(그리기 — `UiPaintPass`). 자식은 트리가 칠한다. 기본은 아무것도 칠하지 않습니다.
         * @details 칠하기 도구의 변환은 이 위젯의 기하입니다 — 로컬 (0, 0) ~ 크기(`getGeometry()._size`, UI 단위)에 칠한다. 결과는 위젯의 그림 캐시에 남아
         *          그리기 더러움(`kPaint` · `kStyle` · 조상의 `kTransform`)이 없으면 다시 부르지 않습니다.
         */
        virtual void paint( CanvasPainter& painter, const UiPaintContext& context ) const;
        /** @brief 자식 위에 칠합니다(스크롤 막대 · 패널 테두리). 자르기 밖이다. 기본은 아무것도 칠하지 않습니다. */
        virtual void paintOverChildren( CanvasPainter& painter, const UiPaintContext& context ) const;
        /** @brief 트리에 붙었다(바인딩 · 애니메이션이 여기서 붙는다). 자손은 부모 다음에 불린다. */
        virtual void onAttachedToTree();
        /** @brief 트리에서 떨어지기 직전이다(바인딩 · 애니메이션을 뗀다). */
        virtual void onDetachedFromTree();
        /**
         * @brief 사용자 입력이 칸 @p propertyName 의 값을 바꿨다고 화면에 알립니다 — 양방향 바인딩이 소스(뷰모델 · 설정)에 되씁니다.
         * @details 슬라이더 · 체크 · 콤보 · 글 입력 칸이 사용자 입력 길에서 부릅니다(코드가 세터로 바꾼 값은 알리지 않는다). 화면 밖 트리면 아무것도 하지 않습니다.
         */
        void notifyValueEdited( const hashed_string& propertyName );

    private:
        friend class PanelWidget;
        friend class WidgetTree;
        friend class UiLayoutPass;
        friend class UiPaintPass;
        friend class UiPointerState;
        friend class UiStylePass;
        friend class UiStyleTransition;

        /** @brief 이 위젯과 자손을 @p pTree 에 붙입니다 — 번호를 이름표에 올리고 밀린 무효화를 트리에 넘깁니다. */
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

        unique_ptr<WidgetPaintCache>       _paintCache;       ///< 마지막으로 칠한 사각형(물리 픽셀) — `UiPaintPass` 가 채운다, 처음 칠할 때 만든다
        shared_ptr<const UiComputedStyle>  _computedStyle;    ///< 계산된 스타일(같은 조건의 위젯이 나눠 쓴다 — `UiStylePass` 가 정한다)
        unique_ptr<UiStyleTransitionState> _styleTransition;  ///< 스타일 전환 중의 보이는 값(전환이 없으면 비었다 — `UiStyleTransition` 이 정한다)
        uint64                             _styleAncestorKey; ///< 이 위젯이 맞는 "조상 쪽" 선택자 조각의 해시 — 바뀌면 자손을 다시 맞춘다(`UiStylePass`)

        WidgetGeometry _geometry;          ///< 마지막 arrange 결과
        float2         _desiredSize;       ///< 마지막 measure 결과
        float2         _lastAvailableSize; ///< 마지막 measure 의 가용 크기(덮어쓰기 적용 뒤) — 캐시 열쇠 · 뿌리 다시 재기
        float2         _lastSlotPosition;  ///< 마지막 arrange 의 슬롯 자리(부모 로컬) — 뿌리 다시 놓기
        float2         _lastSlotSize;      ///< 마지막 arrange 의 슬롯 크기
        PanelWidget*   _pParent;           ///< 소유자(구조 링크 — 원시 포인터)
        WidgetTree*    _pTree;             ///< 붙은 트리(떨어져 있으면 nullptr)
        WidgetID       _id;
        uint32         _dirtyFlags;   ///< WidgetDirty 비트
        uint32         _layoutSerial; ///< 마지막으로 잰 레이아웃 걷기 번호(0 = 아직 잰 적 없음)

        PROPERTY( DisplayName = "Opacity", Min = 0.0, Max = 1.0 )
        float32 _opacity;
        PROPERTY( DisplayName = "Visibility" )
        WidgetVisibility _visibility;
        PROPERTY( DisplayName = "Flow Direction", Tooltip = "Inherit follows the parent (the culture at the root); fix LeftToRight for numbers and clocks" )
        UiFlowDirection _flowDirection;
        PROPERTY( DisplayName = "Enabled" )
        bool _bEnabled;
        bool _bRightToLeft; ///< 마지막 arrange 에서 푼 흐름 방향(UiLayoutPass 가 적는다)
        bool _bHovered;     ///< 호버 경로 안이다(UiPointerState 가 적는다)
    };
} // namespace sw
