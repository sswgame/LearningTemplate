/**
 * @file FractureRenderUnits.h
 * @brief 쪼갠 오브젝트의 렌더 유닛입니다. 슬롯(겉면, 안쪽 면)마다 스킨드 메시 컴포넌트 하나와, 쉬는 자세를 베이크한 정적 메시 컴포넌트 하나를 오브젝트에 붙여 다룹니다.
 * @details `FractureBaseComponent` 의 값 멤버이고 컴포넌트가 직접(비가상으로) 부른다. 잎마다 본 로컬 변환(`listPiecePose`)은 컴포넌트가 그룹 자세로 구해 넘긴다.
 *          붙인 컴포넌트는 핸들로 들고 쓸 때마다 오브젝트의 매니저로 찾는다. 사라졌으면 그 슬롯을 건너뛴다.
 *
 *          **조각 그리기.** 조각이 수백 개여도 한 번의 그리기로 처리한다 — 조각마다 본 하나(강체 스킨, 가중치 1)를 둔 스킨드 메시와 스켈레톤, 쉬는 자세를 베이크한 정적 메시(static 함수들).
 *          Chaos 의 Geometry Collection 이 조각마다 본 변환으로 그리는 것에 해당한다. 슬롯(겉면, 안쪽 면)마다 그리기 하나이고,
 *          GPU 스키닝(모프 풀의 스킨 구간)이 정점을 옮긴다. 본 i 의 레퍼런스 포즈는 조각 무게 중심으로의 이동이고 역 바인드는 그 반대다.
 *          그래서 단위 포즈가 곧 쪼개기 전 모습이다. 런타임은 본 로컬 = (이동, 회전, 축소 배율)을 쓴다(`makeBoneTransform`).
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Destruction/Fracture/FractureAsset.h"
#include "Engine/Foundation/ComponentHandle.h"

namespace sw
{

    class GameObject;
    class Mesh;
    class MeshComponent;
    class SkeletalMeshComponent;
    class Skeleton;

    /**
     * @class FractureRenderUnits
     * @brief 스킨드 메시 둘을 만들고, 프레임마다 본 포즈를 쓰며, 멈추면 정적 메시로 바꿉니다.
     */
    class SW_API FractureRenderUnits
    {
    public:
        FractureRenderUnits();

        /**
         * @brief 조각마다 본 하나를 둔 스킨드 메시 컴포넌트를 슬롯마다 @p owner 에 붙입니다. 재질은 안쪽 면이면 @p interiorMaterialPath, 아니면 온전한 메시(@p pIntact)의 것을 씁니다.
         *        슬롯의 삼각형이 없으면 그 슬롯은 만들지 않습니다.
         */
        void create( GameObject& owner, const FractureAsset& asset, const MeshComponent* pIntact, const hashed_string& interiorMaterialPath );
        /** @brief 잎마다 본 로컬 변환을 스킨드 메시에 쓰고 본 팔레트를 바로 다시 계산합니다. 경계 반경은 에셋 반경에 @p maxOffset 을 더한 값입니다. */
        void writePoses( const GameObject& owner, const FractureAsset& asset, const vector<BoneTransform>& listPiecePose, float32 maxOffset );
        /** @brief 지금 자세를 정적 메시에 베이크해 스킨드 메시를 숨기거나(@p bBaked), 정적 메시를 숨기고 스킨드 메시로 돌아갑니다. */
        void setBaked( GameObject& owner, const FractureAsset& asset, const vector<BoneTransform>& listPiecePose, bool bBaked );
        bool isBaked() const { return _bBaked == SW_TRUE; }

        /** @brief 조각마다 루트 본 하나(이름 `piece<번호>`)를 둔 스켈레톤을 만듭니다. */
        static shared_ptr<Skeleton> createPieceSkeleton( const FractureAsset& asset );
        /** @brief 슬롯 하나의 삼각형을 조각 본에 가중치 1 로 묶은 스킨드 메시를 만듭니다. 그 슬롯의 삼각형이 없으면 nullptr 를 반환합니다. */
        static shared_ptr<Mesh> createSkinnedMesh( const FractureAsset& asset, FractureSurfaceSlot slot );
        /**
         * @brief 조각마다 본 로컬 변환(@p listPose, 잎 순)을 정점에 베이크한 정적 메시를 만듭니다. 쉬는 파편을 정적 메시로 합칠 때 씁니다. 배율이 0 인 조각은 뺍니다.
         *        남는 삼각형이 없으면 nullptr 를 반환합니다.
         */
        static shared_ptr<Mesh> createBakedMesh( const FractureAsset& asset, FractureSurfaceSlot slot, vector_reference<const BoneTransform> listPose );
        /**
         * @brief 조각 하나의 본 로컬 변환을 반환합니다. 메시 공간 점 p 를 `회전( p - 무게 중심 ) × 배율 + 회전( 무게 중심 ) + 이동` 으로 보냅니다.
         * @param rotation 오브젝트 기준 회전입니다(쪼개기 전에는 단위 회전입니다).
         * @param offset 오브젝트(메시) 공간의 이동입니다.
         */
        static BoneTransform makeBoneTransform( const float3& centroid, const quaternion& rotation, const float3& offset, float32 scale );

    private:
        /** @brief @p owner 의 매니저로 핸들의 컴포넌트를 찾습니다. 사라졌으면 nullptr 입니다(이 .cpp 안에서만 부르므로 정의도 .cpp 에 둔다). */
        template <typename T>
        static T* findOwnedComponent( const GameObject& owner, TComponentHandle<T> handle );

        shared_ptr<Mesh>                        _arrSkinnedMesh[2]; ///< 슬롯(겉면, 안쪽 면)마다 스킨드 메시 원본이다.
        TComponentHandle<SkeletalMeshComponent> _arrSkinnedUnit[2];
        TComponentHandle<MeshComponent>         _arrBakedUnit[2];
        uint8                                   _bBaked;
    };
} // namespace sw
