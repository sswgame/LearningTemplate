#include "pch.h"

#include "Games/VoxelCraft/VoxelAutoPlayControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Feature/World/Voxel/Rule/VoxelWorld.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

#include "Games/VoxelCraft/VoxelGameModeComponent.h"
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
        requireBehaviorTree();
    }

    void VoxelAutoPlayControllerComponent::updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        Blackboard&                   blackboard = getBlackboard();
        GameObject*                   pOwner     = pawn.getOwner();
        GameObjectManager*            pManager   = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const VoxelPlayerComponent*   pPlayer    = pOwner != nullptr ? pOwner->getComponent<VoxelPlayerComponent>() : nullptr;
        const VoxelGameModeComponent* pGameMode =
            pManager != nullptr && pPlayer != nullptr ? GameModeComponent::resolve<VoxelGameModeComponent>( *pManager, pPlayer->getGameMode() ) : nullptr;
        const bool bPlaying = pGameMode != nullptr && pGameMode->isStarted() && context._deltaTime > 0.0f;
        blackboard.setBool( "Playing", bPlaying );
        if ( bPlaying == false )
            return; // 트리가 아무것도 바꾸지 않는다 — 시계도 서 있다
        const float32 deltaTime     = MathUtil::min( context._deltaTime, 0.1f );
        const float32 previousCycle = MathUtil::fmod( _timer, _cycleSeconds );
        _timer += deltaTime;
        const float32 cycle = MathUtil::fmod( _timer, _cycleSeconds );

        // 바라보기: 요(yaw)는 천천히 돌고 피치는 아래를 조금 봅니다. 그 방향의 점을 초점으로 두면 베이스 클래스가 조종 회전을 돌립니다.
        const float32 yaw   = getControlYaw() + deltaTime * ( cycle < _cycleSeconds * 0.5f ? _turnRightRate : _turnLeftRate );
        const float32 pitch = _lookDownPitch;
        const float3  flat  = float3{ MathUtil::sin( yaw ), 0.0f, MathUtil::cos( yaw ) };
        const float3  look  = float3{ flat._x * MathUtil::cos( pitch ), MathUtil::sin( pitch ), flat._z * MathUtil::cos( pitch ) };
        const float3  eye   = pPlayer->getBody().getEyePosition();
        const float3  feet  = pPlayer->getBody().getPosition();
        blackboard.setVector( "FocusPoint", eye + look * _focusDistance );
        blackboard.setVector( "WalkTarget", feet + flat * _walkLookAhead );

        // 앞이 막혔는지(물에서는 트리가 위로 헤엄친다).
        const VoxelWorld& world = pGameMode->getWorld();
        const float3      ahead = feet + flat * _blockLookAhead;
        blackboard.setBool( "Blocked", world.isSolid( static_cast<int32>( MathUtil::floor( ahead._x ) ), static_cast<int32>( MathUtil::floor( feet._y + 0.5f ) ),
                                                      static_cast<int32>( MathUtil::floor( ahead._z ) ) ) );
        blackboard.setBool( "InWater", pPlayer->getBody().isInWater() );
        // 주기 시계와 구간 — 놓기는 PlaceAt 을 지나는 판단 한 번이라 지난 시각도 적는다.
        blackboard.setFloat( "CycleTime", cycle );
        blackboard.setFloat( "PreviousCycleTime", previousCycle );
        blackboard.setFloat( "BreakStart", _breakStart );
        blackboard.setFloat( "BreakEnd", _breakEnd );
        blackboard.setFloat( "PlaceAt", _placeAt );
    }
} // namespace sw
