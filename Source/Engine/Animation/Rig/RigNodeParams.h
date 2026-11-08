/**
 * @file RigNodeParams.h
 * @brief 리그 파일(`*.rig.xml`)의 원소마다 받는 칸입니다 — 에셋 루트 · 대상 · 노드 공통 키 · 노드 종류마다의 키와 그 목록 원소(`<item>`).
 * @details `RigXMLReader::readFields` 가 이 스키마로 속성을 읽는다. 칸의 초기값이 "적지 않음" 값이고, 범위는 PROPERTY( Min · Max ) 가 정본이다.
 *          이름 목록(`<bones><item>…`)과 객체 목록(`<feet>` 등)은 자식 요소라 노드가 손으로 푼다. 파일 키는 밑줄 이름이라 칸마다 `Name` 을 적는다.
 *          도 단위로 적는 각은 파일 단위 그대로(`Units = deg`) 읽고, 노드가 라디안이나 쿼터니언으로 옮긴다.
 *          언리얼 애님 노드의 UPROPERTY 파라미터 구조체 자리다 — 키 · 기본값 · 범위를 한 곳에 둔다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Rig/RigIKSolver.h"
#include "Engine/Animation/Rig/RigSpringChain.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 에셋 루트(`<Rig planar>`)의 칸입니다. */
    REFLECT()
    struct SW_API RigAssetRootParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "planar" )
        bool _bPlanar{ SW_FALSE };
    };

    /** @brief 대상(`<targets><item …/>`) 하나의 칸입니다. `bone` · `socket` · `object` 중 하나만 적는다(`RigAsset::parseTarget` 이 본다). */
    REFLECT()
    struct SW_API RigTargetParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "name", Required )
        hashed_string _name{};
        PROPERTY( Name = "bone" )
        hashed_string _bone{};
        PROPERTY( Name = "socket" )
        hashed_string _socket{};
        PROPERTY( Name = "object" )
        hashed_string _object{};
        PROPERTY( Name = "skeletalMesh" )
        hashed_string _skeletalMesh{};
        PROPERTY( Name = "space" )
        hashed_string _space{};
        PROPERTY( Name = "translation", Units = m )
        float3 _translation{};
        PROPERTY( Name = "rotation", Units = deg )
        float3 _rotation{};
        PROPERTY( Name = "trace_from" )
        hashed_string _traceFrom{};
        PROPERTY( Name = "trace_direction" )
        float3 _traceDirection{};
        PROPERTY( Name = "trace_distance", Min = 0.0, Units = m )
        float32 _traceDistance{ 0.0f };
        PROPERTY( Name = "surface_offset", Units = m )
        float32 _surfaceOffset{ 0.0f };
        PROPERTY( Name = "blend_speed", Min = 0.0 )
        float32 _blendSpeed{ 0.0f };
    };

    /** @brief 모든 노드가 받는 공통 키입니다(`RigNode::parseCommon`). `type` 은 에셋이 노드를 만들 때 따로 읽는다. */
    REFLECT()
    struct SW_API RigNodeCommonParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "name", Required )
        hashed_string _name{};
        PROPERTY( Name = "weight", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _weight{ 1.0f };
        PROPERTY( Name = "weight_curve" )
        hashed_string _weightCurve{};
        PROPERTY( Name = "weight_slot" )
        hashed_string _weightSlot{};
        /** @brief 이 노드가 켜지는 가장 높은 LOD 등급(`AnimationLODState::_lodGrade`)입니다. -1 이면 모든 등급에서 켠다(언리얼 노드 LOD Threshold 의 -1). */
        PROPERTY( Name = "max_lod", Min = -1 )
        int32 _maxLODGrade{ -1 };
        PROPERTY( Name = "enabled" )
        bool _bEnabled{ SW_TRUE }; ///< 적지 않으면 켜짐(에디터가 끈 노드만 `enabled="false"` 를 쓴다)
    };
} // namespace sw

namespace sw
{
    // ---- 제약 노드(RigConstraintNodes.cpp)

