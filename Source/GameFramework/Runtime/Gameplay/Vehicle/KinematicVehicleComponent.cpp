#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Vehicle/KinematicVehicleComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Environment/Surface/SurfaceSample.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/World/Spline/SplineComponent.h"

namespace sw
{
    namespace
    {
        struct KinematicVehicleComponentInternal
        {
            /** @brief 땅 광선을 차 위 몇 미터에서 쏘는지 나타냅니다. */
            static constexpr float32 kProbeAbove = 2.0f;
            /** @brief 땅 광선의 길이입니다(미터). */
            static constexpr float32 kProbeLength = 50.0f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    KinematicVehicleComponent::PhysicsGround::PhysicsGround()
        : _pManager{ nullptr }
        , _pSurfaceTable{}
        , _ownerID{ 0 }
        , _fallbackHeight{ 0.0f }
        , _probeTop{ 0.0f }
    {
    }

    float32 KinematicVehicleComponent::PhysicsGround::sampleHeight( float32 x, float32 z ) const
    {
        using Internal                     = KinematicVehicleComponentInternal;
        const IPhysicsQueryScene3D* pScene = _pManager != nullptr ? _pManager->getSubsystem<sw::ScenePhysics>().findScene3D() : nullptr;
        if ( pScene == nullptr )
            return _fallbackHeight;
        IPhysicsQueryScene3D::CastHit hit;
        const float3                  origin{ x, _probeTop, z };
        if ( pScene->raycast( origin, float3{ 0.0f, -1.0f, 0.0f }, Internal::kProbeLength, PhysicsQueryFilter{}, hit ) == false )
            return _fallbackHeight;
        return _probeTop - hit._distance;
    }

    VehicleSurface KinematicVehicleComponent::PhysicsGround::sampleSurface( float32 x, float32 z ) const
    {
        using Internal = KinematicVehicleComponentInternal;
        VehicleSurface surface;
        SurfaceSample  sample;
        if ( _pManager == nullptr || _pSurfaceTable == nullptr )
            return surface;
        const float3 from{ x, _probeTop, z };
        const float3 to{ x, _probeTop - Internal::kProbeLength, z };
        if ( SurfaceSample::sampleBelow( *_pManager, _pSurfaceTable, from, to, _ownerID, false, sample ) == false || sample._pKind == nullptr )
            return surface; // 맞은 것이 없다 — 평면 위(구름 저항 0). 표에 없는 재질은 질의가 이름마다 오류를 남겼다
        surface._rollingResistance = sample._pKind->_rollingResistance;
        surface._gripScale         = sample._pKind->_gripScale;
        return surface;
    }

    KinematicVehicleComponent::KinematicVehicleComponent()
        : _tuning{}
        , _rail{}
        , _driftButton{}
        , _boostButton{}
        , _jumpButton{}
        , _bUsePhysicsGround{ true }
        , _motor{}
        , _ground{}
        , _listFrameEvent{}
        , _driftIndex{}
        , _boostIndex{}
        , _jumpIndex{}
        , _bReady{ false }
    {
        setCanEverTick( true );
    }

    KinematicVehicleComponent::~KinematicVehicleComponent() = default;

    void KinematicVehicleComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );
        _bReady                     = false;
        GameObject*          pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        _driftIndex                 = pPawn != nullptr ? pPawn->findButton( _driftButton ) : nullopt;
        _boostIndex                 = pPawn != nullptr ? pPawn->findButton( _boostButton ) : nullopt;
        _jumpIndex                  = pPawn != nullptr ? pPawn->findButton( _jumpButton ) : nullopt;
        const SceneComponent* pRoot = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pOwner == nullptr || pRoot == nullptr )
            return;

