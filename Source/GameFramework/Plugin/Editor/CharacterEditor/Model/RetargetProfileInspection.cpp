#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/RetargetProfileInspection.h"

#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"

namespace sw::editor
{
    namespace
    {
        struct RetargetProfileInspectionInternal
        {
            /** @brief 레퍼런스 포즈에서 이름 사슬의 마디 길이 합(m)입니다. 모르는 본은 건너뜁니다(묶기가 이미 오류로 거른다). */
            static float32 computeChainLength( const Skeleton& skeleton, const vector<hashed_string>& listBone )
            {
                Pose             reference;
                vector<float4x4> listModel;
                reference.setToReference( skeleton );
                reference.computeModelSpace( skeleton.getParentIndices(), listModel );
                float32 length   = 0.0f;
                int32   previous = -1;
                for ( const hashed_string& name : listBone )
                {
                    const int32 bone = skeleton.findBoneIndex( name );
                    if ( bone < 0 )
                        continue;
                    if ( previous >= 0 )
                        length += ( listModel[static_cast<size_t>( bone )].getTranslation() - listModel[static_cast<size_t>( previous )].getTranslation() ).getLength();
                    previous = bone;
                }
                return length;
            }
        };
    } // namespace

    bool RetargetProfileInspector::inspect( const RetargetProfile& profile, RetargetProfileInspection& outInspection, string& outError )
    {
        outInspection = RetargetProfileInspection{};
        outError.clear();
        Skeleton sourceSkeleton;
        Skeleton targetSkeleton;
        if ( profile.getSourceSkeletonPath().empty() || sourceSkeleton.loadFromAsset( AssetID{ profile.getSourceSkeletonPath() } ) == false )
        {
            outError = "The source skeleton could not be read";
            return false;
        }
        if ( profile.getTargetSkeletonPath().empty() || targetSkeleton.loadFromAsset( AssetID{ profile.getTargetSkeletonPath() } ) == false )
        {
            outError = "The target skeleton could not be read";
            return false;
        }
        // 런타임과 같은 검사 하나 — 묶이지 않으면(모르는 본) 저장해도 쓸 수 없다. 까닭은 리타기터가 로그로 남긴다.
        PoseRetargeter retargeter;
        if ( retargeter.initialize( profile, sourceSkeleton, targetSkeleton, nullptr ) == false )
        {
            outError = "The profile does not bind to the two skeletons - see the log";
            return false;
        }
        outInspection._heightRatio           = retargeter.getHeightRatio();
        outInspection._mappedTargetBoneCount = retargeter.getMappedBoneCount();
        outInspection._targetBoneCount       = targetSkeleton.getBoneCount();
        for ( const RetargetChain& chain : profile.getChains() )
        {
            RetargetChainInspection inspection{};
            inspection._name         = chain._name;
            inspection._sourceLength = RetargetProfileInspectionInternal::computeChainLength( sourceSkeleton, chain._listSourceBone );
            inspection._targetLength = RetargetProfileInspectionInternal::computeChainLength( targetSkeleton, chain._listTargetBone );
            outInspection._listChain.push_back( inspection );
        }
        return true;
    }
} // namespace sw::editor