    /** @brief 위치 · 회전 제약(`Position` · `Rotation`)의 칸입니다. */
    REFLECT()
    struct SW_API RigConstraintParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "bone", Required )
        hashed_string _bone{};
        PROPERTY( Name = "target", Required )
        hashed_string _target{};
        PROPERTY( Name = "maintain_offset" )
        bool _bMaintainOffset{ false };
    };

    /** @brief 변환 복사(`CopyTransform`)의 칸입니다. 위치 · 회전을 고를 수 있다. */
    REFLECT()
    struct SW_API RigCopyTransformParams : public RigConstraintParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "position" )
        bool _bPosition{ true };
        PROPERTY( Name = "rotation" )
        bool _bRotation{ true };
    };

    /** @brief 부모 바꾸기(`ParentSwitch`)의 칸입니다. 부모 목록(`parents`)은 이름 목록이라 노드가 푼다. */
    REFLECT()
    struct SW_API RigParentSwitchParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "bone", Required )
        hashed_string _bone{};
        PROPERTY( Name = "initial" )
        uint32 _initial{ 0 };
        PROPERTY( Name = "settle_seconds", Min = 0.0, Units = s )
        float32 _settleSeconds{ 0.0f };
    };

    /** @brief 거리 제한(`Distance`)의 칸입니다. `min <= max` 는 노드가 본다. */
    REFLECT()
    struct SW_API RigDistanceParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "bone", Required )
        hashed_string _bone{};
        PROPERTY( Name = "target", Required )
        hashed_string _target{};
        PROPERTY( Name = "min", Min = 0.0, Units = m )
        float32 _min{ 0.0f };
        PROPERTY( Name = "max", Required, Min = 0.0, Units = m )
        float32 _max{ 1.0f };
    };

    /** @brief 회전 범위 제한(`LimitRotation`)의 칸입니다. 각은 [피치, 요, 롤] 도 단위다. */
    REFLECT()
    struct SW_API RigLimitRotationParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "bone", Required )
        hashed_string _bone{};
        PROPERTY( Name = "min_degrees", Required, Units = deg )
        float3 _minDegrees{};
        PROPERTY( Name = "max_degrees", Required, Units = deg )
        float3 _maxDegrees{};
    };

    /** @brief 트위스트 분배(`TwistDistribution`)의 칸입니다. */
    REFLECT()
    struct SW_API RigTwistDistributionParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "source", Required )
        hashed_string _source{};
        PROPERTY( Name = "axis" )
        float3 _axis{ 0.0f, 1.0f, 0.0f };
    };

    /** @brief 본과 몫 한 쌍(`<item bone weight>`)입니다 — 트위스트 분배가 쓴다. */
    REFLECT()
    struct SW_API RigBoneWeightParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "bone", Required )
        hashed_string _bone{};
        PROPERTY( Name = "weight", Required )
        float32 _weight{ 0.0f };
    };

    /** @brief 본 스케일(`ScaleBone`)의 칸입니다. 성분이 음수가 아닌지는 노드가 본다. */
    REFLECT()
    struct SW_API RigScaleBoneParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "bone", Required )
        hashed_string _bone{};
        PROPERTY( Name = "scale" )
        float3 _scale{};
    };
} // namespace sw

namespace sw
{
    // ---- IK 노드(RigIKNodes.cpp)

