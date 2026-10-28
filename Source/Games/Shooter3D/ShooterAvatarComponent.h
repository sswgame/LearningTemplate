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
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Character/Movement/LocomotionMath.h"

namespace sw
{
    class AnimationClip;
    class SkeletalAnimatorComponent;

    /**
     * @class ShooterAvatarComponent
     * @brief 몸 프리팹(`prefabs/player_body.prefab.xml` — 스킨드 메시 · 애니메이터 · 외형과 같은 오브젝트)의 컴포넌트입니다. 플레이어가 세우고 `setPlayer` 로 잇습니다.
     * @details `TickGroup::PostPhysics` 에서 플레이어(`DuringPhysics` 에서 움직인 뒤)의 발 · 보는 요 · 수평 속도 · 땅 위 · 사격 · 맞음 · 쓰러짐을 **읽기만** 하고
     *          자기 오브젝트에만 씁니다. 그래프(`data/anim/adventurer.animgraph.xml`) 파라미터는 `Move`(0 서기 · 1 걷기 · 2 달리기 · 3 뒷걸음 · 4 왼쪽 ·
     *          5 오른쪽 옆걸음 · 6 공중) · `Hit`(트리거) · `Dead` · `AimWeight` · `ShootWeight` 입니다. 상체는 그래프 데이터의 레이어 둘(`Aim` · `Shoot`,
     *          마스크 `data/anim/masks/upper_body.bonemask.xml`)이 덮고, 이 컴포넌트는 두 가중치 파라미터만 씁니다 — 겨눈 자세는 늘, 쏘는 동작은 막 쏜 동안.
     *          맞음 · 쓰러짐 동안은 가중치를 내려 온몸 클립이 보이게 합니다. 이동 방향은 히스테리시스 +
     *          최소 유지 시간(`LocomotionDirectionFilter`)으로 골라 대각선에서 상태가 오가지 않습니다.
     *          발 미끄러짐은 재생 속도 매칭으로 줄입니다. 지금 이동 클립의 발 속도(`LocomotionSpeedUtil::computeFootSpeed`, 클립마다 한 번 측정)로 실제 수평 속도를 나눠
     *          [`_minPlayRate`, `_maxPlayRate`] 로 자른 값을 애니메이터 재생 속도로 씁니다. 범위를 넘는 몫은 같은 오브젝트의 `PoseModifierComponent` 리그
     *          (`rigs/player_body.rig.xml`)의 보폭 워핑 노드(`_strideNode`)에 `stride_scale` · 모델 공간 이동 방향으로 넣습니다. 노드가 없으면 오류입니다.
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
        /**
         * @brief 1인칭이면 몸은 보이고 머리만 숨깁니다 — 같은 오브젝트의 리그(`PoseModifierComponent`)의 슬롯 `_firstPersonHeadSlot`(머리 `ScaleBone`)을 켜고,
         *        외형의 칸 `_listFirstPersonHiddenSlot`(머리 장비 · 손 무기) 부품을 숨깁니다. 3인칭이면 둘 다 되돌립니다.
         * @details 몸 무기는 1인칭에서 숨기고 시점 무기(플레이어의 `ViewWeapon`)가 보인다 — 조준선과 총구가 카메라 기준이어야 하기 때문이다.
         *          리그나 슬롯이 없으면 오류로 알린다(머리를 숨기지 못하면 눈이 머리 안에 있다).
         */
        void setFirstPerson( bool bFirstPerson );

        /** @brief 보폭 워핑 노드에 마지막으로 넣은 배율입니다(1 = 클립 그대로). 시나리오 프로브 `Shooter3D.StrideScale` 이 읽습니다. */
        float32 getLastStrideScale() const { return _lastStrideScale; }
        /** @brief 리그가 준비돼 보폭 워핑 값을 실제로 넣은 프레임 수입니다. 0 이면 리그가 메시에 아직 묶이지 않았다(`isRigReady` 전 프레임은 넣지 않는다). */
        uint32 getStrideAppliedCount() const { return _strideAppliedCount; }

    private:
        /** @brief 지금 상태 클립의 발 속도(m/s)입니다. 서 있거나 공중이거나 이동 클립이 아니면 0 입니다(재생 속도 · 보폭을 바꾸지 않는다). */
        float32 computeLocomotionFootSpeed( const SkeletalAnimatorComponent& animator, LocomotionDirection direction );
        /** @brief 보폭 워핑 노드에 배율과 모델 공간 이동 방향(+Z 앞, +X 오른쪽)을 넣습니다. */
        void applyStrideWarping( float32 strideScale, const float2& localVelocity );

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
        PROPERTY( Category = "First Person", DisplayName = "Head Rig Slot", Tooltip = "Rig weight slot of the ScaleBone node that hides the head in first person" )
        hashed_string _firstPersonHeadSlot;
        PROPERTY( Category = "First Person", DisplayName = "Hidden Appearance Slots", Tooltip = "Appearance slots whose parts are hidden in first person (head gear, the body's weapon)" )
        vector<hashed_string> _listFirstPersonHiddenSlot;
        PROPERTY( Category = "Avatar", DisplayName = "Stride Node", Tooltip = "StrideWarping node of this body's pose modifier rig that takes the rest of the speed matching" )
        string _strideNode;

        LocomotionDirectionFilter                    _locomotion;
        unordered_map<const AnimationClip*, float32> _mapClipFootSpeed; ///< 클립마다 한 번 측정한 발 속도(m/s)

        float32 _aimWeight;
        float32 _shootWeight;
        float32 _hitTimer;
        float32 _bodyYaw; ///< 몸이 보는 요 — 시점을 각속도 상한으로 따라간다
        float32 _lastStrideScale;
        uint32  _strideAppliedCount;
        uint32  _lastHitReaction;
        uint8   _bYawReady         : 1;
        uint8   _bStrideErrorShown : 1; ///< 노드가 없다는 오류를 한 번만 남긴다
        uint8   _reserved          : 6;
    };
} // namespace sw
