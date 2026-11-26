#include "pch.h"

#include "Engine/UI/Overlay/UITooltipSubsystem.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Input/InputManager.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/UIWidgetPath.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    SW_GLOBAL_VARIABLE( float32, gv_uiTooltipDelay, 0.5f, "런타임 UI 툴팁이 뜨기까지 위젯 위에 머무는 시간(초)" );

    namespace
    {
        struct UITooltipSubsystemInternal
        {
            static constexpr const utf8* kBodyName = "Body";
            static constexpr const utf8* kTextName = "Text";
        };
    } // namespace

    UITooltipSubsystem::UITooltipSubsystem()
        : _pUI{ nullptr }
        , _updateSubscription{}
        , _screen{}
        , _hoverOwner{ kInvalidWidgetID }
        , _shownOwner{ kInvalidWidgetID }
        , _hoverSeconds{ 0.0f }
        , _bDocumentFailed{ SW_FALSE }
    {
    }

    SubsystemInitResult UITooltipSubsystem::initializeSubsystem()
    {
        _pUI                = &engine::getSubsystem<UISystem>();
        _updateSubscription = _pUI->onUpdateBeforeScreens().add( SW_DELEGATE_METHOD( Delegate<void( float32 )>, &UITooltipSubsystem::update, this ) );
        return SubsystemInitResult::Succeeded;
    }

    void UITooltipSubsystem::shutdownSubsystem()
    {
        _updateSubscription.reset();
        clear(); // 화면 스택이 아직 있다(이것이 먼저 내려간다) — 연 화면을 닫는다
        _pUI = nullptr;
    }

    span<const utf8* const> UITooltipSubsystem::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { UISystem::kSubsystemName };
        return kArrDependency;
    }

    float2 UITooltipSubsystem::computePlacement( const float2& pointer, const float2& tooltipSize, const float2& screenSize )
    {
        float32 x = pointer._x + kPointerOffsetX;
        float32 y = pointer._y + kPointerOffsetY;
        // 오른쪽을 넘으면 포인터 왼쪽에, 아래를 넘으면 포인터 위에 둔다(포인터를 가리지 않는다).
        if ( x + tooltipSize._x > screenSize._x )
            x = pointer._x - kPointerOffsetX - tooltipSize._x;
        if ( y + tooltipSize._y > screenSize._y )
            y = pointer._y - kPointerOffsetY - tooltipSize._y;
        // 뒤집어도 화면 밖이면(툴팁이 화면보다 큼) 왼쪽 위 변에 붙인다.
        x = MathUtil::clamp( x, 0.0f, MathUtil::max( 0.0f, screenSize._x - tooltipSize._x ) );
        y = MathUtil::clamp( y, 0.0f, MathUtil::max( 0.0f, screenSize._y - tooltipSize._y ) );
        return float2{ x, y };
    }

    UICursorShape UITooltipSubsystem::resolveCursorShape( const WidgetTree& tree, const UIWidgetPath& path )
    {
        for ( auto it = path._listWidget.rbegin(); it != path._listWidget.rend(); ++it )
        {
            const Widget* pWidget = tree.findWidgetByID( *it );
            if ( pWidget != nullptr && pWidget->getCursorShape() != UICursorShape::Inherit )
                return pWidget->getCursorShape();
        }
        return UICursorShape::Arrow; // 루트까지 모두 부모를 따르면 화살표다(규칙 — CSS `cursor: auto`)
    }

    CursorShape UITooltipSubsystem::toCursorShape( UICursorShape shape )
    {
        SW_ASSERT( shape != UICursorShape::Inherit && "resolve the cursor shape before handing it to the window" );
        switch ( shape )
        {
            case UICursorShape::Inherit:
                return CursorShape::Arrow;
            case UICursorShape::Arrow:
                return CursorShape::Arrow;
            case UICursorShape::Hand:
                return CursorShape::Hand;
            case UICursorShape::TextBeam:
                return CursorShape::TextBeam;
            case UICursorShape::ResizeHorizontal:
                return CursorShape::ResizeHorizontal;
            case UICursorShape::ResizeVertical:
                return CursorShape::ResizeVertical;
            case UICursorShape::NotAllowed:
                return CursorShape::NotAllowed;
        }
        return CursorShape::Arrow;
    }

    const Widget* UITooltipSubsystem::findTooltipOwner( const WidgetTree& tree, const UIWidgetPath& path )
    {
        for ( auto it = path._listWidget.rbegin(); it != path._listWidget.rend(); ++it )
        {
            const Widget* pWidget = tree.findWidgetByID( *it );
            if ( pWidget != nullptr && pWidget->getTooltip().empty() == false )
                return pWidget;
        }
        return nullptr;
    }

    void UITooltipSubsystem::update( float32 deltaSeconds )
    {
        // 화면 그리기를 막는 동안(에디터 멈춤)은 포인터도 막혀 있다 — 툴팁과 커서를 건드리지 않는다.
        if ( _pUI->isOnScreenSuppressed() )
            return;
        const UIInputRouter&  input        = _pUI->getInputRouter();
        const UIPointerState& pointer      = input.getPointerState();
        const WidgetTree*     pHoverTree   = pointer.getHoverTree();
        const bool            bPointerMode = input.getInputMode() == UIInputMode::Pointer;

        // 커서: 호버가 없으면 화살표. 화면이 커서를 숨기는 동안(잠금)은 입력 관리자가 적용을 미룬다.
        InputManager* pInputManager = input.getInputManager();
        if ( pInputManager != nullptr )
        {
            const UICursorShape shape = pHoverTree != nullptr ? resolveCursorShape( *pHoverTree, pointer.getHoverPath() ) : UICursorShape::Arrow;
            pInputManager->setCursorShape( toCursorShape( shape ) );
        }

        // 툴팁: 같은 주인 위에 머문 시간을 센다. 주인이 바뀌면 처음부터, 포인터 입력이 아니면 닫는다.
        const Widget*  pOwner  = pHoverTree != nullptr && bPointerMode ? findTooltipOwner( *pHoverTree, pointer.getHoverPath() ) : nullptr;
        const WidgetID ownerID = pOwner != nullptr ? pOwner->getID() : kInvalidWidgetID;
        if ( ownerID != _hoverOwner )
        {
            _hoverOwner   = ownerID;
            _hoverSeconds = 0.0f;
            if ( _shownOwner != kInvalidWidgetID )
                hide();
        }
        if ( pOwner == nullptr )
            return;
        _hoverSeconds += deltaSeconds;
        if ( _hoverSeconds >= static_cast<float32>( gv_uiTooltipDelay ) )
            show( *pOwner );
    }

    void UITooltipSubsystem::resetGameInstanceState( GameInstanceBoundary boundary )
    {
        if ( boundary == GameInstanceBoundary::Ended )
            clear();
    }

    const utf8* UITooltipSubsystem::findGameInstanceState() const
    {
        return _screen.isHeld() ? "a shown tooltip" : nullptr;
    }

    void UITooltipSubsystem::clear()
    {
        hide();
        _hoverOwner   = kInvalidWidgetID;
        _hoverSeconds = 0.0f;
    }

    void UITooltipSubsystem::show( const Widget& owner )
    {
        if ( _screen.isHeld() == false )
        {
            if ( _bDocumentFailed )
                return;
            const string& documentPath = _pUI->getEngineDocuments()._tooltip;
            _screen                    = UIOwnedScreen( *_pUI, _pUI->openScreen( documentPath ) );
            if ( _screen.find() == nullptr )
            {
                SW_LOG_ERROR_ID( "UITooltipService.TooltipScreenCouldNot", "[UI] Tooltip screen '%#' could not be opened - tooltips are not shown", documentPath.c_str() );
                _screen.reset();
                _bDocumentFailed = SW_TRUE;
                return;
            }
        }
        UIScreen* pScreen = _screen.find();
        if ( pScreen == nullptr )
        {
            _screen.reset(); // 누군가 닫았으므로 다음 프레임에 다시 연다
            return;
        }
        const WidgetTree& tree  = pScreen->getTree();
        Widget*           pBody = tree.findWidgetByName( hashed_string( UITooltipSubsystemInternal::kBodyName ) );
        TextWidget*       pText = tree.findWidget<TextWidget>( hashed_string( UITooltipSubsystemInternal::kTextName ) );
        if ( pBody == nullptr || pText == nullptr )
        {
            SW_LOG_ERROR_ID( "UITooltipSubsystem.TooltipScreenNoWidget", "[UI] Tooltip screen has no '%#' or '%#' widget - tooltips are not shown", UITooltipSubsystemInternal::kBodyName, UITooltipSubsystemInternal::kTextName );
            _bDocumentFailed = SW_TRUE;
            hide();
            return;
        }
        if ( _shownOwner != owner.getID() )
            pText->setText( owner.getTooltip() );
        // 크기는 지난 배치의 것이다. 처음 띄운 프레임은 아직 배치 전(0)이라 자리를 확정할 수 없으므로 한 프레임 보이지 않게 둔다(화면 변에서 한 프레임 번쩍이지 않는다).
        const float2&          pointerPosition  = _pUI->getInputRouter().getLastPointerPosition();
        const float2&          bodySize         = pBody->getGeometry()._size;
        const bool             bLaidOut         = bodySize._x > 0.0f && bodySize._y > 0.0f;
        const WidgetVisibility wantedVisibility = bLaidOut ? WidgetVisibility::Visible : WidgetVisibility::Hidden;
        if ( pBody->getVisibility() != wantedVisibility )
            pBody->setVisibility( wantedVisibility );
        WidgetRenderTransform transform = pBody->getRenderTransform();
        transform._translation          = computePlacement( pointerPosition, bodySize, _pUI->getViewport()._size );
        pBody->setRenderTransform( transform );
        _shownOwner = owner.getID();
    }

    void UITooltipSubsystem::hide()
    {
        _screen.reset();
        _shownOwner = kInvalidWidgetID;
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SUBSYSTEM( UITooltipSubsystem, GameInstance, Startup, (), SW_ENGINE_DEFAULT_FACTORY( UITooltipSubsystem ) );
} // namespace sw
