#include "pch.h"

#include "Engine/UI/World/UIWidgetComponentService.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/PanelWidget.h"
#include "Engine/UI/WidgetTree/Widget.h"
#include "Engine/UI/World/WidgetComponent.h"

namespace sw
{
    UIWidgetComponentService::UIWidgetComponentService()
        : _pUI{ nullptr }
        , _listComponent{}
        , _markers{}
        , _worldPointer{}
        , _pointerSubscription{}
        , _beforeLayoutSubscription{}
        , _afterPaintSubscription{}
    {
    }

    UIWidgetComponentService::~UIWidgetComponentService() = default;

    SubsystemInitResult UIWidgetComponentService::initializeSubsystem()
    {
        _pUI                      = &engine::getSubsystem<UISystem>();
        _pointerSubscription      = _pUI->onPointerProcessed().add( SW_DELEGATE_METHOD( Delegate<void( float32 )>, &UIWidgetComponentService::processWorldPointer, this ) );
        _beforeLayoutSubscription = _pUI->onUpdateBeforeLayout().add( SW_DELEGATE_METHOD( Delegate<void( float32 )>, &UIWidgetComponentService::updateScreenMarkers, this ) );
        _afterPaintSubscription   = _pUI->onUpdateAfterPaint().add( SW_DELEGATE_METHOD( Delegate<void( float32 )>, &UIWidgetComponentService::updateAfterPaint, this ) );
        return SubsystemInitResult::Succeeded;
    }

    void UIWidgetComponentService::shutdownSubsystem()
    {
        _afterPaintSubscription.reset();
        _beforeLayoutSubscription.reset();
        _pointerSubscription.reset();
        // 위젯 컴포넌트가 이 서비스보다 오래 남을 수 있으므로 등록과 마커를 잊게 한다(그 뒤 소멸자가 이 서비스를 부르지 않게).
        for ( WidgetComponent* pComponent : _listComponent )
        {
            _worldPointer.forgetComponent( *pComponent );
            pComponent->forgetUIService();
        }
        _listComponent.clear();
        // 화면 스택은 아직 있다(이것이 먼저 내려간다) — 마커 화면을 닫는다.
        if ( _markers.getScreen() != kInvalidUIScreenHandle )
            _pUI->closeScreen( _markers.getScreen() );
        _markers.clear();
        _pUI = nullptr;
    }