    /** @brief 2 본 IK(`TwoBoneIK`)의 칸입니다. */
    REFLECT()
    struct SW_API RigTwoBoneIKParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "root", Required )
        hashed_string _root{};
        PROPERTY( Name = "mid", Required )
        hashed_string _mid{};
        PROPERTY( Name = "end", Required )
        hashed_string _end{};
        PROPERTY( Name = "target", Required )
        hashed_string _target{};
        PROPERTY( Name = "pole" )
        hashed_string _pole{};
        PROPERTY( Name = "match_rotation" )
        bool _bMatchRotation{ false };
        PROPERTY( Name = "keep_end_rotation" )
        bool _bKeepEndRotation{ true };
    };

    /** @brief 사슬 IK(`FABRIKChain`)의 칸입니다. 본 목록(`bones`)과 관절 제한(`limits`)은 노드가 푼다. */
    REFLECT()
    struct SW_API RigChainIKParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "target", Required )
        hashed_string _target{};
        PROPERTY( Name = "iterations", Min = 1 )
        uint32 _iterations{ RigChainSettings{}._iterationCount };
        PROPERTY( Name = "tolerance", Min = 0.0, Units = m )
        float32 _tolerance{ RigChainSettings{}._tolerance };
        PROPERTY( Name = "match_rotation" )
        bool _bMatchRotation{ false };
    };

    /** @brief CCD 사슬 IK(`CCDChain`)의 칸입니다 — 한 관절이 한 번에 도는 상한을 더 받는다. */
    REFLECT()
    struct SW_API RigCCDChainParams : public RigChainIKParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "max_step_degrees", Min = 0.0, Max = 180.0, Units = deg )
        float32 _maxStepDegrees{ 180.0f };
    };

    /** @brief 관절 제한(`<limits><item …/>`) 하나의 칸입니다. 각은 도 단위다. */
    REFLECT()
    struct SW_API RigJointLimitParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "bone", Required )
        hashed_string _bone{};
        PROPERTY( Name = "type", Required )
        RigJointLimitType _type{ RigJointLimitType::None };
        PROPERTY( Name = "swing_degrees", Units = deg )
        float32 _swingDegrees{ 180.0f };
        PROPERTY( Name = "twist_degrees", Units = deg )
        float32 _twistDegrees{ 180.0f };
        PROPERTY( Name = "axis" )
        float3 _axis{ 1.0f, 0.0f, 0.0f };
        PROPERTY( Name = "min_degrees", Units = deg )
        float32 _minDegrees{ -180.0f };
        PROPERTY( Name = "max_degrees", Units = deg )
        float32 _maxDegrees{ 180.0f };
    };

    /**
     * @brief 조준 사슬의 본 하나(`<chain><item bone weight max_degrees/>`)입니다. 본은 남은 회전의 `weight` 몫을 받되, 애니메이션 방향에서 `max_degrees` 를 넘게 돌지 않습니다.
     * @details 머리 · 목 · 등뼈에 시선을 나눌 때 본마다 상한을 따로 둔다(언리얼 Control Rig 의 Aim 체인 · Look At 의 본별 제한과 같은 자리).
     */
    REFLECT()
    struct SW_API RigAimChainParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "bone", Required )
        hashed_string _bone{};
        PROPERTY( Name = "weight", Required, Min = 0.0, Max = 1.0 )
        float32 _weight{ 0.0f };
        PROPERTY( Name = "max_degrees", Min = 0.0, Max = 180.0, Units = deg )
        float32 _maxDegrees{ 180.0f };
    };

    /** @brief 조준(`Aim`)의 칸입니다. 조준 사슬(`chain`)은 `RigAimChainParams` 목록이다. */
    REFLECT()
    struct SW_API RigAimParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "bone", Required )
        hashed_string _bone{};
        PROPERTY( Name = "target", Required )
        hashed_string _target{};
        PROPERTY( Name = "aim_axis" )
        float3 _aimAxis{ 0.0f, 0.0f, 1.0f };
        PROPERTY( Name = "max_degrees", Min = 0.0, Max = 180.0, Units = deg )
        float32 _maxDegrees{ 180.0f };
        /** @brief 켜면 `aim_axis` 는 레퍼런스 포즈의 모델 공간 방향이다(캐릭터 앞 +Z) — 본 로컬 축을 모르는 임포트 리그(UBC · Mixamo)도 같은 데이터로 쓴다. 묶을 때 본 로컬로 바꾼다. */
        PROPERTY( Name = "aim_axis_in_model" )
        bool _bAimAxisInModel{ false };
    };

    /** @brief 다리 하나(`<feet><item root mid end/>`)의 칸입니다 — 발 디딤 · 보폭 워핑이 함께 쓴다. */
    REFLECT()
    struct SW_API RigLegParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "root", Required )
        hashed_string _root{};
        PROPERTY( Name = "mid", Required )
        hashed_string _mid{};
        PROPERTY( Name = "end", Required )
        hashed_string _end{};
    };

    /** @brief 발 디딤(`FootPlacement`)의 칸입니다. */
    REFLECT()
    struct SW_API RigFootPlacementParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "pelvis", Required )
        hashed_string _pelvis{};
        PROPERTY( Name = "trace_up", Min = 0.0, Units = m )
        float32 _traceUp{ 0.5f };
        PROPERTY( Name = "trace_down", Min = 0.0, Units = m )
        float32 _traceDown{ 0.75f };
        PROPERTY( Name = "max_pelvis_drop", Min = 0.0, Units = m )
        float32 _maxPelvisDrop{ 0.4f };
        PROPERTY( Name = "max_raise", Min = 0.0, Units = m )
        float32 _maxRaise{ 0.5f };
        PROPERTY( Name = "interp_speed", Min = 0.0 )
        float32 _interpSpeed{ 15.0f };
        PROPERTY( Name = "align_to_normal" )
        bool _bAlignToNormal{ true };
        PROPERTY( Name = "max_align_degrees", Min = 0.0, Max = 90.0, Units = deg )
        float32 _maxAlignDegrees{ 30.0f };
    };

    /** @brief 보폭 워핑(`StrideWarping`)의 칸입니다. 모두 필수다. `0 < min_scale <= max_scale` 은 노드가 본다. */
    REFLECT()
    struct SW_API RigStrideWarpingParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "pelvis", Required )
        hashed_string _pelvis{};
        PROPERTY( Name = "min_scale", Required )
        float32 _minScale{ 0.0f };
        PROPERTY( Name = "max_scale", Required )
        float32 _maxScale{ 0.0f };
        PROPERTY( Name = "max_pelvis_drop", Required, Min = 0.0, Units = m )
        float32 _maxPelvisDrop{ 0.0f };
    };

    /** @brief 전신 IK(`FullBodyIK`)의 칸입니다. 사슬 목록(`chains`)은 `RigFullBodyChainParams` 목록이다. */
    REFLECT()
    struct SW_API RigFullBodyIKParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "pelvis", Required )
        hashed_string _pelvis{};
        PROPERTY( Name = "max_pelvis_offset", Required, Min = 0.0, Units = m )
        float32 _maxPelvisOffset{ 0.0f };
        PROPERTY( Name = "pelvis_iterations", Min = 1 )
        uint32 _pelvisIterations{ 4 };
        PROPERTY( Name = "iterations", Min = 1 )
        uint32 _iterations{ RigChainSettings{}._iterationCount };
        PROPERTY( Name = "tolerance", Min = 0.0, Units = m )
        float32 _tolerance{ RigChainSettings{}._tolerance };
    };

    /** @brief 전신 IK 사슬(`<chains><item …>`) 하나의 칸입니다. 본 목록(`bones`)은 노드가 푼다. */
    REFLECT()
    struct SW_API RigFullBodyChainParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "target", Required )
        hashed_string _target{};
        PROPERTY( Name = "weight", Min = 0.0 )
        float32 _weight{ 1.0f };
        PROPERTY( Name = "match_rotation" )
        bool _bMatchRotation{ false };
    };
} // namespace sw

