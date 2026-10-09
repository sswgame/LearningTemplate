#include "pch.h"

#include "Engine/Character/PoseModifier/CharacterPoseUtil.h"

#include "Engine/Animation/Skeleton.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"

namespace sw
{
    void CharacterPoseUtil::makeBindBones( const Skeleton& skeleton, CharacterBoneArray& outBones )
    {
        outBones = CharacterBoneArray{};
        for ( uint32 boneIndex = 0; boneIndex < skeleton.getBoneCount(); ++boneIndex )
        {
            const SkeletonBone& bone = skeleton.getBone( boneIndex );
            // 부모가 앞에 있으므로 붙이면서 모델 칸도 쌓인다.
            (void)outBones.addBone( bone._name, bone._parentIndex, bone._referencePose.toMatrix() );
        }
    }

    bool CharacterPoseUtil::copyUnitPose( const SkeletalMeshComponent& unit, CharacterBoneArray& inoutBones )
    {
        const Skeleton& skeleton  = unit.getSkeleton();
        const uint32    boneCount = skeleton.getBoneCount();
        // 이름 하나만 대조해도 다른 스켈레톤을 가려낸다 — 본 수가 같은 다른 리그를 받으면 다시 짓는다.
        const bool bSameShape = inoutBones.getBoneCount() == boneCount && ( boneCount == 0 || inoutBones._listName[0] == skeleton.getBone( 0 )._name );
        if ( bSameShape == false )
            makeBindBones( skeleton, inoutBones );
        const vector<float4x4>& listModel = unit.getModelSpaceTransforms();
        if ( listModel.size() != static_cast<size_t>( boneCount ) )
            return false;
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            inoutBones._listModel[boneIndex] = listModel[boneIndex];
        }
        return true;
    }
} // namespace sw
