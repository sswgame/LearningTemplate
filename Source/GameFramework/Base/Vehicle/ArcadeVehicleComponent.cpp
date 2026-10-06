#include "pch.h"

#include "GameFramework/Base/Vehicle/ArcadeVehicleComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsQuery.h"

#include "GameFramework/Base/Control/ControlIntent.h"
#include "GameFramework/Base/Control/PawnComponent.h"

namespace sw
{
    namespace
    {
        struct ArcadeVehicleComponentInternal
        {
            /** @brief 땅 광선을 차 위 몇 미터에서 쏘는가입니다. */
            static constexpr float32 kProbeAbove = 2.0f;
            /** @brief 땅 광선의 길이입니다(미터). */
            static constexpr float32 kProbeLength = 50.0f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    ArcadeVehicleComponent::PhysicsGround::PhysicsGround()
        : _pManager{ nullptr }
        , _fallbackHeight{ 0.0f }
        , _probeTop{ 0.0f }
    {
    }

    float32 ArcadeVehicleComponent::PhysicsGround::sampleHeight( float32 x, float32 z ) const
    {
        using Internal                = ArcadeVehicleComponentInternal;
        const IPhysicsScene3D* pScene = _pManager != nullptr ? _pManager->getScenePhysics().findScene3D() : nullptr;
        if ( pScene == nullptr )
            return _fallbackHeight;
        IPhysicsScene3D::CastHit hit;
        const float3             origin{ x, _probeTop, z };
        if ( pScene->raycast( origin, float3{ 0.0f, -1.0f, 0.0f }, Internal::kProbeLength, PhysicsQueryFilter{}, hit ) == false )
            return _fallbackHeight;
        return _probeTop - hit._distance;
    }

    ArcadeVehicleComponent::ArcadeVehicleComponent()
        : _driftButton{ "Drift" }
        , _boostButton{ "Boost" }
        , _jumpButton{ "Jump" }
        , _maxSpeed{ 30.0f }
        , _acceleration{ 18.0f }
        , _brakeDeceleration{ 40.0f }
        , _reverseMaxSpeed{ 8.0f }
        , _steerRate{ 2.4f }
        , _steerRateAtMaxSpeed{ 1.1f }
        , _bUsePhysicsGround{ true }
        , _motor{}
        , _ground{}
        , _listFrameEvent{}
        , _driftIndex{ -1 }
        , _boostIndex{ -1 }
        , _jumpIndex{ -1 }
    {
        setCanEverTick( true );
    }

    ArcadeVehicleSettings ArcadeVehicleComponent::makeSettings() const
    {
        ArcadeVehicleSettings settings;
        settings._maxSpeed            = _maxSpeed;
        settings._acceleration        = _acceleration;
        settings._brakeDeceleration   = _brakeDeceleration;
        settings._reverseMaxSpeed     = _reverseMaxSpeed;
        settings._steerRate           = _steerRate;
        settings._steerRateAtMaxSpeed = _steerRateAtMaxSpeed;
        return settings;
    }

    void ArcadeVehicleComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        GameObject*          pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _driftIndex                 = pPawn != nullptr ? pPawn->findButton( _driftButton ) : -1;
        _boostIndex                 = pPawn != nullptr ? pPawn->findButton( _boostButton ) : -1;
        _jumpIndex                  = pPawn != nullptr ? pPawn->findButton( _jumpButton ) : -1;
        const SceneComponent* pRoot = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const float3          start = pRoot != nullptr ? pRoot->getWorldPosition() : float3{};
        _ground._pManager           = _bUsePhysicsGround && pOwner != nullptr ? pOwner->getManager() : nullptr;
        _ground._fallbackHeight     = start._y;
        _motor.setSettings( makeSettings() );
        _motor.setGround( &_ground );
        _motor.reset( start, pRoot != nullptr ? pRoot->getLocalRotation()._y : 0.0f );
    }

    ArcadeVehicleInput ArcadeVehicleComponent::toVehicleInput( const ControlIntent& intent, float32 vehicleYaw, int32 driftIndex, int32 boostIndex, int32 jumpIndex )
    {
        // 가고 싶은 월드 방향을 차 기준으로 — 앞 성분은 페달, 옆 성분은 조향(요 0 이면 앞 +Z · 오른쪽 +X).
        const float3       worldMove = intent.computeWorldMove();
        const float32      sinYaw    = MathUtil::sin( vehicleYaw );
        const float32      cosYaw    = MathUtil::cos( vehicleYaw );
        ArcadeVehicleInput input{};
        input._throttle      = MathUtil::clamp( worldMove._x * sinYaw + worldMove._z * cosYaw, -1.0f, 1.0f );
        input._steer         = MathUtil::clamp( worldMove._x * cosYaw - worldMove._z * sinYaw, -1.0f, 1.0f );
        input._bDriftHeld    = intent.isDown( driftIndex ) ? SW_TRUE : SW_FALSE;
        input._bBoostPressed = intent.wasTriggered( boostIndex ) ? SW_TRUE : SW_FALSE;
        input._bJumpPressed  = intent.wasTriggered( jumpIndex ) ? SW_TRUE : SW_FALSE;
        return input;
    }

    void ArcadeVehicleComponent::onTick( float32 deltaTime )
    {
        using Internal = ArcadeVehicleComponentInternal;
        Component::onTick( deltaTime );
        GameObject*          pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        SceneComponent*      pRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pRoot == nullptr )
            return;
        const ControlIntent      intent = pPawn != nullptr ? pPawn->getIntent() : ControlIntent{};
        const ArcadeVehicleInput input  = toVehicleInput( intent, _motor.getYaw(), _driftIndex, _boostIndex, _jumpIndex );
        _ground._probeTop               = _motor.getPosition()._y + Internal::kProbeAbove;
        (void)_motor.advance( input, deltaTime );
        _listFrameEvent.clear();
        _motor.drainEvents( _listFrameEvent );

        pRoot->setWorldPosition( _motor.getPosition() );
        float3 rotation = pRoot->getLocalRotation();
        rotation._y     = _motor.getYaw();
        pRoot->setLocalRotation( rotation );
    }
} // namespace sw