namespace sw
{
    // ---- 2 차 동작 노드(RigSecondaryNodes.cpp)

    /**
     * @brief 스프링 사슬(`SpringChain`)의 칸입니다. 본 목록(`bones`) · 충돌체(`colliders`) · 관절마다 물성(`joints`)은 노드가 푼다.
     * @details `gravity` 는 적었는지가 뜻(월드 중력 덮어쓰기)이라 노드가 손으로 읽는다.
     */
    REFLECT()
    struct SW_API RigSpringChainParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "stiffness", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _stiffness{ RigSpringSettings{}._stiffness };
        PROPERTY( Name = "damping", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _damping{ RigSpringSettings{}._damping };
        PROPERTY( Name = "axis_pull", Min = 0.0, Units = "m/s" )
        float32 _axisPull{ RigSpringSettings{}._axisPull };
        PROPERTY( Name = "gravity_scale" )
        float32 _gravityScale{ RigSpringSettings{}._gravityScale };
        PROPERTY( Name = "particle_radius", Min = 0.0, Units = m )
        float32 _particleRadius{ RigSpringSettings{}._particleRadius };
        PROPERTY( Name = "fixed_step", Min = 0.0001, Units = s )
        float32 _fixedStep{ RigSpringSettings{}._fixedStep };
        PROPERTY( Name = "max_substeps", Min = 1 )
        uint32 _maxSubsteps{ RigSpringSettings{}._maxSubStep };
        PROPERTY( Name = "teleport_distance", Min = 0.0, Units = m )
        float32 _teleportDistance{ RigSpringSettings{}._teleportDistance };
        PROPERTY( Name = "use_shared_colliders" )
        bool _bUseSharedColliders{ false };
    };

