/**
 * @file PhysicsAsset.h
 * @brief 물리 에셋(`*.physics.xml`) — 뼈마다 바디(셰이프 · 질량 · 재질 · 레이어) · 부모 뼈와의 관절(스윙 · 비틀림 한계) · 충돌을 끈 쌍 · 히트 존입니다.
 * @details 래그돌 · 히트박스 · 천 충돌체가 함께 쓰는 하나의 에셋입니다(언리얼 Physics Asset). 스켈레톤 타입을 모릅니다 — 뼈를 이름으로 적고,
 *          쓰는 쪽이 이름 배열로 검사합니다(`validateAgainstSkeleton` — 모르는 뼈는 오류). 래그돌로 세우는 것은 `PhysicsRagdollBuilder` 입니다.
 *
 *          셰이프 · 관절 축은 **뼈 로컬**입니다(바디의 틀 = 뼈의 틀). 관절은 그 뼈의 바디와, 부모 사슬에서 가장 가까운 바디가 있는 뼈를 잇고,
 *          자리는 이 뼈의 원점입니다. 루트 바디(부모 쪽 바디가 없는 것)의 관절은 쓰지 않습니다.
 * @code
 *     <PhysicsAsset _defaultLayer="Ragdoll" _bDisableJointedCollision="true">
 *         <_listBody>
 *             <PhysicsAssetBodyDef _bone="spine" _mass="20" _material="Flesh">
 *                 <_listShape><PhysicsShapeDesc3D _type="Capsule" _radius="0.15" _halfHeight="0.1" _localPosition="0,0.2,0" /></_listShape>
 *                 <_joint _type="Cone" _twistAxis="0,1,0" _normalAxis="1,0,0" _swingLimitNormal="0.5" _swingLimitPlane="0.5" _twistMin="-0.3" _twistMax="0.3" />
 *                 <_hitZone _name="Torso" _damageMultiplier="1" />
 *             </PhysicsAssetBodyDef>
 *         </_listBody>
 *         <_listDisabledPair><PhysicsAssetPairDef _boneA="spine" _boneB="head" /></_listDisabledPair>
 *     </PhysicsAsset>
 * @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Physics/PhysicsShape.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 뼈의 바디를 부모 쪽 바디에 잇는 관절입니다. 축은 뼈 로컬, 각은 라디안입니다. */
    REFLECT()
    struct SW_API PhysicsAssetJointDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Cone (swing-twist) for limbs, Hinge for knees and elbows, Fixed to weld" )
        PhysicsJointType _type{ PhysicsJointType::Cone };
        PROPERTY( Tooltip = "Twist / hinge axis in bone space" )
        float3 _twistAxis{ 0.0f, 1.0f, 0.0f };
        PROPERTY( Tooltip = "Reference axis perpendicular to the twist axis, in bone space" )
        float3 _normalAxis{ 1.0f, 0.0f, 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Cone swing half angle around the normal axis", Meta = "Units=rad" )
        float32 _swingLimitNormal{ 0.5f };
        PROPERTY( Min = 0.0, Tooltip = "Cone swing half angle around the third axis", Meta = "Units=rad" )
        float32 _swingLimitPlane{ 0.5f };
        PROPERTY( Tooltip = "Twist (Cone) / angle (Hinge) lower limit", Meta = "Units=rad" )
        float32 _twistMin{ -0.3f };
        PROPERTY( Tooltip = "Twist (Cone) / angle (Hinge) upper limit", Meta = "Units=rad" )
        float32 _twistMax{ 0.3f };
    };
} // namespace sw

namespace sw
{
    /** @brief 바디의 히트 존 — 맞은 바디로 부위와 피해 배율을 고릅니다(히트박스). 이름이 비면 히트 존이 아닙니다. */
    REFLECT()
    struct SW_API PhysicsHitZoneDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Hit zone name (Head, Torso, LeftArm ...)" )
        hashed_string _name{};
        PROPERTY( Min = 0.0, Tooltip = "Damage multiplier for hits on this body" )
        float32 _damageMultiplier{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 뼈 하나의 바디입니다. */
    REFLECT()
    struct SW_API PhysicsAssetBodyDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Bone name" )
        hashed_string _bone{};
        PROPERTY( Tooltip = "Collision shapes in bone space" )
        vector<PhysicsShapeDesc3D> _listShape;
        PROPERTY( Min = 0.0, Tooltip = "Mass in kg; 0 computes it from the shapes and material density", Meta = "Units=kg" )
        float32 _mass{ 0.0f };
        PROPERTY( Tooltip = "Physics material name (empty: the first material)" )
        hashed_string _material{};
        PROPERTY( Tooltip = "Collision layer name (empty: the asset's default layer)" )
        hashed_string _layer{};
        PROPERTY( Tooltip = "Joint to the nearest ancestor bone that has a body" )
        PhysicsAssetJointDef _joint{};
        PROPERTY( Tooltip = "Hit zone of this body" )
        PhysicsHitZoneDef _hitZone{};
    };
} // namespace sw

namespace sw
{
    /** @brief 충돌을 끈 뼈 쌍입니다(관절로 잇지 않은 이웃 — 골반과 허벅지 반대쪽 등). */
    REFLECT()
    struct SW_API PhysicsAssetPairDef
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _boneA{};
        PROPERTY()
        hashed_string _boneB{};
    };
} // namespace sw

namespace sw
{
    /** @brief 물리 에셋 하나입니다. 파일 머리말 참고. */
    REFLECT()
    struct SW_API PhysicsAsset
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Collision layer of bodies that do not name one" )
        hashed_string _defaultLayer{ "Ragdoll" };
        PROPERTY( Tooltip = "Bodies, one per bone" )
        vector<PhysicsAssetBodyDef> _listBody;
        PROPERTY( Tooltip = "Bone pairs that never collide" )
        vector<PhysicsAssetPairDef> _listDisabledPair;
        PROPERTY( Tooltip = "Bodies connected by a joint never collide" )
        bool _bDisableJointedCollision{ true };

        /** @brief 리소스 경로의 XML 을 읽고 검사합니다. 모르는 키 · 열거자 · 겹친 뼈 · 셰이프 없는 바디 · 모르는 쌍 뼈는 오류이고 false 입니다. */
        [[nodiscard]] bool loadFromResource( string_view resourcePath );
        /** @brief XML 문자열을 읽고 검사합니다. */
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName );
        /** @brief 에셋 자체의 규칙을 검사합니다(뼈 이름이 겹치지 않고, 바디마다 셰이프가 있고, 쌍의 뼈가 에셋에 있다). */
        [[nodiscard]] bool validate( string_view sourceName ) const;
        /** @brief 모든 바디의 뼈가 스켈레톤의 이름에 있는지 검사합니다. 없는 뼈는 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validateAgainstSkeleton( span<const hashed_string> listBoneName, string_view sourceName ) const;
        /** @brief 뼈 이름의 바디 번호입니다. 없으면 -1 입니다. */
        int32 findBodyIndex( const hashed_string& bone ) const;
    };
} // namespace sw
