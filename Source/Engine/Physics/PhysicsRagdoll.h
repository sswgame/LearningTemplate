/**
 * @file PhysicsRagdoll.h
 * @brief 물리 에셋 + 평범한 뼈 배열(이름 · 부모 번호 · 모델 공간 행렬)로 래그돌 · 히트박스 바디를 세우고, 바디 자세를 뼈 행렬로 되읽는 빌더입니다.
 * @details 애니메이션 타입(스켈레톤 · 포즈)을 모릅니다 — 배열만 받습니다(`PhysicsSkeletonView`). 쓰는 쪽(애니메이션 · 히트 반응 · 절단)이 자기 포즈를
 *          그 모양으로 넘깁니다. 백엔드도 모릅니다(`IPhysicsScene3D` 만 부른다).
 *
 *          쓰는 법 — 히트박스: Kinematic 으로 세우고 프레임마다 `driveToPose` 로 애니메이션 포즈를 따르게 한 뒤, 레이 · 셰이프 질의가 맞힌 바디를
 *          `findBodyIndex` → `PhysicsAssetBodyDef::_hitZone` 으로 부위 · 배율을 고릅니다. 래그돌: `setBodyType( Dynamic )` 으로 넘기고(속도는 마지막
 *          `driveToPose` 가 준 것을 잇는다) 프레임마다 `readBoneTransforms` 로 포즈를 받습니다.
 *
 *          행렬은 엔진 관례(행 벡터, `로컬 × 부모`)입니다. 뼈 배열은 부모가 자식보다 앞이어야 합니다(부모 번호 < 자기 번호, 루트는 -1).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    struct PhysicsAsset;
    struct PhysicsSettings;

    /** @brief 뼈 배열 셋을 묶은 보기입니다. 셋의 길이가 같아야 합니다. */
    struct PhysicsSkeletonView
    {
        span<const hashed_string> _listBoneName;
        span<const int32>         _listParentIndex;    ///< 부모 뼈 번호(루트는 -1). 부모가 자식보다 앞이다
        span<const float4x4>      _listModelSpaceBone; ///< 뼈의 모델 공간 행렬(행 벡터)
    };
} // namespace sw

namespace sw
{
    /** @brief 래그돌을 세우는 선택입니다. */
    struct PhysicsRagdollOptions
    {
        const PhysicsSettings* _pSettings{ nullptr }; ///< 레이어 이름을 풀 설정 표(없으면 레이어 0)
        uint64                 _userData{ 0 };        ///< 바디마다 실을 사용자 값(엔진은 소유 오브젝트 id — 이벤트가 그 오브젝트로 간다)
        PhysicsBodyType        _bodyType{ PhysicsBodyType::Dynamic };
    };
} // namespace sw

namespace sw
{
    /** @brief 세운 래그돌 — 에셋 바디 순서의 핸들과 뼈 대응입니다. 값이라 복사해 들 수 있고, 핸들은 씬이 지울 때까지 유효합니다. */
    struct SW_API PhysicsRagdoll
    {
        vector<PhysicsBodyHandle>  _listBody;       ///< 에셋 바디 순서
        vector<PhysicsJointHandle> _listJoint;      ///< 에셋 바디 순서(루트 바디는 무효)
        vector<int32>              _listBoneIndex;  ///< 에셋 바디 → 스켈레톤 뼈 번호
        vector<int32>              _listParentBody; ///< 에셋 바디 → 부모 쪽 에셋 바디(루트는 -1)
        vector<int32>              _listBodyOfBone; ///< 스켈레톤 뼈 → 에셋 바디(없으면 -1)

        /** @brief 바디 핸들의 에셋 바디 번호입니다(히트 존을 고를 때). 이 래그돌의 바디가 아니면 -1 입니다. */
        int32 findBodyIndex( PhysicsBodyHandle body ) const;
        /** @brief 바디가 하나도 없으면 true 입니다. */
        bool isEmpty() const { return _listBody.empty(); }
    };
} // namespace sw

namespace sw
{
    /** @brief 래그돌 빌더입니다. 파일 머리말 참고. */
    struct SW_API PhysicsRagdollBuilder
    {
        /**
         * @brief 에셋의 바디 · 관절을 스켈레톤 포즈 자리에 세웁니다(바디는 한 번에 넣는다 — `createBodies`).
         * @details 관절로 이은 바디끼리(`_bDisableJointedCollision`)와 에셋이 적은 쌍은 서로 부딪히지 않습니다. 에셋의 뼈가 스켈레톤에 없거나 배열이
         *          어긋나면 오류를 남기고 아무것도 만들지 않고 false 입니다.
         * @param worldFromModel 모델 공간 → 월드(캐릭터의 월드 행렬)
         */
        [[nodiscard]] static bool create( IPhysicsScene3D& scene, const PhysicsAsset& asset, const PhysicsSkeletonView& skeleton, const float4x4& worldFromModel,
                                          const PhysicsRagdollOptions& options, PhysicsRagdoll& outRagdoll );
        /** @brief 바디 · 관절을 지우고 래그돌을 비웁니다. */
        static void destroy( IPhysicsScene3D& scene, PhysicsRagdoll& inoutRagdoll );
        /** @brief 모든 바디의 종류를 바꿉니다(히트박스 Kinematic ↔ 래그돌 Dynamic). */
        static void setBodyType( IPhysicsScene3D& scene, const PhysicsRagdoll& ragdoll, PhysicsBodyType type );
        /**
         * @brief 바디를 포즈로 보냅니다. Kinematic 바디는 @p deltaTime 뒤에 닿게 움직이고(밀린 것이 속도를 받는다), 나머지는 순간이동합니다.
         * @param deltaTime 0 이하면 모두 순간이동합니다(처음 자리 잡기 · 리스폰).
         */
        static void driveToPose( IPhysicsScene3D& scene, const PhysicsRagdoll& ragdoll, const PhysicsSkeletonView& skeleton, const float4x4& worldFromModel,
                                 float32 deltaTime );
        /**
         * @brief 바디 자세를 모델 공간 뼈 행렬로 되읽습니다. 바디가 있는 뼈는 바디 자세(뼈의 배율은 입력 포즈의 것), 없는 뼈는 입력 포즈의 부모 상대
         *        변환을 새 부모에 붙인 것입니다(손가락이 손을 따라간다).
         * @param skeleton 입력 포즈(바디 없는 뼈의 로컬 변환을 여기서 뽑는다)
         * @param outListModelSpaceBone 스켈레톤 뼈 수만큼의 자리
         */
        static void readBoneTransforms( const IPhysicsScene3D& scene, const PhysicsRagdoll& ragdoll, const PhysicsSkeletonView& skeleton, const float4x4& worldFromModel,
                                        span<float4x4> outListModelSpaceBone );
    };
} // namespace sw
