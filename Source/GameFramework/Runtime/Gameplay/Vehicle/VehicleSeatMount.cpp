#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

#include "GameFramework/Runtime/Character/Control/Controller/ControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/DismountOnDeathComponent.h"
#include "GameFramework/Runtime/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    namespace
    {
        struct VehicleSeatMountInternal
        {
            /** @brief 하차 위치가 막혔을 때 둘러보는 방향 수입니다. 45° 간격입니다. */
            static constexpr int32 kExitProbeCount = 8;

            static GameObjectManager* findManager( const Component& component )
            {
                const GameObject* pOwner = component.getOwner();
                return pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
            }

            /** @brief 탑승 자세 파라미터를 켜거나 끕니다. 애니메이터가 없거나 이름이 비어 있으면 아무것도 하지 않습니다. */
            static void setRiderPose( GameObject& rider, const hashed_string& parameter, bool bSeated )
            {
                if ( parameter.empty() )
                    return;
                SkeletalAnimatorComponent* pAnimator = rider.getComponent<SkeletalAnimatorComponent>();
                if ( pAnimator != nullptr )
                    pAnimator->getParameters().setFloat( parameter, bSeated ? 1.0f : 0.0f );
            }

            /** @brief 탑승자의 걷기를 멈추는 구독입니다. 쥔 동안 물리 캡슐이 사라져서 탈것과 부딪치지 않습니다. 컨트롤러가 없으면 빈 구독입니다. */
            [[nodiscard]] static Subscription suspendRider( GameObject& rider )
            {
                CharacterControllerComponent* pController = rider.getComponent<CharacterControllerComponent>();
                return pController != nullptr ? pController->suspend() : Subscription{};
            }

            /** @brief 탑승자 캡슐을 @p feet 에 두어도 겹치지 않는지 봅니다. 컨트롤러가 없으면 비어 있는 것으로 봅니다. */
            static bool isSpotFree( const GameObject& rider, const float3& feet )
            {
                const CharacterControllerComponent* pController = rider.getComponent<CharacterControllerComponent>();
                return pController == nullptr || pController->isSpotFree( feet );
            }

            /** @brief 내릴 위치를 구합니다. 좌석의 하차 위치를 쓰고, 막혀 있으면 탈것 둘레 여덟 방향(같은 거리) 중 처음 빈 곳을 쓰고, 다 막혀 있으면 하차 위치를 그대로 씁니다. */
            static float3 findExitSpot( const GameObject& rider, const VehicleSeatComponent& seat, const SceneComponent& vehicleRoot )
            {
                const float4x4 vehicleWorld = vehicleRoot.getWorldMatrix();
                const float3   preferred    = float3::transform( seat.getExitOffset(), vehicleWorld );
                float3         result       = preferred;
                if ( isSpotFree( rider, preferred ) )
                    return result;
                const float3  offset   = seat.getExitOffset();
                const float32 distance = MathUtil::sqrt( offset._x * offset._x + offset._z * offset._z );
                const float32 baseYaw  = MathUtil::atan2( offset._x, offset._z );
                for ( int32 probeIndex = 1; probeIndex < kExitProbeCount; ++probeIndex )
                {
                    const float32 yaw = baseYaw + MathUtil::kTwoPi * static_cast<float32>( probeIndex ) / static_cast<float32>( kExitProbeCount );
                    const float3  probe{ MathUtil::sin( yaw ) * distance, offset._y, MathUtil::cos( yaw ) * distance };
                    const float3  spot = float3::transform( probe, vehicleWorld );
                    if ( isSpotFree( rider, spot ) )
                    {
                        result = spot;
                        return result;
                    }
                }
                return result;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MountResult VehicleSeatComponent::mount( PawnComponent& riderPawn, VehicleSeatComponent& seat )
    {
        using Internal                   = VehicleSeatMountInternal;
        GameObject*        pRider        = riderPawn.getOwner();
        GameObject*        pVehicle      = seat.getOwner();
        GameObjectManager* pManager      = Internal::findManager( riderPawn );
        SceneComponent*    pRiderRoot    = pRider != nullptr ? pRider->getPrimarySceneComponent() : nullptr;
        SceneComponent*    pVehicleRoot  = pVehicle != nullptr ? pVehicle->getPrimarySceneComponent() : nullptr;
        const bool         bHasAllPieces = pManager != nullptr && pRiderRoot != nullptr && pVehicleRoot != nullptr && pVehicle != pRider;
        if ( bHasAllPieces == false )
            return MountResult::NoSocket;
        if ( seat.isFree() == false || findSeatOf( riderPawn ) != nullptr )
            return MountResult::SeatTaken;
        const float3  riderPosition   = pRiderRoot->getWorldPosition();
        const float3  vehiclePosition = pVehicleRoot->getWorldPosition();
        const float32 deltaX          = riderPosition._x - vehiclePosition._x;
        const float32 deltaZ          = riderPosition._z - vehiclePosition._z;
        if ( MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ ) > seat.getEnterDistance() )
            return MountResult::TooFar;

        // 운전석은 빙의를 넘긴다. 넘길 조종자와 받을 폰이 있어야 한다.
        PawnComponent*       pVehiclePawn = seat.isDriverSeat() ? pVehicle->getComponent<PawnComponent>() : nullptr;
        ControllerComponent* pController  = pManager->resolveComponent( riderPawn.getController() );
        if ( seat.isDriverSeat() && pVehiclePawn == nullptr )
            return MountResult::NoVehiclePawn;
        if ( seat.isDriverSeat() && pController == nullptr )
            return MountResult::NotPossessed;

        // 소켓 변환은 탈것 외형의 소켓을 쓴다. 이름이 비어 있으면 좌석 오프셋(탈것 루트 기준)을 쓴다.
        float4x4 socketInVehicle = float4x4::makeTranslation( seat.getSeatOffset() );
        if ( seat.getSocketName().empty() == false )
        {
            const CharacterAppearanceComponent* pAppearance = pVehicle->getComponent<CharacterAppearanceComponent>();
            if ( pAppearance == nullptr || pAppearance->findBindSocketTransform( seat.getSocketName(), socketInVehicle ) == false )
                return MountResult::NoSocket;
        }
        SocketBindingComponent* pBinding = pRider->getComponent<SocketBindingComponent>();
        if ( pBinding == nullptr )
            pBinding = pRider->addComponent<SocketBindingComponent>();
        Subscription suspension = Internal::suspendRider( *pRider );
        if ( pBinding == nullptr || pBinding->bindToSocket( pVehicle, seat.getSocketName(), socketInVehicle ) == false )
            return MountResult::NoSocket;
        seat._riderSuspension = std::move( suspension );
        Internal::setRiderPose( *pRider, seat.getRiderPoseParameter(), true );
        // 탑승자가 쓰러지면 강제로 하차시킨다. 규칙이므로 타는 순간 붙인다.
        if ( pRider->getComponent<DismountOnDeathComponent>() == nullptr )
            (void)pRider->addComponent<DismountOnDeathComponent>();

        pRider->setKeptWhenParentDestroyed( true ); // 소켓으로 붙어도 탈것의 일부가 아니다 — 탈것이 사라지면 내린다
        seat._occupant        = pRider->getHandle();
        seat._seatInVehicle   = socketInVehicle; // 탑승 가속감의 기울임이 이 위에 얹힌다
        seat._rideSampleCount = 0;
        if ( seat.isDriverSeat() )
        {
            seat._occupantController        = TComponentHandle<ControllerComponent>{ pController };
            seat._previousVehicleController = pVehiclePawn->getController();
            pController->possess( *pVehiclePawn );
        }
        return MountResult::Mounted;
    }

    bool VehicleSeatComponent::dismount( PawnComponent& riderPawn, bool bForced )
    {
        VehicleSeatComponent* pSeat = findSeatOf( riderPawn );
        return pSeat != nullptr && pSeat->releaseRider( riderPawn, bForced );
    }

    bool VehicleSeatComponent::releaseRider( PawnComponent& riderPawn, bool bForced )
    {
        using Internal                        = VehicleSeatMountInternal;
        GameObject*        pRider             = riderPawn.getOwner();
        GameObjectManager* pManager           = Internal::findManager( riderPawn );
        const bool         bMissingMountParts = pRider == nullptr || pManager == nullptr;
        if ( bMissingMountParts )
            return false;
        GameObject*     pVehicle     = getOwner();
        SceneComponent* pVehicleRoot = pVehicle != nullptr ? pVehicle->getPrimarySceneComponent() : nullptr;
        SceneComponent* pRiderRoot   = pRider->getPrimarySceneComponent();

        // 소켓에서 분리한다. 지금 월드 위치를 유지한다. 강제(쓰러짐)이고 물리 바디(래그돌)가 있으면 물리로 떨어지고 걷기를 켜지 않는다.
        // 강제가 아니면 하차 위치로 옮긴다.
        SocketBindingComponent* pBinding           = pRider->getComponent<SocketBindingComponent>();
        const bool              bReleasedToPhysics = bForced && pBinding != nullptr && pBinding->release( SocketReleaseMode::Physics );
        if ( pBinding != nullptr && bReleasedToPhysics == false )
            (void)pBinding->release( SocketReleaseMode::Animated );
        const bool bPlaceRiderAtExit = bForced == false && pVehicleRoot != nullptr && pRiderRoot != nullptr;
        if ( bPlaceRiderAtExit )
        {
            const float3 exitSpot = Internal::findExitSpot( *pRider, *this, *pVehicleRoot );
            pRiderRoot->teleportTo( exitSpot );
            float3 rotation = pRiderRoot->getLocalRotation();
            rotation._x     = 0.0f;
            rotation._z     = 0.0f;
            pRiderRoot->setLocalRotation( rotation );
        }
        Internal::setRiderPose( *pRider, getRiderPoseParameter(), false );
        // 물리로 떨어진 몸은 걷지 않는다 — 부활(체력 다시 채움)까지 탑승자 쪽이 멈춤을 쥔다. 좌석이 쥐고 있으면 다음 탑승자가 앉을 때 풀린다.
        DismountOnDeathComponent* const pDown = bReleasedToPhysics ? pRider->getComponent<DismountOnDeathComponent>() : nullptr;
        if ( pDown != nullptr )
            pDown->holdMovement( std::move( _riderSuspension ) );
        _riderSuspension.reset();

        // 운전석이면 탔을 때의 조종자가 탑승자를 다시 쥔다. 탈것 폰은 놓여서 의도가 0 이 되므로 멈춰 선다. 탈 때 탈것을 쥐고 있던 조종자(말 AI)는 탈것을 다시 쥔다.
        ControllerComponent* pController                = _occupantController.isValid() ? pManager->resolveComponent( _occupantController ) : nullptr;
        ControllerComponent* pPreviousVehicleController = _previousVehicleController.isValid() ? pManager->resolveComponent( _previousVehicleController ) : nullptr;
        pRider->setKeptWhenParentDestroyed( false );
        _occupant                  = GameObjectHandle{};
        _occupantController        = TComponentHandle<ControllerComponent>{};
        _previousVehicleController = TComponentHandle<ControllerComponent>{};
        if ( pController != nullptr )
            pController->possess( riderPawn );
        PawnComponent* pVehiclePawn            = pVehicle != nullptr ? pVehicle->getComponent<PawnComponent>() : nullptr;
        const bool     bReturnControlToVehicle = pPreviousVehicleController != nullptr && pVehiclePawn != nullptr && pVehiclePawn->isPossessed() == false;
        if ( bReturnControlToVehicle )
            pPreviousVehicleController->possess( *pVehiclePawn );
        return true;
    }

    void VehicleSeatComponent::releaseGoneOccupant( GameObjectManager& manager, const GameObjectHandle& rider )
    {
        for ( VehicleSeatComponent* pSeat : manager.getComponentRegistry().getAll<VehicleSeatComponent>() )
        {
            if ( pSeat == nullptr || pSeat->_occupant != rider )
                continue;
            ControllerComponent* pController         = pSeat->_occupantController.isValid() ? manager.resolveComponent( pSeat->_occupantController ) : nullptr;
            ControllerComponent* pPreviousController = pSeat->_previousVehicleController.isValid() ? manager.resolveComponent( pSeat->_previousVehicleController ) : nullptr;
            pSeat->_riderSuspension.reset();
            pSeat->_occupant                  = GameObjectHandle{};
            pSeat->_occupantController        = TComponentHandle<ControllerComponent>{};
            pSeat->_previousVehicleController = TComponentHandle<ControllerComponent>{};
            // 사라진 탑승자의 조종자가 탈것을 쥔 채 남지 않는다. 내릴 때처럼 탈 때 쥐고 있던 조종자가 되찾는다.
            const GameObject* pVehicle     = pSeat->getOwner();
            PawnComponent*    pVehiclePawn = pVehicle != nullptr ? pVehicle->getComponent<PawnComponent>() : nullptr;
            if ( pController != nullptr && pVehiclePawn != nullptr && pController->findPawn() == pVehiclePawn )
                pController->unpossess();
            if ( pPreviousController != nullptr && pVehiclePawn != nullptr && pVehiclePawn->isPossessed() == false )
                pPreviousController->possess( *pVehiclePawn );
            return;
        }
    }

    VehicleSeatComponent* VehicleSeatComponent::findSeatOf( const PawnComponent& riderPawn )
    {
        const GameObject*        pRider   = riderPawn.getOwner();
        const GameObjectManager* pManager = pRider != nullptr ? pRider->getManager() : nullptr;
        if ( pManager == nullptr )
            return nullptr;
        const GameObjectHandle riderHandle = pRider->getHandle();
        for ( VehicleSeatComponent* pSeat : pManager->getComponentRegistry().getAll<VehicleSeatComponent>() )
        {
            if ( pSeat != nullptr && pSeat->getOccupant() == riderHandle )
                return pSeat;
        }
        return nullptr;
    }

    VehicleSeatComponent* VehicleSeatComponent::findNearestFreeSeat( const PawnComponent& riderPawn )
    {
        const GameObject*        pRider     = riderPawn.getOwner();
        const GameObjectManager* pManager   = pRider != nullptr ? pRider->getManager() : nullptr;
        const SceneComponent*    pRiderRoot = pRider != nullptr ? pRider->getPrimarySceneComponent() : nullptr;
        if ( pManager == nullptr || pRiderRoot == nullptr )
            return nullptr;
        const float3          riderPosition = pRiderRoot->getWorldPosition();
        VehicleSeatComponent* pBest         = nullptr;
        float32               bestDistance  = MathUtil::kMaxFloat;
        bool                  bBestIsDriver = false;
        for ( VehicleSeatComponent* pSeat : pManager->getComponentRegistry().getAll<VehicleSeatComponent>() )
        {
            const GameObject*     pVehicle         = pSeat != nullptr ? pSeat->getOwner() : nullptr;
            const SceneComponent* pVehicleRoot     = pVehicle != nullptr && pVehicle != pRider ? pVehicle->getPrimarySceneComponent() : nullptr;
            const bool            bSeatUnavailable = pVehicleRoot == nullptr || pSeat->isFree() == false || pSeat->isActive() == false;
            if ( bSeatUnavailable )
                continue;
            const float3  vehiclePosition = pVehicleRoot->getWorldPosition();
            const float32 deltaX          = riderPosition._x - vehiclePosition._x;
            const float32 deltaZ          = riderPosition._z - vehiclePosition._z;
            const float32 distance        = MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
            if ( distance > pSeat->getEnterDistance() )
                continue;
            // 운전석을 먼저 보고, 같은 종류끼리는 가까운 것을 먼저 본다.
            const bool bBetter = ( pSeat->isDriverSeat() && bBestIsDriver == false ) || ( pSeat->isDriverSeat() == bBestIsDriver && distance < bestDistance );
            if ( pBest == nullptr || bBetter )
            {
                pBest         = pSeat;
                bestDistance  = distance;
                bBestIsDriver = pSeat->isDriverSeat();
            }
        }
        return pBest;
    }
} // namespace sw
