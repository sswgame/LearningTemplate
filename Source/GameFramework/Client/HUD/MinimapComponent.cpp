#include "pch.h"

#include "GameFramework/Client/HUD/MinimapComponent.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Widgets/BorderPanel.h"

#include "GameFramework/Client/HUD/HUDControllerComponent.h"
#include "GameFramework/Client/HUD/MinimapMarkerComponent.h"
#include "GameFramework/Runtime/Character/Control/ControlEvents.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Event/GameEvents.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct MinimapComponentInternal
        {
            /** @brief 패널 가운데에 점 하나를 더합니다(자리는 렌더 변환이 옮긴다). */
            static Widget* addDot( CanvasPanel& panel )
            {
                unique_ptr<BorderPanel> dot = make_unique<BorderPanel>();
                return panel.addChild( std::move( dot ) );
            }

            /** @brief 점의 크기 · 색을 표시에 맞춥니다(배치를 다시 하므로 점의 표시가 바뀐 때만 부른다). */
            static void styleDot( Widget& widget, const MinimapMarkerComponent& marker )
            {
                const float32    half = marker.getSize() * 0.5f;
                WidgetLayoutSlot slot = widget.getLayoutSlot();
                slot._anchorMin       = float2{ 0.5f, 0.5f };
                slot._anchorMax       = float2{ 0.5f, 0.5f };
                slot._offsetMin       = float2{ -half, -half };
                slot._offsetMax       = float2{ half, half };
                widget.setLayoutSlot( slot );
                if ( BorderPanel* pDot = castTo<BorderPanel>( &widget ) )
                    pDot->setBackground( UIBrush::makeSolid( marker.getColor(), half ) );
            }

            static void placeDot( Widget& widget, const float2& offset )
            {
                WidgetRenderTransform transform = widget.getRenderTransform();
                transform._translation          = offset;
                widget.setRenderTransform( transform );
                widget.setVisibility( WidgetVisibility::HitTestInvisible );
            }
        };
    } // namespace

    MinimapComponent::MinimapComponent()
        : _panelName{ "Minimap" }
        , _range{ 30.0f }
        , _bRotateWithView{ false }
        , _captureCamera{}
        , _followTarget{}
        , _possessionSubscription{}
        , _listDotMarker{}
    {
    }

    void MinimapComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const GameObject* pOwner = getOwner();
        if ( pOwner != nullptr && pOwner->getComponent<HUDControllerComponent>() == nullptr )
            SW_LOG_ERROR_ID( "Minimap.NoHUD", "Minimap of '%#' needs a HUDControllerComponent on the same object", pOwner->getName().c_str() );
        _followTarget                      = GameObjectHandle{};
        EventDispatcher* const pDispatcher = game::findSubsystem<EventDispatcher>();
        if ( pDispatcher != nullptr )
            _possessionSubscription = pDispatcher->subscribe<PossessionChangedEvent>(
                gameEventChannel(), SW_DELEGATE_METHOD( Delegate<void( const PossessionChangedEvent& )>, &MinimapComponent::onPossessionChanged, this ) );
    }

    void MinimapComponent::onEndPlay()
    {
        _possessionSubscription.reset();
        Component::onEndPlay();
    }

    void MinimapComponent::onPossessionChanged( const PossessionChangedEvent& event )
    {
        const GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        const GameObjectHandle current = _followTarget.isValid() ? _followTarget : pOwner->getHandle();
        if ( event._previousPawn != current && event._pawn != pOwner->getHandle() )
            return;                  // 남의 빙의다
        _followTarget = event._pawn; // 놓기만 했으면 무효 — 이 오브젝트로 돌아온다
    }

    const GameObject* MinimapComponent::findFollowTarget() const
    {
        const GameObject* const pOwner    = getOwner();
        const GameObject* const pFollowed = pOwner != nullptr && pOwner->getManager() != nullptr ? pOwner->getManager()->resolveGameObject( _followTarget ) : nullptr;
        return pFollowed != nullptr ? pFollowed : pOwner;
    }

    MinimapPlacement MinimapComponent::computePlacement( const float3& center, float32 yaw, float32 range, const float2& halfSize, const float3& target )
    {
        const float32 deltaX = target._x - center._x;
        const float32 deltaZ = target._z - center._z;
        const float32 sine   = MathUtil::sin( yaw );
        const float32 cosine = MathUtil::cos( yaw );
        // 보는 쪽 앞(sine, cosine)이 화면 위(-Y), 오른쪽(cosine, -sine)이 화면 오른쪽이다.
        float2           unit{ ( deltaX * cosine - deltaZ * sine ) / range, -( deltaX * sine + deltaZ * cosine ) / range };
        const float32    length = MathUtil::sqrt( unit._x * unit._x + unit._y * unit._y );
        MinimapPlacement placement;
        placement._bInRange = length <= 1.0f ? SW_TRUE : SW_FALSE;
        if ( placement._bInRange == SW_FALSE )
            unit = float2{ unit._x / length, unit._y / length };
        placement._offset = float2{ unit._x * halfSize._x, unit._y * halfSize._y };
        return placement;
    }

    MinimapPlacement MinimapComponent::computeMarkerPlacement( const MinimapMarkerComponent& marker ) const
    {
        const GameObject*     pFollowed = findFollowTarget();
        const SceneComponent* pRoot     = pFollowed != nullptr ? pFollowed->getPrimarySceneComponent() : nullptr;
        const SceneComponent* pTarget   = marker.getOwner() != nullptr ? marker.getOwner()->getPrimarySceneComponent() : nullptr;
        if ( pRoot == nullptr || pTarget == nullptr )
        {
            MinimapPlacement outside;
            outside._bInRange = SW_FALSE;
            return outside;
        }
        return computePlacement( pRoot->getWorldPosition(), computeYaw(), _range, float2{ 1.0f, 1.0f }, pTarget->getWorldPosition() );
    }

    float32 MinimapComponent::computeYaw() const
    {
        const GameObject*    pFollowed = _bRotateWithView ? findFollowTarget() : nullptr;
        const PawnComponent* pPawn     = pFollowed != nullptr ? pFollowed->getComponent<PawnComponent>() : nullptr;
        return pPawn != nullptr ? pPawn->getIntent()._controlYaw : 0.0f;
    }

    void MinimapComponent::placeCaptureCamera( GameObjectManager& manager ) const
    {
        GameObject* const     pCameraObject = manager.resolveGameObject( _captureCamera );
        CameraComponent*      pCamera       = pCameraObject != nullptr ? pCameraObject->getComponent<CameraComponent>() : nullptr;
        const GameObject*     pFollowed     = findFollowTarget();
        const SceneComponent* pRoot         = pFollowed != nullptr ? pFollowed->getPrimarySceneComponent() : nullptr;
        if ( pCamera == nullptr || pRoot == nullptr )
            return; // 카메라가 파괴되었다
        const float3 center = pRoot->getWorldPosition();
        const float3 eye{ center._x, pCamera->getWorldPosition()._y, center._z };
        pCamera->setWorldTransform( float4x4::makeTrs( eye, quaternion::makeFromYawPitchRoll( computeYaw(), MathUtil::kHalfPi, 0.0f ), float3{ 1.0f, 1.0f, 1.0f } ) );
        pCamera->setOrthographic( true );
        pCamera->setOrthoHeight( _range * 2.0f );
    }

    CanvasPanel* MinimapComponent::findPanel() const
    {
        const GameObject*             pOwner = getOwner();
        const HUDControllerComponent* pHUD   = pOwner != nullptr ? pOwner->getComponent<HUDControllerComponent>() : nullptr;
        return pHUD != nullptr ? pHUD->findWidget<CanvasPanel>( _panelName ) : nullptr;
    }

    void MinimapComponent::onTick( TickContext& context )
    {
        using Internal = MinimapComponentInternal;
        Component::onTick( context );
        if ( _captureCamera.isValid() )
        {
            context.deferPostTickOnComponent( *this, []( MinimapComponent& minimap, GameObjectManager& manager )
            { minimap.placeCaptureCamera( manager ); } ); // 남의 카메라를 옮기는 일이라 틱 뒤다
        }
        CanvasPanel* pPanel = findPanel();
        if ( pPanel == nullptr )
            return; // 화면이 아직 없거나(로드 중) UI 시스템이 없다(헤드리스)
        const float2 size      = pPanel->getGeometry()._size;
        uint32       usedCount = 0;
        for ( const MinimapMarkerComponent* pMarker : context.getManager().getComponentRegistry().getAll<MinimapMarkerComponent>() )
        {
            const GameObject* pObject = pMarker->getOwner();
            const bool        bUsable = pMarker->isPendingDestroy() == false && pMarker->isActive() && pObject != nullptr && pObject->isActiveInHierarchy();
            if ( bUsable == false )
                continue;
            const MinimapPlacement placement = computeMarkerPlacement( *pMarker );
            if ( placement._bInRange == SW_FALSE && pMarker->isClampedToEdge() == false )
                continue;
            const bool bNewDot = usedCount >= pPanel->getChildCount();
            Widget*    pDot    = bNewDot ? Internal::addDot( *pPanel ) : pPanel->getChild( usedCount );
            if ( usedCount >= _listDotMarker.size() )
                _listDotMarker.resize( usedCount + 1u );
            if ( bNewDot || _listDotMarker[usedCount] != pMarker->getComponentID() )
            {
                _listDotMarker[usedCount] = pMarker->getComponentID();
                Internal::styleDot( *pDot, *pMarker );
            }
            Internal::placeDot( *pDot, float2{ placement._offset._x * size._x * 0.5f, placement._offset._y * size._y * 0.5f } );
            ++usedCount;
        }
        for ( uint32 index = usedCount; index < pPanel->getChildCount(); ++index )
        {
            pPanel->getChild( index )->setVisibility( WidgetVisibility::Hidden );
        }
    }
} // namespace sw
