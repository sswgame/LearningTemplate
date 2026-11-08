/**
 * @file RigBodyInput.h
 * @brief 후처리 리그가 받는 몸 입력입니다. 외형(장비 · 캐릭터 프리셋)이 정한 발 들림(굽 높이), 지글 묶음 물성, 동작 배율, 맞음 충격을 리그 노드에 건넵니다.
 * @details 리그 데이터(`*.rig.xml`)는 어느 본이 흔들리는지(`Jiggle` 노드의 `bones` · `group`)만 적고, 얼마나 흔들리는지는 캐릭터마다 다른 프리셋(몸 프로필)이 정한다 —
 *          같은 스켈레톤 · 같은 리그를 여러 캐릭터가 나누면서 물성만 바꾼다(언리얼 AnimDynamics 의 노드 · 물성 분리, 검은사막 커스터마이즈의 "흔들림 정도" 슬라이더 자리).
 *          값은 게임 스레드에서 평가 밖에서만 바꾼다(노드 조절 `setNodeControl` 과 같은 규약). 충격은 게임 스레드가 쌓고 준비 단계(`RigInstance::prepare`)가 이번 프레임 값으로 옮긴다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief 지글 묶음 하나(가슴 · 배 · 허벅지)의 물성입니다. 몸 프로필 데이터(`<Jiggle group …/>`)가 이 칸으로 읽힌다 — 한 스키마를 리그와 외형이 함께 쓴다.
     * @details 스프링 사슬(`RigSpringChain`)의 값으로 옮겨 쓴다. 동작 배율(`RigBodyInput::getMotionScale`)이 느슨함(1 − 강성)과 충격에 곱해진다.
     */
    REFLECT()
    struct SW_API RigJiggleGroupParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Jiggle group name the rig's Jiggle nodes name (Bust, Belly, Thigh)" )
        hashed_string _group{};
        PROPERTY( Min = 0.0, Max = 1.0, Units = ratio, Tooltip = "Pull towards the animated pose per step" )
        float32 _stiffness{ 0.2f };
        PROPERTY( Min = 0.0, Max = 1.0, Units = ratio, Tooltip = "Velocity lost per step" )
        float32 _damping{ 0.15f };
        PROPERTY( Tooltip = "Multiplier on world gravity (sag)" )
        float32 _gravityScale{ 0.3f };
        PROPERTY( Min = 0.0, Max = 90.0, Units = deg, Tooltip = "Largest swing away from the animated direction; 0 = unlimited" )
        float32 _maxSwingDegrees{ 20.0f };
        PROPERTY( Min = 0.0, Tooltip = "Multiplier on hit / landing impulses" )
        float32 _impulseScale{ 1.0f };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Particle radius against the colliders" )
        float32 _particleRadius{ 0.02f };
    };
} // namespace sw

namespace sw
{
    /** @class RigBodyInput @brief 파일 머리말 참고. 리그 인스턴스가 하나 가진다(`RigInstance::getBodyInput`). */
    class SW_API RigBodyInput
    {
    public:
        /** @brief 동작 배율의 범위입니다. 0 은 애니메이션 그대로(흔들림 없음), 1 은 프리셋 그대로, 2 는 두 배로 느슨하다. */
        static constexpr float32 kMaxMotionScale = 2.0f;

        RigBodyInput();

        /** @brief 발 들림(m)입니다. 굽 높이만큼 발바닥이 땅 위에 있다 — 발 디딤 IK 가 발 목표를 땅 + 이 값에 둔다. */
        void    setFootLift( float32 meters ) { _footLift = meters; }
        float32 getFootLift() const { return _footLift; }

        /** @brief 동작 배율(0..`kMaxMotionScale`)입니다. 상태 태그(달리기 · 공격)가 정한다. 범위 밖은 자른다. */
        void    setMotionScale( float32 scale );
        float32 getMotionScale() const { return _motionScale; }

        /** @brief 맞음 · 착지 충격(월드 속도 m/s)을 쌓습니다(게임 스레드). 다음 준비 단계가 이번 프레임 값으로 옮긴다. */
        void addImpulse( const float3& worldVelocity ) { _pendingImpulse += worldVelocity; }
        /** @brief 준비 단계(게임 스레드)에서 쌓인 충격을 이번 프레임 값으로 옮깁니다. */
        void beginFrame();
        /** @brief 이번 프레임에 노드가 쓸 충격입니다(워커가 읽는다). */
        const float3& getFrameImpulse() const { return _frameImpulse; }

        /** @brief 지글 묶음 물성을 바꿉니다. 리그의 `Jiggle` 노드가 가리키는 묶음이 모두 있는지는 `RigJiggleNode::checkGroups` 가 본다. */
        void                                setJiggleGroups( vector<RigJiggleGroupParams> listGroup ) { _listJiggleGroup = std::move( listGroup ); }
        const vector<RigJiggleGroupParams>& getJiggleGroups() const { return _listJiggleGroup; }
        /** @brief 이름의 지글 묶음입니다. 없으면 nullptr 입니다. */
        const RigJiggleGroupParams* findJiggleGroup( const hashed_string& group ) const;

    private:
        vector<RigJiggleGroupParams> _listJiggleGroup;
        float3                       _pendingImpulse;
        float3                       _frameImpulse;
        float32                      _footLift;
        float32                      _motionScale;
    };
} // namespace sw