    span<const utf8* const> UIWidgetComponentService::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { UISystem::kSubsystemName };
        return kArrDependency;
    }

    void UIWidgetComponentService::registerComponent( WidgetComponent& component )
    {
        for ( const WidgetComponent* pExisting : _listComponent )
        {
            if ( pExisting == &component )
                return;
        }
        _listComponent.push_back( &component );
    }

    void UIWidgetComponentService::unregisterComponent( WidgetComponent& component )
    {
        for ( size_t index = 0; index < _listComponent.size(); ++index )
        {
            if ( _listComponent[index] == &component )
            {
                _worldPointer.forgetComponent( component );
                _listComponent.erase( _listComponent.begin() + static_cast<ptrdiff_t>( index ) );
                return;
            }
        }
    }

    WidgetID UIWidgetComponentService::addScreenMarker( unique_ptr<Widget> widget )
    {
        if ( widget == nullptr )
            return kInvalidWidgetID;
        UIScreen* pMarkers = _pUI->findScreen( _markers.getScreen() );
        if ( pMarkers == nullptr || pMarkers->isClosing() )
        {
            unique_ptr<CanvasPanel> root = make_unique<CanvasPanel>();
            root->setVisibility( WidgetVisibility::SelfHitTestInvisible ); // 빈 곳 클릭은 아래(게임)로 넘긴다
            UIScreenDesc desc{};
            desc._layer       = UILayer::HUD;
            desc._bTakesFocus = false;
            desc._bShowCursor = false;
            _markers.setScreen( _pUI->pushScreen( sw::make_unique<UIScreen>( desc, std::move( root ) ) ) );
            pMarkers = _pUI->findScreen( _markers.getScreen() );
        }
        PanelWidget* pRoot = castTo<PanelWidget>( pMarkers->getTree().getRoot() );
        if ( pRoot == nullptr )
            return kInvalidWidgetID;
        // 앵커 번호를 준다(뗀 마커의 번호부터). 첫 위치는 붙인 쪽이 정한다 — 그 전에는 위젯이 접혀 있다.
        widget->setPaintAnchor( _markers.allocateAnchor() );
        return pRoot->addChild( std::move( widget ) )->getID();
    }

    void UIWidgetComponentService::removeScreenMarker( WidgetID widget )
    {
        UIScreen*    pMarkers = _pUI->findScreen( _markers.getScreen() );
        Widget*      pWidget  = pMarkers != nullptr ? pMarkers->getTree().findWidgetByID( widget ) : nullptr;
        PanelWidget* pRoot    = pWidget != nullptr ? pWidget->getParent() : nullptr;
        if ( pRoot == nullptr )
            return;
        _markers.releaseAnchor( pWidget->getPaintAnchor() );
        (void)pRoot->removeChild( pWidget ); // 부모에서 찾은 위젯이므로 항상 제거된다
        if ( pRoot->getChildCount() == 0 )
        {
            _pUI->closeScreen( _markers.getScreen() );
            _markers.forgetScreen();
        }
    }

    Widget* UIWidgetComponentService::findScreenMarker( WidgetID widget ) const
    {
        const UIScreen* pMarkers = _pUI->findScreen( _markers.getScreen() );
        return pMarkers != nullptr ? pMarkers->getTree().findWidgetByID( widget ) : nullptr;
    }

    bool UIWidgetComponentService::routeWorldPointer( const WorldWidgetRay& ray, const UIPointerEvent& event )
    {
        return _worldPointer.route( _listComponent, ray, event );
    }

    void UIWidgetComponentService::appendWorldCanvases( vector<CanvasTargetDrawList>& inoutListTarget ) const
    {
        for ( const WidgetComponent* pComponent : _listComponent )
        {
            pComponent->appendWorldCanvas( inoutListTarget );
        }
    }

    void UIWidgetComponentService::processWorldPointer( [[maybe_unused]] float32 deltaSeconds )
    {
        // 입력을 켠 World 위젯이 하나도 없으면 광선을 만들 필요가 없다. 호버가 남아 있으면 푼다.
        const WidgetComponent* pAnchorComponent = nullptr;
        for ( const WidgetComponent* pComponent : _listComponent )
        {
            if ( pComponent->acceptsWorldInput() )
            {
                pAnchorComponent = pComponent;
                break;
            }
        }
        UIInputRouter&    input       = _pUI->getInputRouter();
        const UIViewport& viewport    = _pUI->getViewport();
        const bool        bLocked     = input.getInputManager()->isMouseLockActive();
        const bool        bNoViewport = viewport._physicalSize._y <= 0.0f;
        const bool        bCannotAim  = bLocked || bNoViewport;
        if ( pAnchorComponent == nullptr || bCannotAim )
        {
            _worldPointer.releaseHover();
            return;
        }
        // 카메라는 입력을 켠 첫 위젯의 씬 것을 쓴다(한 UI 시스템은 한 게임 씬의 UI 다).
        const GameObject*        pOwner   = pAnchorComponent->getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const CameraComponent*   pCamera  = pManager != nullptr ? pManager->getCameraRegistry().selectCamera( CameraRole::Game ) : nullptr;
        if ( pCamera == nullptr )
        {
            _worldPointer.releaseHover();
            return;
        }
        const float4x4 inverseViewProjection = pCamera->getViewProjectionMatrix( viewport._physicalSize._x / viewport._physicalSize._y ).invert();
        for ( const UIRoutedPointerEvent& routed : input.getFramePointerEvents() )
        {
            if ( routed._bOverScreenWidget == SW_TRUE )
            {
                _worldPointer.releaseHover(); // 화면 위젯이 World 위젯을 가린다
                continue;
            }
            WorldWidgetRay ray{};
            if ( WorldWidgetPick::makeRay( inverseViewProjection, routed._event._position, viewport._size, ray ) == false )
                continue;
            const bool bHandled = _worldPointer.route( _listComponent, ray, routed._event );
            // 위젯이 처리한 누름 · 뗌은 게임이 받지 않게 소비한다(화면 위젯과 같은 규칙).
            const bool bButtonEvent = routed._event._kind == UIPointerEventKind::Down || routed._event._kind == UIPointerEventKind::Up;
            if ( bHandled && bButtonEvent )
                input.getConsumption().consumeMouseButton( routed._event );
        }
    }

    void UIWidgetComponentService::updateScreenMarkers( [[maybe_unused]] float32 deltaSeconds )
    {
        SW_PROFILE_SCOPE( "GT.UI.Markers" );
        const UIViewport& viewport = _pUI->getViewport();
        WidgetMarkerView  view{}; // 카메라와 뷰 · 투영은 관리자마다 한 번 구한다
        for ( WidgetComponent* pComponent : _listComponent )
        {
            pComponent->updateScreenMarker( viewport, view );
        }
        // GPU 투영 마커의 공용 카메라 — 켜진 마커가 있을 때만 패킷에 실린다(`UIScreenMarkers::getGPUMarkerCount`).
        if ( view._pGPUManager != nullptr )
        {
            CanvasMarkerView gpuView{};
            WidgetComponent::makeGPUMarkerView( view, viewport._size, gpuView );
            _markers.setGPUMarkerView( gpuView, view._pGPUManager );
        }
    }

    void UIWidgetComponentService::updateAfterPaint( [[maybe_unused]] float32 deltaSeconds )
    {
        // 앵커는 사각형과 따로 프레임마다 낸다(움직인 마커가 그리기 목록 번호를 올리지 않는다). 배율은 그리기와 같은 값이다(`UISystem::makePaintContext`).
        const float32 uiScale = _pUI->getViewport()._uiScale;
        _markers.publishCanvasAnchors( uiScale > 0.0f ? uiScale : 1.0f );
        // 월드 공간 위젯: 컴포넌트마다 자기 트리를 렌더 텍스처 크기(배율 1)로 배치하고 칠한다.
        if ( _listComponent.empty() )
            return;
        const UILayoutContext layout = _pUI->makeLayoutContext();
        const UIPaintContext  paint  = _pUI->makePaintContext();
        for ( WidgetComponent* pComponent : _listComponent )
        {
            pComponent->updateWorldCanvas( layout, paint );
        }
    }
} // namespace sw
