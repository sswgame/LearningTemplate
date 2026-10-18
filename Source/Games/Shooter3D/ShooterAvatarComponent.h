/**
 * @file ShooterAvatarComponent.h
 * @brief 플레이어의 보이는 몸 — 플레이어 컴포넌트의 상태를 읽어 자기 자리 · 요 · 애니메이터 파라미터 · 상체 레이어를 맞춥니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Movement/LocomotionMath.h"

namespace sw
{
    class AnimationClip;
    class SkeletalAnimatorComponent;

    /**
     * @class ShooterAvatarComponent
     * @brief 몸 프리팹(`prefabs/player_body.prefab.xml` — 스킨드 메시 · 애니메이터 · 외형과 같은 오브젝트)의 컴포넌트입니다. 플레이어가 세우고 `setPlayer` 로 잇습니다.
     * @details `TickGroup::PostPhysics` 에서 플레이어(`DuringPhysics` 에서 움직인 뒤)의 발 · 보는 요 · 수평 속도 · 땅 위 · 사격 · 맞음 · 쓰러짐을 **읽기만** 하고
     *          자기 오브젝트에만 씁니다. 그래프(`data/anim/adventurer.animgraph.json`) 파라미터는 `Move`(0 서기 · 1 걷기 · 2 달리기 · 3 뒷걸음 · 4 왼쪽 ·
     *          5 오른쪽 옆걸음 · 6 공중) · `Hit`(트리거) · `Dead` · `AimWeight` · `ShootWeight` 입니다. 상체는 그래프 데이터의 레이어 둘(`Aim` · `Shoot`,
     *          마스크 `data/anim/masks/upper_body.bonemask.json`)이 덮고, 이 컴포넌트는 두 가중치 파라미터만 씁니다 — 겨눈 자세는 늘, 쏘는 동작은 막 쏜 동안.
     *          맞음 · 쓰러짐 동안은 가중치를 내려 온몸 클립이 보이게 합니다. 이동 방향은 히스테리시스 +
     *          최소 유지 시간(`LocomotionDirectionFilter`)으로 골라 대각선에서 상태가 오가지 않습니다.
     *          발 미끄러짐은 재생 속도 매칭으로 줄입니다. 지금 이동 클립의 발 속도(`LocomotionSpeedUtil::computeFootSpeed`, 클립마다 한 번 측정)로 실제 수평 속도를 나눠
     *          [`_minPlayRate`, `_maxPlayRate`] 로 자른 값을 애니메이터 재생 속도로 씁니다. 범위를 넘는 몫(빠른 뒷걸음)은 보폭 워핑이 없어 남습니다.
     *          왼손을 총에 붙이는 손 IK · 맞은 부위 · 래그돌은 PoseModifier · 물리 쪽이 들어오면 그 자리(무기 소켓 `MainHand.SupportHand`)를 씁니다.
     */
    REFLECT( Category = "Shooter3D", DisplayName = "Shooter Avatar", Tooltip = "Visible player body: follows the player and drives the animator from its movement and firing" )
    class ShooterAvatarComponent : public Component
    {
    public:
        REFLECT_BODY();

        ShooterAvatarComponent();
        virtual ~ShooterAvatarComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 따라갈 플레이어(`ShooterPlayerComponent` 를 가진 오브젝트)입니다. */
        void setPlayer( GameObjectHandle player ) { _player = player; }

    private:
        /** @brief 지금 상태 클립의 발 속도에 수평 이동 속도를 맞춘 재생 속도입니다. 서 있거나 공중이거나 이동 클립이 아니면 1 입니다. */
        float32 computeLocomotionPlayRate( const SkeletalAnimatorComponent& animator, LocomotionDirection direction, float32 moveSpeed );

    private:
        PROPERTY( Category = "Avatar", DisplayName = "Player", Tooltip = "Object with the ShooterPlayerComponent this body follows" )
        GameObjectHandle _player;
        PROPERTY( Category = "Avatar", DisplayName = "Walk Threshold", Tooltip = "Below this speed the body stands", Min = 0.0, Units = "m/s" )
        float32 _walkThreshold;
        PROPERTY( Category = "Avatar", DisplayName = "Run Threshold", Tooltip = "At or above this forward speed the run clip plays", Min = 0.0, Units = "m/s" )
        float32 _runThreshold;
        PROPERTY( Category = "Avatar", DisplayName = "Hit Pause", Tooltip = "Seconds the upper-body layers step aside for the hit reaction", Min = 0.0, Units = s )
        float32 _hitPause;
        PROPERTY( Category = "Avatar", DisplayName = "Turn Rate", Tooltip = "Fastest the body turns to the view direction", Min = 0.0, Units = "rad/s" )
        float32 _turnRate;
        PROPERTY( Category = "Avatar", DisplayName = "Layer Blend Rate", Tooltip = "How fast the aim and shoot layer weights follow their target", Min = 0.0 )
        float32 _layerBlendRate;
        PROPERTY( Category = "Avatar", DisplayName = "Left Foot Bone", Tooltip = "Bone used to measure the foot speed of locomotion clips" )
        string _leftFootBone;
        PROPERTY( Category = "Avatar", DisplayName = "Right Foot Bone", Tooltip = "Bone used to measure the foot speed of locomotion clips" )
        string _rightFootBone;
        PROPERTY( Category = "Avatar", DisplayName = "Min Play Rate", Tooltip = "Slowest play rate speed matching may choose", Min = 0.0 )
        float32 _minPlayRate;
        PROPERTY( Category = "Avatar", DisplayName = "Max Play Rate", Tooltip = "Fastest play rate speed matching may choose", Min = 0.0 )
        float32 _maxPlayRate;
        PROPERTY( Category = "Avatar", DisplayName = "Min Locomotion Foot Speed", Tooltip = "A clip whose feet move slower than this is not a locomotion clip and keeps its play rate", Min = 0.0, Units = "m/s" )
        float32 _minLocomotionFootSpeed;

        LocomotionDirectionFilter                    _locomotion;
        unordered_map<const AnimationClip*, float32> _mapClipFootSpeed; ///< 클립마다 한 번 측정한 발 속도(m/s)

        float32 _aimWeight;
        float32 _shootWeight;
        float32 _hitTimer;
        float32 _bodyYaw; ///< 몸이 보는 요 — 시점을 각속도 상한으로 따라간다
        uint32  _lastHitReaction;
        uint8   _bYawReady : 1;
        uint8   _reserved  : 7;
    };
} // namespace sw
