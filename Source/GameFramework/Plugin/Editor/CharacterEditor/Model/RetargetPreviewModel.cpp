#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/RetargetPreviewModel.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Animation/Retarget/PoseRetargeter.h"
#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Skeletal/Pose.h"

namespace sw::editor
{
    namespace
    {
        struct RetargetPreviewModelInternal
        {
            /** @brief 자세의 모델 공간 본 위치를 스켈레톤 순서로 모읍니다. */
            static void computeBonePositions( const Skeleton& skeleton, const Pose& pose, vector<float3>& outListPosition )
            {
                vector<float4x4> listModel;
                pose.computeModelSpace( skeleton.getParentIndices(), listModel );
                outListPosition.clear();
                for ( const float4x4& model : listModel )
                {
                    outListPosition.push_back( model.getTranslation() );
                }
            }

            /** @brief 프로필의 리타깃 포즈 줄이 모두 대상 스켈레톤에 있으면 true 입니다. */
            static bool canApplyTargetPose( const RetargetProfile& profile, const Skeleton& target )
            {
                for ( const RetargetPoseBone& poseBone : profile.getTargetPose() )
                {
                    if ( target.findBoneIndex( poseBone._bone ).has_value() == false )
                        return false;
                }
                return true;
            }
        };
    } // namespace

    RetargetPreviewModel::RetargetPreviewModel()
        : _source{}
        , _target{}
        , _sourcePath{}
        , _targetPath{}
        , _listSourcePosition{}
        , _listTargetReferencePosition{}
        , _listTargetPosePosition{}
        , _bSourceReady{ SW_FALSE }
        , _bTargetReady{ SW_FALSE }
        , _bFramePending{ SW_FALSE }
    {
    }

    bool RetargetPreviewModel::isStale( const RetargetProfile& profile ) const
    {
        return _sourcePath != profile.getSourceSkeletonPath() || _targetPath != profile.getTargetSkeletonPath();
    }

    void RetargetPreviewModel::reloadSkeletons( const RetargetProfile& profile )
    {
        // 스켈레톤은 런타임과 같은 공유 캐시의 한 벌을 읽는다(빈 경로는 읽지 않는다 — nullptr).
        const shared_ptr<const Skeleton> pSource = SharedAssetCache<Skeleton>::acquire( profile.getSourceSkeletonPath() );
        const shared_ptr<const Skeleton> pTarget = SharedAssetCache<Skeleton>::acquire( profile.getTargetSkeletonPath() );
        applySkeletons( profile, pSource.get(), pTarget.get() );
    }

    void RetargetPreviewModel::applySkeletons( const RetargetProfile& profile, const Skeleton* pSource, const Skeleton* pTarget )
    {
        _sourcePath    = profile.getSourceSkeletonPath();
        _targetPath    = profile.getTargetSkeletonPath();
        _bSourceReady  = pSource != nullptr ? SW_TRUE : SW_FALSE;
        _bTargetReady  = pTarget != nullptr ? SW_TRUE : SW_FALSE;
        _bFramePending = SW_TRUE;
        if ( pSource != nullptr )
            _source = *pSource;
        if ( pTarget != nullptr )
            _target = *pTarget;
        rebuildPoses( profile );
    }

    void RetargetPreviewModel::rebuildPoses( const RetargetProfile& profile )
    {
        using Internal = RetargetPreviewModelInternal;
        _listSourcePosition.clear();
        _listTargetReferencePosition.clear();
        _listTargetPosePosition.clear();
        if ( isSourceReady() )
        {
            Pose sourcePose;
            sourcePose.setToReference( _source );
            Internal::computeBonePositions( _source, sourcePose, _listSourcePosition );
        }
        if ( isTargetReady() == false )
            return;
        Pose targetPose;
        targetPose.setToReference( _target );
        Internal::computeBonePositions( _target, targetPose, _listTargetReferencePosition );
        // 리타기터와 같은 길로 리타깃 포즈를 얹는다.
        if ( Internal::canApplyTargetPose( profile, _target ) && PoseRetargeter::applyTargetPose( profile, _target, targetPose ) == false )
            targetPose.setToReference( _target );
        Internal::computeBonePositions( _target, targetPose, _listTargetPosePosition );
    }

    bool RetargetPreviewModel::hasSourceBone( const hashed_string& name ) const
    {
        return isSourceReady() && _source.findBoneIndex( name ).has_value();
    }

    bool RetargetPreviewModel::hasTargetBone( const hashed_string& name ) const
    {
        return isTargetReady() && _target.findBoneIndex( name ).has_value();
    }

    bool RetargetPreviewModel::consumeFramePending()
    {
        const bool bPending = _bFramePending == SW_TRUE;
        _bFramePending      = SW_FALSE;
        return bPending;
    }

    bool RetargetPreviewModel::findFrameSphere( float3& outCenter, float32& outRadius ) const
    {
        float3 minimum{ 1.0e9f };
        float3 maximum{ -1.0e9f };
        bool   bAny = false;
        for ( const vector<float3>* pList : { &_listSourcePosition, &_listTargetReferencePosition, &_listTargetPosePosition } )
        {
            for ( const float3& position : *pList )
            {
                minimum = float3{ MathUtil::min( minimum._x, position._x ), MathUtil::min( minimum._y, position._y ), MathUtil::min( minimum._z, position._z ) };
                maximum = float3{ MathUtil::max( maximum._x, position._x ), MathUtil::max( maximum._y, position._y ), MathUtil::max( maximum._z, position._z ) };
                bAny    = true;
            }
        }
        if ( bAny == false )
            return false;
        outCenter = ( minimum + maximum ) * 0.5f;
        outRadius = MathUtil::max( ( maximum - minimum ).getLength() * 0.5f * kFramePadding, kMinFrameRadius );
        return true;
    }
} // namespace sw::editor
