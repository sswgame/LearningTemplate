#include "pch.h"

#include "Engine/Animation/Skeletal/LocomotionSpeedUtil.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"

namespace sw
{
    float32 LocomotionSpeedUtil::computeFootSpeed( const AnimationClip& clip, const Skeleton& skeleton, const hashed_string& leftFoot, const hashed_string& rightFoot,
                                                   uint32 sampleCount )
    {
        const optional<uint32> leftBone             = skeleton.findBoneIndex( leftFoot );
        const optional<uint32> rightBone            = skeleton.findBoneIndex( rightFoot );
        const float32          duration             = clip.getDuration();
        const bool             bInvalidFootSampling = leftBone.has_value() == false || rightBone.has_value() == false || sampleCount < 2 || duration <= 0.0f;
        if ( bInvalidFootSampling )
            return 0.0f;

        vector<int32> listTrackToBone;
        (void)clip.makeTrackToBoneMap( skeleton, listTrackToBone ); // 오류는 함수가 남겼고 있는 본은 쓴다
        Pose             pose;
        Pose             scratchTrackPose;
        vector<float4x4> listModel;
        const float32    step              = duration / static_cast<float32>( sampleCount - 1 );
        float32          plantedDistance   = 0.0f;
        float32          plantedSeconds    = 0.0f;
        float3           previousPosition  = float3{};
        int32            previousPlantBone = constant::invalid_index::kInt32;
        for ( uint32 sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
        {
            pose.setToReference( skeleton );
            if ( clip.samplePose( skeleton, step * static_cast<float32>( sampleIndex ), listTrackToBone, pose, scratchTrackPose, true ) == false )
                return 0.0f;
            pose.computeModelSpace( skeleton.getParentIndices(), listModel );
            const float3 leftPosition  = listModel[*leftBone].getTranslation();
            const float3 rightPosition = listModel[*rightBone].getTranslation();
            const bool   bLeftPlanted  = leftPosition._y <= rightPosition._y;
            const int32  plantBone     = static_cast<int32>( bLeftPlanted ? *leftBone : *rightBone );
            const float3 plantPosition = bLeftPlanted ? leftPosition : rightPosition;
            if ( plantBone == previousPlantBone )
            {
                const float32 deltaX = plantPosition._x - previousPosition._x;
                const float32 deltaZ = plantPosition._z - previousPosition._z;
                plantedDistance += MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
                plantedSeconds += step;
            }
            previousPlantBone = plantBone;
            previousPosition  = plantPosition;
        }
        return ( plantedSeconds > 0.0f ) ? plantedDistance / plantedSeconds : 0.0f;
    }

    float32 LocomotionSpeedUtil::computePlayRate( float32 moveSpeed, float32 clipFootSpeed, float32 minRate, float32 maxRate )
    {
        if ( clipFootSpeed <= MathUtil::kEpsilon )
            return 1.0f;
        return MathUtil::clamp( moveSpeed / clipFootSpeed, minRate, maxRate );
    }

    float32 LocomotionSpeedUtil::computeStrideScale( float32 moveSpeed, float32 clipFootSpeed, float32 playRate )
    {
        const float32 playedFootSpeed = clipFootSpeed * playRate;
        if ( playedFootSpeed <= MathUtil::kEpsilon )
            return 1.0f;
        return moveSpeed / playedFootSpeed;
    }
} // namespace sw
