#include "pch.h"

#include "Engine/Animation/Retarget/PoseRetargeter.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Codec/AnimCodec.h"
#include "Engine/Animation/Skeleton.h"

namespace sw
{
    SW_LOG_CALLER( "PoseRetargeter" );

    namespace
    {
        struct PoseRetargeterInternal
        {
            /** @brief 파일 굽기의 표본율(Hz)입니다 — 임포트 규칙의 기본과 같다. */
            static constexpr float32 kBakeSampleRate = 30.0f;

            static bool findBone( const Skeleton& skeleton, const hashed_string& name, const utf8* pSide, uint32& outBone )
            {
                const int32 boneIndex = skeleton.findBoneIndex( name );
                if ( boneIndex < 0 )
                {
                    SW_LOG_ERROR( "Retarget: %# skeleton has no bone '%#'", pSide, name.c_str() );
                    return false;
                }
                outBone = static_cast<uint32>( boneIndex );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PoseRetargeter::PoseRetargeter()
        : _listPair{}
        , _listIkChain{}
        , _listGoal{}
        , _listSourceParent{}
        , _listTargetParent{}
        , _targetReference{}
        , _sourceBuffer{}
        , _targetBuffer{}
        , _sourceRootReference{}
        , _targetRootReference{}
        , _sourcePelvisReference{}
        , _targetPelvisReference{}
        , _heightRatio{ 1.0f }
        , _sourceRoot{ 0 }
        , _targetRoot{ 0 }
        , _sourcePelvis{ 0 }
        , _targetPelvis{ 0 }
        , _translationMode{ RetargetTranslationMode::ScaleByPelvisHeight }
        , _bInitialized{ SW_FALSE }
    {
    }

    bool PoseRetargeter::initialize( const RetargetProfile& profile, const Skeleton& sourceSkeleton, const Skeleton& targetSkeleton, const Pose* pTargetReference )
    {
        _bInitialized = SW_FALSE;
        _listPair.clear();
        _listIkChain.clear();
        _listSourceParent = sourceSkeleton.getParentIndices();
        _listTargetParent = targetSkeleton.getParentIndices();
        if ( pTargetReference != nullptr && pTargetReference->getBoneCount() == targetSkeleton.getBoneCount() )
            _targetReference = *pTargetReference;
        else
            _targetReference.setToReference( targetSkeleton );
        _translationMode = profile.getTranslationMode();

        bool bOk = PoseRetargeterInternal::findBone( sourceSkeleton, profile.getSourceRoot(), "source", _sourceRoot ) &&
                   PoseRetargeterInternal::findBone( targetSkeleton, profile.getTargetRoot(), "target", _targetRoot ) &&
                   PoseRetargeterInternal::findBone( sourceSkeleton, profile.getSourcePelvis(), "source", _sourcePelvis ) &&
                   PoseRetargeterInternal::findBone( targetSkeleton, profile.getTargetPelvis(), "target", _targetPelvis );
        if ( bOk == false )
            return false;

        Pose sourceReference;
        sourceReference.setToReference( sourceSkeleton );
        _sourceBuffer.initialize( sourceReference, _listSourceParent );
        _targetBuffer.initialize( _targetReference, _listTargetParent );

        vector<uint32> listSourceBone;
        vector<uint32> listTargetBone;
        listSourceBone.push_back( _sourceRoot );
        listTargetBone.push_back( _targetRoot );
        listSourceBone.push_back( _sourcePelvis );
        listTargetBone.push_back( _targetPelvis );
        for ( const RetargetChain& chain : profile.getChains() )
        {
            vector<uint32> listChainSource;
            vector<uint32> listChainTarget;
            for ( const hashed_string& name : chain._listSourceBone )
            {
                uint32 bone = 0;
                bOk         = bOk && PoseRetargeterInternal::findBone( sourceSkeleton, name, "source", bone );
                listChainSource.push_back( bone );
            }
            for ( const hashed_string& name : chain._listTargetBone )
            {
                uint32 bone = 0;
                bOk         = bOk && PoseRetargeterInternal::findBone( targetSkeleton, name, "target", bone );
                listChainTarget.push_back( bone );
            }
            if ( bOk == false || listChainSource.empty() || listChainTarget.empty() )
                return false;
            // 본 수가 다르면 사슬 길이 비율로 원본 본을 고른다(가장 가까운 것).
            const size_t sourceCount = listChainSource.size();
            const size_t targetCount = listChainTarget.size();
            for ( size_t targetIndex = 0; targetIndex < targetCount; ++targetIndex )
            {
                const float32 ratio       = targetCount > 1 ? static_cast<float32>( targetIndex ) / static_cast<float32>( targetCount - 1 ) : 0.0f;
                const size_t  sourceIndex = static_cast<size_t>( MathUtil::round( ratio * static_cast<float32>( sourceCount - 1 ) ) );
                listSourceBone.push_back( listChainSource[sourceIndex] );
                listTargetBone.push_back( listChainTarget[targetIndex] );
            }
            if ( chain._bIkGoal == SW_TRUE && targetCount >= 2 )
            {
                IkChain ikChain{};
                ikChain._listTargetBone     = listChainTarget;
                ikChain._sourceEnd          = listChainSource.back();
                ikChain._sourceEndReference = _sourceBuffer.getModelPosition( listChainSource.back() );
                ikChain._targetEndReference = _targetBuffer.getModelPosition( listChainTarget.back() );
                _listIkChain.push_back( ikChain );
            }
        }

        for ( size_t index = 0; index < listTargetBone.size(); ++index )
        {
            bool bDuplicate = false;
            for ( const BonePair& existing : _listPair )
                bDuplicate = bDuplicate || existing._target == listTargetBone[index];
            if ( bDuplicate )
                continue;
            BonePair pair{};
            pair._source                  = listSourceBone[index];
            pair._target                  = listTargetBone[index];
            pair._sourceReferenceRotation = _sourceBuffer.getModelRotation( pair._source );
            pair._targetReferenceRotation = _targetBuffer.getModelRotation( pair._target );
            _listPair.push_back( pair );
        }
        // 대상 본 순서(부모가 먼저)로 — 모델 회전을 쓸 때 부모가 이미 이번 값이어야 한다.
        std::sort( _listPair.begin(), _listPair.end(), &PoseRetargeter::isPairBefore );

        _sourceRootReference       = _sourceBuffer.getModelPosition( _sourceRoot );
        _targetRootReference       = _targetBuffer.getModelPosition( _targetRoot );
        _sourcePelvisReference     = _sourceBuffer.getModelPosition( _sourcePelvis );
        _targetPelvisReference     = _targetBuffer.getModelPosition( _targetPelvis );
        const float32 sourceHeight = _sourcePelvisReference._y - _sourceRootReference._y;
        const float32 targetHeight = _targetPelvisReference._y - _targetRootReference._y;
        _heightRatio               = ( MathUtil::abs( sourceHeight ) > MathUtil::kEpsilon ) ? targetHeight / sourceHeight : 1.0f;
        _bInitialized              = SW_TRUE;
        return true;
    }

    void PoseRetargeter::retarget( const Pose& sourcePose, Pose& outTargetPose )
    {
        outTargetPose = _targetReference;
        if ( _bInitialized == SW_FALSE )
            return;
        _sourceBuffer.initialize( sourcePose, _listSourceParent );
        _targetBuffer.initialize( _targetReference, _listTargetParent );

        // 1) 모델 공간 회전 차이를 옮긴다.
        for ( const BonePair& pair : _listPair )
        {
            const quaternion delta = ( _sourceBuffer.getModelRotation( pair._source ) * RigIkSolver::makeInverse( pair._sourceReferenceRotation ) ).normalize();
            _targetBuffer.setModelRotation( pair._target, delta * pair._targetReferenceRotation );
        }

        // 2) 뿌리 · 골반 이동 — 레퍼런스에서 움직인 만큼을 높이 비로.
        if ( _translationMode != RetargetTranslationMode::None )
        {
            const float32 scale = ( _translationMode == RetargetTranslationMode::ScaleByPelvisHeight ) ? _heightRatio : 1.0f;
            _targetBuffer.setModelPosition( _targetRoot, _targetRootReference + ( _sourceBuffer.getModelPosition( _sourceRoot ) - _sourceRootReference ) * scale );
            _targetBuffer.setModelPosition( _targetPelvis, _targetPelvisReference + ( _sourceBuffer.getModelPosition( _sourcePelvis ) - _sourcePelvisReference ) * scale );
        }

        // 3) IK 목표 — 원본 끝이 레퍼런스에서 움직인 만큼(높이 비)을 대상 끝 레퍼런스에 더한 자리.
        const float32 goalScale = ( _translationMode == RetargetTranslationMode::Copy ) ? 1.0f : _heightRatio;
        _listGoal.resize( _listIkChain.size() );
        float32 pelvisDrop = 0.0f;
        for ( size_t chainIndex = 0; chainIndex < _listIkChain.size(); ++chainIndex )
        {
            const IkChain& chain  = _listIkChain[chainIndex];
            const float3   goal   = chain._targetEndReference + ( _sourceBuffer.getModelPosition( chain._sourceEnd ) - chain._sourceEndReference ) * goalScale;
            _listGoal[chainIndex] = goal;
            // 다리를 다 펴도 닿지 않는 목표(늘린 다리의 보폭 끝)면 골반을 그만큼 내린다 — 발이 목표에서 떨어지지 않게(발이 미끄러지지 않게).
            float32 reach = 0.0f;
            for ( size_t boneIndex = 1; boneIndex < chain._listTargetBone.size(); ++boneIndex )
                reach += ( _targetBuffer.getModelPosition( chain._listTargetBone[boneIndex] ) - _targetBuffer.getModelPosition( chain._listTargetBone[boneIndex - 1] ) ).getLength();
            const float3  toGoal     = goal - _targetBuffer.getModelPosition( chain._listTargetBone.front() );
            const float32 usable     = reach * 0.999f;
            const float32 horizontal = toGoal._x * toGoal._x + toGoal._z * toGoal._z;
            if ( toGoal.getLength() <= usable || horizontal >= usable * usable )
                continue;
            pelvisDrop = MathUtil::min( pelvisDrop, toGoal._y + MathUtil::sqrt( usable * usable - horizontal ) );
        }
        if ( pelvisDrop < 0.0f )
            _targetBuffer.setModelPosition( _targetPelvis, _targetBuffer.getModelPosition( _targetPelvis ) + float3{ 0.0f, pelvisDrop, 0.0f } );

        // 끝 본의 모델 회전은 1 의 값을 지킨다.
        const RigSolveSpace space{};
        RigChainSettings    settings{};
        for ( size_t chainIndex = 0; chainIndex < _listIkChain.size(); ++chainIndex )
        {
            const IkChain&   chain       = _listIkChain[chainIndex];
            const uint32     targetEnd   = chain._listTargetBone.back();
            const quaternion endRotation = _targetBuffer.getModelRotation( targetEnd );
            if ( chain._listTargetBone.size() == 3 )
                (void)RigIkSolver::solveTwoBone( _targetBuffer, chain._listTargetBone[0], chain._listTargetBone[1], targetEnd, _listGoal[chainIndex], nullptr, space );
            else
                (void)RigIkSolver::solveFabrik( _targetBuffer, chain._listTargetBone, _listGoal[chainIndex], {}, settings, space );
            _targetBuffer.setModelRotation( targetEnd, endRotation );
        }

        _targetBuffer.writeTo( outTargetPose );
    }

    bool RetargetBakeUtil::bakeClip( PoseRetargeter& retargeter, const AnimClip& sourceClip, const Skeleton& sourceSkeleton, const Skeleton& targetSkeleton,
                                     float32 sampleRate, const IAnimCodec& codec, const AnimCodecSettings& settings, AnimClip& outClip )
    {
        if ( retargeter.isInitialized() == false || sampleRate <= 0.0f )
            return false;
        const float32 duration    = sourceClip.getDuration();
        const uint32  sampleCount = MathUtil::max( 2u, static_cast<uint32>( MathUtil::ceil( duration * sampleRate ) ) + 1u );

        AnimRawClip raw;
        raw._sampleCount = sampleCount;
        raw._sampleRate  = ( duration > 0.0f ) ? static_cast<float32>( sampleCount - 1 ) / duration : sampleRate;
        for ( uint32 boneIndex = 0; boneIndex < targetSkeleton.getBoneCount(); ++boneIndex )
        {
            raw._listTrackName.push_back( targetSkeleton.getBone( boneIndex )._name );
            raw._listTrackParent.push_back( targetSkeleton.getBone( boneIndex )._parentIndex );
        }
        raw._listSample.reserve( static_cast<size_t>( sampleCount ) * targetSkeleton.getBoneCount() );

        vector<int32> listTrackToBone;
        sourceClip.makeTrackToBoneMap( sourceSkeleton, listTrackToBone );
        Pose trackPose;
        Pose sourcePose;
        Pose targetPose;
        for ( uint32 sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
        {
            const float32 time = MathUtil::min( static_cast<float32>( sampleIndex ) / raw._sampleRate, duration );
            sourcePose.setToReference( sourceSkeleton );
            if ( sourceClip.sampleTracks( time, trackPose ) == false )
            {
                SW_LOG_ERROR( "Retarget bake: clip '%#' could not be sampled", sourceClip.getName().c_str() );
                return false;
            }
            AnimClip::copyTracksToPose( trackPose, listTrackToBone, sourcePose );
            retargeter.retarget( sourcePose, targetPose );
            for ( uint32 boneIndex = 0; boneIndex < targetPose.getBoneCount(); ++boneIndex )
                raw._listSample.push_back( targetPose.getBoneTransform( boneIndex ) );
        }

        outClip = AnimClip{};
        if ( outClip.compressFrom( raw, codec, settings, nullptr ) == false )
            return false;
        outClip.setName( sourceClip.getName() );
        outClip.setLooping( sourceClip.isLoopingByDefault() );
        for ( const AnimNotifyEvent& event : sourceClip.getNotifyTrack().getEvents() )
            outClip.addNotify( event );
        for ( const AnimCurve& curve : sourceClip.getCurves() )
            outClip.addCurve( curve );
        // 루트 모션 트랙은 원본 뿌리를 가리켰으면 대상 뿌리로 옮긴다.
        const int32 sourceRootMotion = sourceClip.getRootMotionTrack();
        if ( 0 <= sourceRootMotion && static_cast<size_t>( sourceRootMotion ) < listTrackToBone.size() &&
             listTrackToBone[static_cast<size_t>( sourceRootMotion )] == static_cast<int32>( retargeter.getSourceRootBone() ) )
            outClip.setRootMotionTrack( static_cast<int32>( retargeter.getTargetRootBone() ) );
        return true;
    }

    bool RetargetBakeUtil::bakeClipFile( string_view profilePath, string_view sourceClipPath, string_view outClipPath )
    {
        RetargetProfile profile;
        Skeleton        sourceSkeleton;
        Skeleton        targetSkeleton;
        AnimClip        sourceClip;
        const bool      bLoaded = profile.loadFromResource( profilePath ) && sourceSkeleton.loadFromResource( profile.getSourceSkeletonPath() ) &&
                             targetSkeleton.loadFromResource( profile.getTargetSkeletonPath() ) && sourceClip.loadFromResource( sourceClipPath );
        if ( bLoaded == false )
        {
            SW_LOG_ERROR( "Retarget bake: '%#' / '%#' could not be loaded", profilePath, sourceClipPath );
            return false;
        }
        const IAnimCodec* pCodec = AnimCodecRegistry::findCodec( sourceClip.getCodecId() );
        PoseRetargeter    retargeter;
        AnimClip          baked;
        const bool        bBaked = pCodec != nullptr && retargeter.initialize( profile, sourceSkeleton, targetSkeleton, nullptr ) &&
                            bakeClip( retargeter, sourceClip, sourceSkeleton, targetSkeleton, PoseRetargeterInternal::kBakeSampleRate, *pCodec, AnimCodecSettings{},
                                      baked );
        if ( bBaked == false || baked.saveToFile( outClipPath ) == false )
        {
            SW_LOG_ERROR( "Retarget bake: '%#' could not be baked to '%#'", sourceClipPath, outClipPath );
            return false;
        }
        SW_LOG_INFO( "Retarget bake: '%#' -> '%#' (%# tracks, %# s)", sourceClipPath, outClipPath, baked.getTrackCount(), baked.getDuration() );
        return true;
    }
} // namespace sw