    /** @brief 스프링 충돌체(`<colliders><item …/>`) 하나의 칸입니다. `bone` · `target` 중 하나만 적는다(노드가 본다). */
    REFLECT()
    struct SW_API RigSpringColliderParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "bone" )
        hashed_string _bone{};
        PROPERTY( Name = "target" )
        hashed_string _target{};
        PROPERTY( Name = "shape", Required )
        RigSpringColliderShape _shape{ RigSpringColliderShape::Sphere };
        PROPERTY( Name = "a", Units = m )
        float3 _a{};
        PROPERTY( Name = "b", Units = m )
        float3 _b{};
        PROPERTY( Name = "radius", Required, Min = 0.0, Units = m )
        float32 _radius{ 0.0f };
    };

    /** @brief 입자 하나의 물성(`<joints><item …/>`)입니다. 적지 않은 칸은 사슬 값이다 — 노드가 사슬 값으로 채운 뒤 읽는다. */
    REFLECT()
    struct SW_API RigSpringJointParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "stiffness", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _stiffness{ 0.0f };
        PROPERTY( Name = "axis_pull", Min = 0.0, Units = "m/s" )
        float32 _axisPull{ 0.0f };
        PROPERTY( Name = "damping", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _damping{ 0.0f };
        PROPERTY( Name = "gravity" )
        float3 _gravity{};
        PROPERTY( Name = "radius", Min = 0.0, Units = m )
        float32 _radius{ 0.0f };
    };

    /** @brief 포즈 구동(`PoseDriver`)의 칸입니다. 포즈 목록(`poses`)은 노드가 푼다. */
    REFLECT()
    struct SW_API RigPoseDriverParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "driver", Required )
        hashed_string _driver{};
        PROPERTY( Name = "radius_degrees", Min = 1.0, Max = 180.0, Units = deg )
        float32 _radiusDegrees{ 45.0f };
    };

    /** @brief 구동 포즈(`<poses><item …>`) 하나의 칸입니다. 모프(`morphs`) · 보정 본(`bones`) 목록은 노드가 푼다. */
    REFLECT()
    struct SW_API RigDriverPoseParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "name", Required )
        hashed_string _name{};
        PROPERTY( Name = "rotation", Required, Units = deg )
        float3 _rotation{};
    };

    /** @brief 포즈의 모프 가중치(`<morphs><item morph weight/>`) 하나입니다. */
    REFLECT()
    struct SW_API RigDriverMorphParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "morph", Required )
        hashed_string _morph{};
        PROPERTY( Name = "weight", Required )
        float32 _weight{ 0.0f };
    };

    /** @brief 포즈의 보정 본(`<bones><item bone rotation translation/>`) 하나입니다. */
    REFLECT()
    struct SW_API RigCorrectiveBoneParams
    {
        REFLECT_BODY();

        PROPERTY( Name = "bone", Required )
        hashed_string _bone{};
        PROPERTY( Name = "rotation", Units = deg )
        float3 _rotation{};
        PROPERTY( Name = "translation", Units = m )
        float3 _translation{};
    };
} // namespace sw
