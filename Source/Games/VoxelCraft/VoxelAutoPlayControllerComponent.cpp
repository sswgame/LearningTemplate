#include "pch.h"

#include "Games/VoxelCraft/VoxelAutoPlayControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Kits/Simulation/Voxel/VoxelWorld.h"

#include "Games/VoxelCraft/VoxelDirectorComponent.h"
#include "Games/VoxelCraft/VoxelPlayerComponent.h"

namespace sw
{
    namespace
    {
        struct VoxelAutoPlayControllerComponentInternal
        {
            static constexpr float32 kCycleSeconds   = 12.0f;
            static constexpr float32 kTurnRightRate  = 0.25f; ///< 주기 앞 절반의 요 속도(rad/s)
            static constexpr float32 kTurnLeftRate   = -0.15f;
            static constexpr float32 kLookDownPitch  = -0.35f;
            static constexpr float32 kBreakStart     = 3.0f;
            static constexpr float32 kBreakEnd       = 6.5f;
            static constexpr float32 kPlaceAt        = 9.0f;
            static constexpr float32 kFocusDistance  = 10.0f; ///< 바라볼 점까지(조종 회전을 그 방향으로 돌린다)
            static constexpr float32 kWalkLookAhead  = 4.0f;  ///< 걸을 목적지까지(매 틱 앞으로 다시 건다)
            static constexpr float32 kBlockLookAhead = 0.6f;  ///< 막힘을 보는 앞 거리
        };
    } // namespace
} // namespace sw

namespace sw
{
    VoxelAutoPlayControllerComponent::VoxelAutoPlayControllerComponent()
        : _timer{ 0.0f }
    {
    }

    void VoxelAutoPlayControllerComponent::think( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        using Internal                         = VoxelAutoPlayControllerComponentInternal;
        GameObject*                   pOwner   = pawn.getOwner();
        GameObjectManager*            pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const VoxelPlayerComponent*   pPlayer  = pOwner != nullptr ? pOwner->getComponent<VoxelPlayerComponent>() : nullptr;
        const VoxelDirectorComponent* pDirector =
            pManager != nullptr && pPlayer != nullptr ? GameDirectorComponent::resolve<VoxelDirectorComponent>( *pManager, pPlayer->getDirector() ) : nullptr;
        if ( pDirector == nullptr || pDirector->isStarted() == false || context._deltaTime <= 0.0f )
            return;
        const float32 deltaTime     = MathUtil::min( context._deltaTime, 0.1f );
        const float32 previousCycle = MathUtil::fmod( _timer, Internal::kCycleSeconds );
        _timer += deltaTime;
        const float32 cycle = MathUtil::fmod( _timer, Internal::kCycleSeconds );

        // 바라보기 — 요는 천천히 돌고 피치는 아래를 조금. 그 방향의 점을 초점으로 두면 기반이 조종 회전을 돌린다.
        const float32 yaw   = getControlYaw() + deltaTime * ( cycle < Internal::kCycleSeconds * 0.5f ? Internal::kTurnRightRate : Internal::kTurnLeftRate );
        const float32 pitch = Internal::kLookDownPitch;
        const float3  flat  = float3{ MathUtil::sin( yaw ), 0.0f, MathUtil::cos( yaw ) };
        const float3  look  = float3{ flat._x * MathUtil::cos( pitch ), MathUtil::sin( pitch ), flat._z * MathUtil::cos( pitch ) };
        const float3  eye   = pPlayer->getBody().getEyePosition();
        const float3  feet  = pPlayer->getBody().getPosition();
        setFocus( eye + look * Internal::kFocusDistance );
        moveTo( feet + flat * Internal::kWalkLookAhead );

        // 앞이 막혔거나 물 속이면 뛴다(물에서는 위로 헤엄).
        const VoxelWorld& world = pDirector->getWorld();
        const float3      ahead = feet + flat * Internal::kBlockLookAhead;
        const bool        bBlocked =
            world.isSolid( static_cast<int32>( MathUtil::floor( ahead._x ) ), static_cast<int32>( MathUtil::floor( feet._y + 0.5f ) ), static_cast<int32>( MathUtil::floor( ahead._z ) ) );
        holdButton( hashed_string( "Voxel.Jump" ), bBlocked || pPlayer->getBody().isInWater() );
        holdButton( hashed_string( "Voxel.Break" ), cycle > Internal::kBreakStart && cycle < Internal::kBreakEnd );
        if ( previousCycle < Internal::kPlaceAt && cycle >= Internal::kPlaceAt )
            pressButton( hashed_string( "Voxel.Place" ) );
    }
} // namespace sw
