/**
 * @file ShooterAvatarComponent.h
 * @brief 플레이어의 보이는 몸 — 플레이어 컴포넌트의 상태를 읽어 자기 자리 · 요 · 애니메이터 파라미터 · 상체 레이어를 맞춥니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Movement/LocomotionMath.h"

namespace sw
{
    /**
     * @class ShooterAvatarComponent
     * @brief 몸 프리팹(`prefabs/player_body.prefab.xml` — 스킨드 메시 · 애니메이터 · 외형과 같은 오브젝트)의 컴포넌트입니다. 플레이어가 세우고 `setPlayer` 로 잇습니다.
     * @details `TickGroup::PostPhysics` 에서 플레이어(`DuringPhysics` 에서 움직인 뒤)의 발 · 보는 요 · 수평 속도 · 땅 위 · 사격 · 맞음 · 쓰러짐을 **읽기만** 하고
     *          자기 오브젝트에만 씁니다. 그래프(`data/anim/adventurer.animgraph.json`) 파라미터는 `Move`(0 서기 · 1 걷기 · 2 달리기 · 3 뒷걸음 · 4 왼쪽 ·
     *          5 오른쪽 옆걸음 · 6 공중) · `Hit`(트리거) · `Dead` 이고, 상체(`_upperBodyBone` 아래)는 레이어 둘 — 총을 겨눈 자세(`_aimLayerClip`)와 쏘는
     *          동작(`_shootLayerClip`, 막 쏜 동안)이 덮습니다. 맞음 · 쓰러짐 동안은 레이어를 내려 온몸 클립이 보이게 합니다. 이동 방향은 히스테리시스 +
     *          최소 유지 시간(`LocomotionDirectionFilter`)으로 골라 대각선에서 상태가 오가지 않습니다.
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
        /** @brief 레이어 둘을 겁니다(클립 폴더가 정해진 뒤 한 번). */
        void ensureLayers();

    private:
        PROPERTY( Category = "Avatar", DisplayName = "Player", Tooltip = "Object with the ShooterPlayerComponent this body follows" )
        GameObjectHandle _player;
        PROPERTY( Category = "Avatar", DisplayName = "Aim Layer Clip", Tooltip = "Upper-body clip held while armed" )
        string _aimLayerClip;
        PROPERTY( Category = "Avatar", DisplayName = "Shoot Layer Clip", Tooltip = "Upper-body clip blended in right after a shot" )
        string _shootLayerClip;
        PROPERTY( Category = "Avatar", DisplayName = "Upper Body Bone", Tooltip = "Root bone of the upper-body layer mask" )
        string _upperBodyBone;
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

        LocomotionDirectionFilter _locomotion;

        float32 _aimWeight;
        float32 _shootWeight;
        float32 _hitTimer;
        float32 _bodyYaw; ///< 몸이 보는 요 — 시점을 각속도 상한으로 따라간다
        uint32  _lastHitReaction;
        int32   _aimLayer;
        int32   _shootLayer;
        uint8   _bLayersReady : 1;
        uint8   _bYawReady    : 1;
        uint8   _reserved     : 6;
    };
} // namespace sw
