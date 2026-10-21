#include "pch.h"

#include "Engine/UI/Screen/UITooltipService.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Input/InputManager.h"
#include "Engine/UI/Base/UIEventRouter.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/TextWidget.h"

namespace sw
{
    SW_GLOBAL_VARIABLE( float32, gv_uiTooltipDelay, 0.5f, "런타임 UI 툴팁이 뜨기까지 위젯 위에 머무는 시간(초)" );

    namespace
    {
        struct UITooltipServiceInternal
        {
            static constexpr const utf8* kBodyName = "Body";
            static constexpr const utf8* kTextName = "Text";
        };
    } // namespace

    UITooltipService::UITooltipService( UISystem& uiSystem )
        : _uiSystem{ uiSystem }
        , _screen{ kInvalidUIScreenHandle }
        , _hoverOwner{ kInvalidWidgetID }
        , _shownOwner{ kInvalidWidgetID }
        , _hoverSeconds{ 0.0f }
        , _bDocumentFailed{ false }
    {
    }

    UICursorShape UITooltipService::resolveCursorShape( const WidgetTree& tree, const UIWidgetPath& path )
    {
        for ( auto it = path._listWidget.rbegin(); it != path._listWidget.rend(); ++it )
        {
            const Widget* pWidget = tree.findWidgetByID( *it );
            if ( pWidget != nullptr && pWidget->getCursorShape() != UICursorShape::Inherit )
                return pWidget->getCursorShape();
        }
        return UICursorShape::Arrow; // 루트까지 모두 부모를 따르면 화살표다(규칙 — CSS `cursor: auto`)
    }

    CursorShape UITooltipService::toCursorShape( UICursorShape shape )
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

    const Widget* UITooltipService::findTooltipOwner( const WidgetTree& tree, const UIWidgetPath& path )
    {
        for ( auto it = path._listWidget.rbegin(); it != path._listWidget.rend(); ++it )
        {
            const Widget* pWidget = tree.findWidgetByID( *it );
            if ( pWidget != nullptr && pWidget->getTooltip().empty() == false )
                return pWidget;
        }
        return nullptr;
    }

    void UITooltipService::update( float32 deltaSeconds )
    {
        const UIInputRouter&  input        = _uiSystem.getInputRouter();
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

    void UITooltipService::clear()
    {
        hide();
        _hoverOwner   = kInvalidWidgetID;
        _hoverSeconds = 0.0f;
    }

    void UITooltipService::show( const Widget& owner )
    {
        if ( _screen == kInvalidUIScreenHandle )
        {
            if ( _bDocumentFailed )
                return;
            const string& documentPath = _uiSystem.getEngineDocuments()._tooltip;
            _screen                    = _uiSystem.openScreen( documentPath );
            if ( _uiSystem.findScreen( _screen ) == nullptr )
            {
                SW_LOG_ERROR( "[UI] Tooltip screen '%#' could not be opened - tooltips are not shown", documentPath.c_str() );
                _screen          = kInvalidUIScreenHandle;
                _bDocumentFailed = true;
                return;
            }
        }
        UIScreen* pScreen = _uiSystem.findScreen( _screen );
        if ( pScreen == nullptr )
        {
            _screen = kInvalidUIScreenHandle; // 누군가 닫았으므로 다음 프레임에 다시 연다
            return;
        }
        const WidgetTree& tree  = pScreen->getTree();
        Widget*           pBody = tree.findWidgetByName( UITooltipServiceInternal::kBodyName );
        TextWidget*       pText = tree.findWidget<TextWidget>( UITooltipServiceInternal::kTextName );
        if ( pBody == nullptr || pText == nullptr )
        {
            SW_LOG_ERROR( "[UI] Tooltip screen has no '%#' or '%#' widget - tooltips are not shown", UITooltipServiceInternal::kBodyName, UITooltipServiceInternal::kTextName );
            _bDocumentFailed = true;
            hide();
            return;
        }
        if ( _shownOwner != owner.getID() )
            pText->setText( owner.getTooltip() );
        const float2&         pointerPosition = _uiSystem.getInputRouter().getLastPointerPosition();
        WidgetRenderTransform transform       = pBody->getRenderTransform();
        transform._translation                = float2{ pointerPosition._x + kPointerOffsetX, pointerPosition._y + kPointerOffsetY };
        pBody->setRenderTransform( transform );
        _shownOwner = owner.getID();
    }

    void UITooltipService::hide()
    {
        if ( _screen != kInvalidUIScreenHandle )
            _uiSystem.closeScreen( _screen );
        _screen     = kInvalidUIScreenHandle;
        _shownOwner = kInvalidWidgetID;
    }
} // namespace sw