        const shared_ptr<const KinematicVehicleSettings> pTuning = _tuning.acquire();
        if ( pTuning == nullptr )
        {
            SW_LOG_ERROR_ID( "KinematicVehicle.TuningMissing", "'%#': vehicle tuning '%#' did not load; the vehicle does not move", pOwner->getName().c_str(), _tuning.c_str() );
            return;
        }
        const float3 start      = pRoot->getWorldPosition();
        _ground._pManager       = _bUsePhysicsGround ? pOwner->getManager() : nullptr;
        _ground._ownerID        = pOwner->getObjectID();
        _ground._fallbackHeight = start._y;
        _ground._probeTop       = start._y + KinematicVehicleComponentInternal::kProbeAbove;
        if ( _bUsePhysicsGround )
        {
            _ground._pSurfaceTable = SurfaceKindTable::findDefault();
            if ( _ground._pSurfaceTable == nullptr )
                return; // 표를 읽지 못했다 — `findDefault` 가 오류를 남겼다. 표면 없이 달리지 않는다
        }
        _motor.setSettings( *pTuning );
        _motor.setGround( &_ground );
        _motor.reset( start, pRoot->getLocalRotation()._y );
        if ( pTuning->_rail._bEnabled && attachRail() == false )
            return;
        _bReady = true;
    }

    bool KinematicVehicleComponent::attachRail()
    {
        GameObject*             pOwner   = getOwner();
        GameObjectManager*      pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GameObject*             pRail    = pManager != nullptr && _rail.isValid() ? pManager->resolveGameObject( _rail ) : nullptr;
        const SplineComponent*  pSpline  = pRail != nullptr ? pRail->getComponent<SplineComponent>() : nullptr;
        const SplinePath* const pPath    = pSpline != nullptr ? &pSpline->getWorldPath() : nullptr;
        if ( pPath == nullptr || pPath->isValid() == false )
        {
            SW_LOG_ERROR_ID( "KinematicVehicle.RailMissing", "'%#': the rail module needs a Rail object with a spline of two or more points; the vehicle does not move",
                             pOwner != nullptr ? pOwner->getName().c_str() : "" );
            return false;
        }
        _motor.setRail( pPath );
        return true;
    }

    KinematicVehicleInput KinematicVehicleComponent::toVehicleInput( const ControlIntent& intent, float32 vehicleYaw, optional<int32> driftIndex, optional<int32> boostIndex, optional<int32> jumpIndex )
    {
        // 가고 싶은 월드 방향을 차 기준으로 바꾼다. 앞 성분은 페달, 옆 성분은 조향이다. 요가 0 이면 앞은 +Z 이고 오른쪽은 +X 이다.
        const float3          worldMove = intent.computeWorldMove();
        const float32         sinYaw    = MathUtil::sin( vehicleYaw );
        const float32         cosYaw    = MathUtil::cos( vehicleYaw );
        KinematicVehicleInput input{};
        input._throttle      = MathUtil::clamp( worldMove._x * sinYaw + worldMove._z * cosYaw, -1.0f, 1.0f );
        input._steer         = MathUtil::clamp( worldMove._x * cosYaw - worldMove._z * sinYaw, -1.0f, 1.0f );
        input._bDriftHeld    = intent.isDown( driftIndex ) ? SW_TRUE : SW_FALSE;
        input._bBoostPressed = intent.wasTriggered( boostIndex ) ? SW_TRUE : SW_FALSE;
        input._bJumpPressed  = intent.wasTriggered( jumpIndex ) ? SW_TRUE : SW_FALSE;
        return input;
    }

    void KinematicVehicleComponent::onTick( float32 deltaTime )
    {
        using Internal = KinematicVehicleComponentInternal;
        Component::onTick( deltaTime );
        _listFrameEvent.clear();
        GameObject*          pOwner = getOwner();
        const PawnComponent* pPawn  = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        SceneComponent*      pRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pRoot == nullptr || _bReady == false )
            return;
        const ControlIntent         intent = pPawn != nullptr ? pPawn->getIntent() : ControlIntent{};
        const KinematicVehicleInput input  = toVehicleInput( intent, _motor.getYaw(), _driftIndex, _boostIndex, _jumpIndex );
        _ground._probeTop                  = _motor.getPosition()._y + Internal::kProbeAbove;
        (void)_motor.advance( input, deltaTime );
        _motor.pollEvents( _listFrameEvent );

        pRoot->setWorldPosition( _motor.getPosition() );
        float3 rotation = pRoot->getLocalRotation();
        rotation._y     = _motor.getYaw();
        pRoot->setLocalRotation( rotation );
    }
} // namespace sw
