#include "pch.h"

#include "GameFramework/Base/Gameplay/Vehicle/MountUtil.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/Physics/CharacterControllerComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsQuery.h"
#include "Engine/Physics/PhysicsShape.h"

#include "GameFramework/Base/Actor/Control/Controller/ControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Pawn/CharacterPawnMovementComponent.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearanceComponent.h"
#include "GameFramework/Base/Gameplay/Vehicle/RiderDownWatcherComponent.h"
#include "GameFramework/Base/Gameplay/Vehicle/VehicleSeatComponent.h"

namespace sw
{
    namespace
    {
        struct MountUtilInternal
        {
            /** @brief 하차 자리가 막혔을 때 둘러보는 방향 수입니다(45° 간격). */
            static constexpr int32 kExitProbeCount = 8;
            /** @brief 겹침을 잴 때 발을 바닥에서 띄우는 높이입니다(미터 — 바닥과 겹치지 않게). */
            static constexpr float32 kGroundClearance = 0.05f;

            static GameObjectManager* findManager( const Component& component )
            {
                const GameObject* pOwner = component.getOwner();
                return pOwner != nullptr ? pOwner->getManager() : nullptr;
            }

            /** @brief 탑승 자세 파라미터를 켜거나 끕니다(애니메이터가 없거나 이름이 비면 할 일이 없다). */
            static void setRiderPose( GameObject& rider, const hashed_string& parameter, bool bSeated )
            {
                if ( parameter.empty() )
                    return;
                SkeletalAnimatorComponent* pAnimator = rider.getComponent<SkeletalAnimatorComponent>();
                if ( pAnimator != nullptr )
                    pAnimator->getParameters().setFloat( parameter, bSeated ? 1.0f : 0.0f );
            }

            /** @brief 탑승자의 걷기를 멈추거나 켭니다 — 폰 이동과 캐릭터 컨트롤러(꺼 두면 물리 캡슐이 사라져 탈것과 부딪치지 않는다). */
            static void setRiderMovementSuspended( GameObject& rider, bool bSuspended )
            {
                CharacterPawnMovementComponent* pMovement = rider.getComponent<CharacterPawnMovementComponent>();
                if ( pMovement != nullptr )
                    pMovement->setSuspended( bSuspended );
                CharacterControllerComponent* pController = rider.getComponent<CharacterControllerComponent>();
                if ( pController != nullptr )
                    pController->setActive( bSuspended == false );
            }

            /** @brief 탑승자 캡슐을 @p feet 에 세우면 무엇과 겹치는지입니다. 물리 씬 · 컨트롤러가 없으면 비었다고 봅니다. */
            static bool isSpotFree( GameObjectManager& manager, const GameObject& rider, const float3& feet )
            {
                const IPhysicsScene3D*              pScene      = manager.getScenePhysics().findScene3D();
                const CharacterControllerComponent* pController = rider.getComponent<CharacterControllerComponent>();
                if ( pScene == nullptr || pController == nullptr )
                    return true;
                PhysicsShapeDesc3D capsule;
                capsule._type       = PhysicsShapeType3D::Capsule;
                capsule._radius     = pController->getRadius();
                capsule._halfHeight = pController->getHalfHeight();
                const float3              center{ feet._x, feet._y + kGroundClearance + capsule._radius + capsule._halfHeight, feet._z };
                PhysicsQueryFilter        filter;
                vector<PhysicsBodyHandle> listBody;
                return pScene->overlapShape( capsule, center, quaternion{}, filter, listBody ) == 0;
            }

