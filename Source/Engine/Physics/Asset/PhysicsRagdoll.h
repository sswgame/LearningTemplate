/**
 * @file PhysicsRagdoll.h
 * @brief 물리 에셋과 일반 뼈 배열(이름, 부모 번호, 모델 공간 행렬)로 래그돌과 히트박스 바디를 만들고, 바디 자세를 뼈 행렬로 다시 읽어 오는 빌더입니다.
 * @details 애니메이션 타입(스켈레톤, 포즈)을 모르고 배열만 받는다(`PhysicsSkeletonView`). 쓰는 쪽(애니메이션, 히트 반응, 절단)이 자기 포즈를
 *          그 형태로 넘긴다. 백엔드도 모른다(`IPhysicsScene3D` 만 호출한다).
 *
 *          사용법. 히트박스는 Kinematic 으로 만들고 프레임마다 `driveToPose` 로 애니메이션 포즈를 따르게 한다. 레이나 셰이프 질의가 맞힌 바디는
 *          `findBodyIndex` 와 `PhysicsAssetBodyDef::_hitZone` 으로 부위와 배율을 고른다. 래그돌은 `setBodyType( Dynamic )` 으로 넘기고(속도는 마지막
 *          `driveToPose` 가 준 값을 이어 간다) 프레임마다 `readBoneTransforms` 로 포즈를 받는다.
 *
 *          행렬은 엔진 관례(행 벡터, `로컬 × 부모`)를 따릅니다. 뼈 배열은 부모가 자식보다 앞이어야 합니다(부모 번호 < 자기 번호, 루트는 -1).
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

    /** @brief 뼈 배열 셋을 묶은 뷰입니다. 셋의 길이가 같아야 합니다. */
    struct PhysicsSkeletonView
    {
        span<const hashed_string> _listBoneName;
        span<const int32>         _listParentIndex;    ///< 부모 뼈 번호(루트는 -1). 부모가 자식보다 앞이다
        span<const float4x4>      _listModelSpaceBone; ///< 뼈의 모델 공간 행렬(행 벡터)
    };
} // namespace sw

namespace sw
{
    /** @brief 래그돌을 만들 때의 옵션입니다. */
    struct PhysicsRagdollOptions
    {
        const PhysicsSettings* _pSettings{ nullptr }; ///< 레이어 이름을 물리 설정 테이블에서 찾는다(없으면 레이어 0).
        uint64                 _userData{ 0 };        ///< 바디마다 실을 사용자 값이다(엔진은 소유 오브젝트 id 를 쓰며, 이벤트가 그 오브젝트로 전달된다).
        PhysicsBodyType        _bodyType{ PhysicsBodyType::Dynamic };
    };
} // namespace sw

namespace sw
{
    /** @brief 만든 래그돌입니다. 에셋 바디 순서의 핸들과 뼈 대응을 담습니다. 값이라 복사해서 보관할 수 있고, 핸들은 씬이 지울 때까지 유효합니다. */
    struct SW_API PhysicsRagdoll
    {
        vector<PhysicsBodyHandle>  _listBody;       ///< 에셋 바디 순서다.
        vector<PhysicsJointHandle> _listJoint;      ///< 에셋 바디 순서(루트 바디는 무효)
        vector<int32>              _listBoneIndex;  ///< 에셋 바디에서 스켈레톤 뼈 번호로 가는 매핑이다.
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
         * @brief 에셋의 바디와 관절을 스켈레톤 포즈 위치에 만듭니다(바디는 `createBodies` 로 한 번에 넣습니다).
         * @details 관절로 연결된 바디끼리(`_bCollideJointedBodies` 가 false 일 때)와 에셋이 지정한 쌍은 서로 부딪히지 않는다. 에셋의 뼈가 스켈레톤에 없거나 배열이
         *          맞지 않으면 오류를 남기고 아무것도 만들지 않으며 false 를 반환한다.
         * @param worldFromModel 모델 공간에서 월드로 가는 행렬입니다(캐릭터의 월드 행렬).
         */
        [[nodiscard]] static bool create( IPhysicsScene3D& scene, const PhysicsAsset& asset, const PhysicsSkeletonView& skeleton, const float4x4& worldFromModel,
                                          const PhysicsRagdollOptions& options, PhysicsRagdoll& outRagdoll );
        /** @brief 바디와 관절을 지우고 래그돌을 비웁니다. */
        static void destroy( IPhysicsScene3D& scene, PhysicsRagdoll& inoutRagdoll );
        /** @brief 모든 바디의 종류를 바꿉니다(히트박스 Kinematic ↔ 래그돌 Dynamic). */
        static void setBodyType( IPhysicsScene3D& scene, const PhysicsRagdoll& ragdoll, PhysicsBodyType type );
        /**
         * @brief 바디를 포즈로 보냅니다. Kinematic 바디는 @p deltaTime 뒤에 닿도록 움직이고(밀린 것이 속도를 받습니다), 나머지는 순간이동합니다.
         * @param deltaTime 0 이하면 모두 순간이동합니다(처음 배치할 때, 리스폰).
         */
        static void driveToPose( IPhysicsScene3D& scene, const PhysicsRagdoll& ragdoll, const PhysicsSkeletonView& skeleton, const float4x4& worldFromModel,
                                 float32 deltaTime );
        /**
         * @brief 바디 자세를 모델 공간 뼈 행렬로 다시 읽습니다. 바디가 있는 뼈는 바디 자세를 쓰고(뼈의 배율은 입력 포즈의 것), 바디가 없는 뼈는 입력 포즈의 부모 상대
         *        변환을 새 부모에 붙입니다(손가락이 손을 따라갑니다).
         * @param skeleton 입력 포즈입니다(바디 없는 뼈의 로컬 변환을 여기서 가져옵니다).
         * @param outListModelSpaceBone 스켈레톤 뼈 수만큼의 버퍼입니다.
         */
        static void readBoneTransforms( const IPhysicsScene3D& scene, const PhysicsRagdoll& ragdoll, const PhysicsSkeletonView& skeleton, const float4x4& worldFromModel,
                                        span<float4x4> outListModelSpaceBone );
    };
} // namespace sw
