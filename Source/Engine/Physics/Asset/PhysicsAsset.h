/**
 * @file PhysicsAsset.h
 * @brief 물리 에셋(`*.physics.xml`)입니다. 뼈마다 바디(셰이프, 질량, 재질, 레이어), 부모 뼈와의 관절(스윙과 비틀림 한계), 충돌을 끈 쌍, 히트 존을 담습니다.
 * @details 래그돌, 히트박스, 천 충돌체가 함께 쓰는 하나의 에셋이다(언리얼의 Physics Asset). 스켈레톤 타입을 모르므로 뼈를 이름으로 적고,
 *          쓰는 쪽이 이름 배열로 검사한다(`validateAgainstSkeleton`, 모르는 뼈는 오류). 래그돌로 만드는 일은 `PhysicsRagdoll` 가 한다.
 *
 *          셰이프와 관절 축은 **뼈 로컬**이다(바디의 좌표계가 뼈의 좌표계다). 관절은 그 뼈의 바디와, 부모 체인에서 가장 가까운 바디가 있는 뼈를 연결하고,
 *          위치는 이 뼈의 원점이다. 루트 바디(부모 쪽 바디가 없는 것)의 관절은 쓰지 않는다.
 * @code
 *     <PhysicsAsset _defaultLayer="Ragdoll" _bCollideJointedBodies="false">
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
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"
#include "Core/Path/AbsolutePath.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/PhysicsTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct ResourceContext;

    /** @brief 뼈의 바디를 부모 쪽 바디에 연결하는 관절입니다. 축은 뼈 로컬이고, 각은 라디안입니다. */
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
        PROPERTY( Min = 0.0, Tooltip = "Cone swing half angle around the normal axis", Units = rad )
        float32 _swingLimitNormal{ 0.5f };
        PROPERTY( Min = 0.0, Tooltip = "Cone swing half angle around the third axis", Units = rad )
        float32 _swingLimitPlane{ 0.5f };
        PROPERTY( Tooltip = "Twist (Cone) / angle (Hinge) lower limit", Units = rad )
        float32 _twistMin{ -0.3f };
        PROPERTY( Tooltip = "Twist (Cone) / angle (Hinge) upper limit", Units = rad )
        float32 _twistMax{ 0.3f };
    };
} // namespace sw

namespace sw
{
    /** @brief 바디의 히트 존입니다. 맞은 바디로 부위와 피해 배율을 고릅니다(히트박스). 이름이 비어 있으면 히트 존이 아닙니다. */
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

        /** @brief 차이 파일(`_base`)의 열쇠입니다. 셰이프 · 충돌 쌍 목록은 이름이 없어 통째로 바뀝니다(쌍의 열쇠는 뼈 둘이라 칸 하나로 짚을 수 없다). */
        PROPERTY( InheritKey, Tooltip = "Bone name" )
        hashed_string _bone{};
        PROPERTY( Tooltip = "Collision shapes in bone space" )
        vector<PhysicsShapeDesc3D> _listShape;
        PROPERTY( Min = 0.0, Tooltip = "Mass in kg; 0 computes it from the shapes and material density", Units = kg )
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
    /** @brief 충돌을 끈 뼈 쌍입니다(관절로 연결하지 않은 이웃, 예를 들어 골반과 반대쪽 허벅지). */
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
        friend struct AssetTypeLoader;

        REFLECT_BODY();

        /** @brief 물리 에셋 파일의 확장자입니다. */
        static constexpr string_view kExtension = ".physics.xml";
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `CharacterDataCache.cpp` 의 공유 캐시 줄). 타입 있는 참조 `AssetRef<PhysicsAsset>` 가 접미사로 가른다. */
        static constexpr AssetTypeDef kAssetType{ "PhysicsAsset", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        PROPERTY( Tooltip = "Collision layer of bodies that do not name one" )
        hashed_string _defaultLayer{ "Ragdoll" };
        PROPERTY( Tooltip = "Bodies, one per bone" )
        vector<PhysicsAssetBodyDef> _listBody;
        PROPERTY( Tooltip = "Bone pairs that never collide" )
        vector<PhysicsAssetPairDef> _listDisabledPair;
        PROPERTY( Tooltip = "Bodies connected by a joint collide with each other" )
        bool   _bCollideJointedBodies{ false };
        string _baseID{}; ///< 읽은 글의 `_base`(PROPERTY 가 아니다 — 파일 칸이 아니라 읽기 앞 단계가 지우는 표시다). `saveToFile` 이 부모와의 차이로 쓴다

        /**
         * @brief 검사한 뒤 @p absPath 에 씁니다. 부모(`_base`)를 가리키던 글에서 읽었으면 부모와 다른 칸만 쓰고(`ReflectedXMLFile::saveDescAsChild`), 아니면 형식 기본값과 다른 칸만 씁니다.
         * @return 검사에 걸리거나 부모를 읽지 못하면 쓰지 않고 false 입니다 — 모든 칸을 적어 부모를 끊지 않습니다.
         */
        [[nodiscard]] bool saveToFile( const AbsolutePath& absPath ) const;
        /** @brief XML 문자열을 읽고 검사합니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        /** @brief 에셋 자체의 규칙을 검사합니다(뼈 이름이 겹치지 않고, 바디마다 셰이프가 있고, 쌍의 뼈가 에셋에 있다). */
        [[nodiscard]] bool validate( string_view sourceName ) const;
        /** @brief 모든 바디의 뼈가 스켈레톤의 이름에 있는지 검사합니다. 없는 뼈는 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validateAgainstSkeleton( span<const hashed_string> listBoneName, string_view sourceName ) const;
        /** @brief 뼈 이름의 바디 번호입니다. 없으면 없음입니다. */
        optional<uint32> findBodyIndex( const hashed_string& bone ) const;

    private:
        /** @brief 합성 문맥을 받는 읽기의 본문입니다. `loadFromAsset` 은 리소스 id 를 실은 문맥(`withAsset`)을, 글 입구는 id 없는 문맥을 넘깁니다(덮어쓰기 층 · 다시 읽기 기록 · 접속 대조는 id 로 읽을 때만 받는다). */
        [[nodiscard]] bool loadComposed( string_view xmlText, string_view sourceName, const ResourceContext& context );
        /** @brief 리소스 경로의 파일을 읽습니다. 종류의 로더(공유 캐시)만 부른다 — 쓰는 쪽은 `SharedAssetCache<PhysicsAsset>::acquire` 로 한 벌을 쥔다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID );
    };
} // namespace sw