            /** @brief 내릴 자리 — 좌석의 하차 자리, 막혔으면 탈것 둘레 여덟 방향(같은 거리) 중 처음 빈 곳, 다 막혔으면 하차 자리 그대로. */
            static float3 findExitSpot( GameObjectManager& manager, const GameObject& rider, const VehicleSeatComponent& seat, const SceneComponent& vehicleRoot )
            {
                const float4x4 vehicleWorld = vehicleRoot.getWorldMatrix();
                const float3   preferred    = float3::transform( seat.getExitOffset(), vehicleWorld );
                if ( isSpotFree( manager, rider, preferred ) )
                    return preferred;
                const float3  offset   = seat.getExitOffset();
                const float32 distance = MathUtil::sqrt( offset._x * offset._x + offset._z * offset._z );
                const float32 baseYaw  = MathUtil::atan2( offset._x, offset._z );
                for ( int32 probeIndex = 1; probeIndex < kExitProbeCount; ++probeIndex )
                {
                    const float32 yaw = baseYaw + MathUtil::kTwoPi * static_cast<float32>( probeIndex ) / static_cast<float32>( kExitProbeCount );
                    const float3  probe{ MathUtil::sin( yaw ) * distance, offset._y, MathUtil::cos( yaw ) * distance };
                    const float3  spot = float3::transform( probe, vehicleWorld );
                    if ( isSpotFree( manager, rider, spot ) )
                        return spot;
                }
                return preferred;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MountResult MountUtil::mount( PawnComponent& riderPawn, VehicleSeatComponent& seat )
    {
        using Internal                   = MountUtilInternal;
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

        // 운전석은 빙의를 넘긴다 — 넘길 조종자와 받을 폰이 있어야 한다.
        PawnComponent*       pVehiclePawn = seat.isDriverSeat() ? pVehicle->getComponent<PawnComponent>() : nullptr;
        ControllerComponent* pController  = static_cast<ControllerComponent*>( pManager->resolveComponent( riderPawn.getController() ) );
        if ( seat.isDriverSeat() && pVehiclePawn == nullptr )
            return MountResult::NoVehiclePawn;
        if ( seat.isDriverSeat() && pController == nullptr )
            return MountResult::NotPossessed;

        // 소켓 변환 — 탈것 외형의 소켓, 이름이 비면 좌석 오프셋(탈것 루트 기준).
        float4x4 socketInVehicle = float4x4::createTranslation( seat.getSeatOffset() );
        if ( seat.getSocketName().empty() == false )
        {
            const CharacterAppearanceComponent* pAppearance = pVehicle->getComponent<CharacterAppearanceComponent>();
            if ( pAppearance == nullptr || pAppearance->findBindSocketTransform( seat.getSocketName(), socketInVehicle ) == false )
                return MountResult::NoSocket;
        }
        SocketBindingComponent* pBinding = pRider->getComponent<SocketBindingComponent>();
        if ( pBinding == nullptr )
            pBinding = pRider->addComponent<SocketBindingComponent>();
        Internal::setRiderMovementSuspended( *pRider, true );
        if ( pBinding == nullptr || pBinding->bindToSocket( pVehicle, seat.getSocketName(), socketInVehicle ) == false )
        {
            Internal::setRiderMovementSuspended( *pRider, false );
            return MountResult::NoSocket;
        }
        Internal::setRiderPose( *pRider, seat.getRiderPoseParameter(), true );
        // 탑승자가 쓰러지면 강제 하차 — 규칙이라 타는 순간 붙인다.
        if ( pRider->getComponent<RiderDownWatcherComponent>() == nullptr )
            (void)pRider->addComponent<RiderDownWatcherComponent>();

        seat._occupant = pRider->getHandle();
        if ( seat.isDriverSeat() )
        {
            seat._occupantController        = pController->getHandle();
            seat._previousVehicleController = pVehiclePawn->getController();
            pController->possess( *pVehiclePawn );
        }
        return MountResult::Mounted;
    }

    bool MountUtil::dismount( PawnComponent& riderPawn, bool bForced )
    {
        using Internal                 = MountUtilInternal;
        VehicleSeatComponent* pSeat    = findSeatOf( riderPawn );
        GameObject*           pRider   = riderPawn.getOwner();
        GameObjectManager*    pManager = Internal::findManager( riderPawn );
        if ( pSeat == nullptr || pRider == nullptr || pManager == nullptr )
            return false;
        GameObject*     pVehicle     = pSeat->getOwner();
        SceneComponent* pVehicleRoot = pVehicle != nullptr ? pVehicle->getPrimarySceneComponent() : nullptr;
        SceneComponent* pRiderRoot   = pRider->getPrimarySceneComponent();

        // 소켓에서 뗀다(지금 월드 자리를 지킨다). 강제(쓰러짐)면 물리 바디(래그돌)가 있으면 물리로 떨어지고 걷기를 켜지 않는다.
        // 강제가 아니면 하차 자리로 옮긴다.
        SocketBindingComponent* pBinding           = pRider->getComponent<SocketBindingComponent>();
        const bool              bReleasedToPhysics = bForced && pBinding != nullptr && pBinding->release( SocketReleaseMode::Physics );
        if ( pBinding != nullptr && bReleasedToPhysics == false )
            (void)pBinding->release( SocketReleaseMode::Animated );
        if ( bForced == false && pVehicleRoot != nullptr && pRiderRoot != nullptr )
        {
            const float3 exitSpot = Internal::findExitSpot( *pManager, *pRider, *pSeat, *pVehicleRoot );
            pRiderRoot->teleportTo( exitSpot );
            float3 rotation = pRiderRoot->getLocalRotation();
            rotation._x     = 0.0f;
            rotation._z     = 0.0f;
            pRiderRoot->setLocalRotation( rotation );
        }
        Internal::setRiderPose( *pRider, pSeat->getRiderPoseParameter(), false );
        if ( bReleasedToPhysics == false )
            Internal::setRiderMovementSuspended( *pRider, false );

        // 운전석이면 탔을 때의 조종자가 탑승자를 다시 쥐고(탈것 폰은 놓여 의도 0 — 선다), 탈 때 탈것을 쥐고 있던 조종자(말 AI)가 탈것을 다시 쥔다.
        ControllerComponent* pController                = pSeat->_occupantController.isValid()
                                                            ? static_cast<ControllerComponent*>( pManager->resolveComponent( pSeat->_occupantController ) )
                                                            : nullptr;
        ControllerComponent* pPreviousVehicleController = pSeat->_previousVehicleController.isValid()
                                                            ? static_cast<ControllerComponent*>( pManager->resolveComponent( pSeat->_previousVehicleController ) )
                                                            : nullptr;
        pSeat->_occupant                                = GameObjectHandle{};
        pSeat->_occupantController                      = ComponentHandle{};
        pSeat->_previousVehicleController               = ComponentHandle{};
        if ( pController != nullptr )
            pController->possess( riderPawn );
        PawnComponent* pVehiclePawn = pVehicle != nullptr ? pVehicle->getComponent<PawnComponent>() : nullptr;
        if ( pPreviousVehicleController != nullptr && pVehiclePawn != nullptr && pVehiclePawn->isPossessed() == false )
            pPreviousVehicleController->possess( *pVehiclePawn );
        return true;
    }

    VehicleSeatComponent* MountUtil::findSeatOf( const PawnComponent& riderPawn )
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

    VehicleSeatComponent* MountUtil::findNearestFreeSeat( const PawnComponent& riderPawn )
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
            const GameObject*     pVehicle     = pSeat != nullptr ? pSeat->getOwner() : nullptr;
            const SceneComponent* pVehicleRoot = pVehicle != nullptr && pVehicle != pRider ? pVehicle->getPrimarySceneComponent() : nullptr;
            if ( pVehicleRoot == nullptr || pSeat->isFree() == false || pSeat->isActive() == false )
                continue;
            const float3  vehiclePosition = pVehicleRoot->getWorldPosition();
            const float32 deltaX          = riderPosition._x - vehiclePosition._x;
            const float32 deltaZ          = riderPosition._z - vehiclePosition._z;
            const float32 distance        = MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
            if ( distance > pSeat->getEnterDistance() )
                continue;
            // 운전석 먼저, 같은 종류끼리는 가까운 것.
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
