/**
 * @file FractureRenderUnits.h
 * @brief 쪼갠 오브젝트의 렌더 유닛입니다. 슬롯(겉면, 안쪽 면)마다 스킨드 메시 컴포넌트 하나와, 쉬는 자세를 베이크한 정적 메시 컴포넌트 하나를 오브젝트에 붙여 다룹니다.
 * @details `FractureComponentBase` 의 값 멤버이고 컴포넌트가 직접(비가상으로) 부른다. 잎마다 본 로컬 변환(`listPiecePose`)은 컴포넌트가 그룹 자세로 구해 넘긴다.
 *          붙인 컴포넌트는 핸들로 들고 쓸 때마다 오브젝트의 매니저로 찾는다. 사라졌으면 그 슬롯을 건너뛴다.
 */
#pragma once
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct BoneTransform;
    struct FractureAsset;

    class GameObject;
    class Mesh;
    class MeshComponent;
    class SkeletalMeshComponent;

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
