#include "pch.h"

#include "Games/VoxelCraft/VoxelAutoPlayControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"
#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"

#include "Games/VoxelCraft/VoxelDirectorComponent.h"
#include "Games/VoxelCraft/VoxelPlayerComponent.h"

namespace sw
{
    VoxelAutoPlayControllerComponent::VoxelAutoPlayControllerComponent()
        : _cycleSeconds{ 12.0f }
        , _turnRightRate{ 0.25f }
        , _turnLeftRate{ -0.15f }
        , _lookDownPitch{ -0.35f }
        , _breakStart{ 3.0f }
        , _breakEnd{ 6.5f }
        , _placeAt{ 9.0f }
        , _focusDistance{ 10.0f }
        , _walkLookAhead{ 4.0f }
        , _blockLookAhead{ 0.6f }
        , _timer{ 0.0f }
    {
    }

    void VoxelAutoPlayControllerComponent::think( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        GameObject*                   pOwner   = pawn.getOwner();
        GameObjectManager*            pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const VoxelPlayerComponent*   pPlayer  = pOwner != nullptr ? pOwner->getComponent<VoxelPlayerComponent>() : nullptr;
        const VoxelDirectorComponent* pDirector =
            pManager != nullptr && pPlayer != nullptr ? GameDirectorComponent::resolve<VoxelDirectorComponent>( *pManager, pPlayer->getDirector() ) : nullptr;
        const bool bNotPlaying = pDirector == nullptr || pDirector->isStarted() == false || context._deltaTime <= 0.0f;
        if ( bNotPlaying )
            return;
        const float32 deltaTime     = MathUtil::min( context._deltaTime, 0.1f );
        const float32 previousCycle = MathUtil::fmod( _timer, _cycleSeconds );
        _timer += deltaTime;
        const float32 cycle = MathUtil::fmod( _timer, _cycleSeconds );

        // 바라보기 — 요는 천천히 돌고 피치는 아래를 조금. 그 방향의 점을 초점으로 두면 기반이 조종 회전을 돌린다.
        const float32 yaw   = getControlYaw() + deltaTime * ( cycle < _cycleSeconds * 0.5f ? _turnRightRate : _turnLeftRate );
        const float32 pitch = _lookDownPitch;
        const float3  flat  = float3{ MathUtil::sin( yaw ), 0.0f, MathUtil::cos( yaw ) };
        const float3  look  = float3{ flat._x * MathUtil::cos( pitch ), MathUtil::sin( pitch ), flat._z * MathUtil::cos( pitch ) };
        const float3  eye   = pPlayer->getBody().getEyePosition();
        const float3  feet  = pPlayer->getBody().getPosition();
        setFocus( eye + look * _focusDistance );
        moveTo( feet + flat * _walkLookAhead );

        // 앞이 막혔거나 물 속이면 뛴다(물에서는 위로 헤엄).
        const VoxelWorld& world = pDirector->getWorld();
        const float3      ahead = feet + flat * _blockLookAhead;
        const bool        bBlocked =
            world.isSolid( static_cast<int32>( MathUtil::floor( ahead._x ) ), static_cast<int32>( MathUtil::floor( feet._y + 0.5f ) ), static_cast<int32>( MathUtil::floor( ahead._z ) ) );
        holdButton( hashed_string( "Voxel.Jump" ), bBlocked || pPlayer->getBody().isInWater() );
        holdButton( hashed_string( "Voxel.Break" ), cycle > _breakStart && cycle < _breakEnd );
        if ( previousCycle < _placeAt && cycle >= _placeAt )
            pressButton( hashed_string( "Voxel.Place" ) );
    }
} // namespace sw
