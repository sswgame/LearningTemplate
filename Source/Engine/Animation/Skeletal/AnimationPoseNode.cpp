#include "pch.h"

#include "Engine/Animation/Skeletal/AnimationPoseNode.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    BlendPosesNode::BlendPosesNode( const IAnimationPlayable& inputA, const IAnimationPlayable& inputB, const hashed_string& weightParameter )
        : _pInputA{ &inputA }
        , _pInputB{ &inputB }
        , _weightParameter{ weightParameter }
        , _scratchPose{}
    {
    }

    bool BlendPosesNode::evaluatePose( const AnimationPoseParams& params, float32 time, Pose& inoutPose ) const
    {
        if ( params._pSampler == nullptr || params._pParameter == nullptr )
        {
            SW_LOG_ERROR_ID( "AnimationPoseNode.BlendPosesNodeNeedsSamplerParameter", "BlendPosesNode needs a sampler and a parameter set" );
            return false;
        }
        const float32 timeB  = mapTimeToInputB( time );
        const float32 weight = computeWeight( *params._pParameter );
        // 무게가 한쪽에 다 있으면 다른 입력은 샘플하지 않는다(언리얼 TwoWayBlend 의 관련 무게 생략과 같다) — 0 인 입력까지 늘 평가하지 않게.
        if ( weight <= 0.0f )
        {
            params._pSampler->samplePose( *_pInputA, time, inoutPose );
            return true;
        }
        if ( weight >= 1.0f )
        {
            params._pSampler->samplePose( *_pInputB, timeB, inoutPose );
            return true;
        }
        _scratchPose = inoutPose;
        params._pSampler->samplePose( *_pInputA, time, inoutPose );
        params._pSampler->samplePose( *_pInputB, timeB, _scratchPose );
        Pose::blend( inoutPose, _scratchPose, weight, inoutPose );
        return true;
    }

    BoneTransform BlendPosesNode::computeRootMotion( const AnimationParameterSet& parameter, const AnimationTimeStep& step, const IAnimationRootMotionSource& source ) const
    {
        // 포즈와 같은 가중치 · 같은 위상으로 섞는다 — 둘째 입력의 걸음은 정규화 위치로 옮긴 구간이다(반복 수는 같다).
        const float32 weight = computeWeight( parameter );
        if ( weight <= 0.0f )
            return source.computeRootMotion( *_pInputA, step );
        AnimationTimeStep stepB = step;
        stepB._previousTime     = mapTimeToInputB( step._previousTime );
        stepB._currentTime      = mapTimeToInputB( step._currentTime );
        if ( weight >= 1.0f )
            return source.computeRootMotion( *_pInputB, stepB );
        return BoneTransform::blend( source.computeRootMotion( *_pInputA, step ), source.computeRootMotion( *_pInputB, stepB ), weight );
    }

    void BlendPosesNode::accumulateCurves( const AnimationParameterSet& parameter, float32 time, float32 weight, IAnimationCurveSink& sink ) const
    {
        const float32 blendWeight = computeWeight( parameter );
        if ( blendWeight < 1.0f )
            sink.accumulateCurves( *_pInputA, time, weight * ( 1.0f - blendWeight ) );
        if ( blendWeight > 0.0f )
            sink.accumulateCurves( *_pInputB, mapTimeToInputB( time ), weight * blendWeight );
    }

    float32 BlendPosesNode::mapTimeToInputB( float32 time ) const
    {
        // 둘째 입력은 첫 입력과 같은 정규화 위치다 — 길이가 달라도 위상이 맞는다.
        const float32 lengthA = _pInputA->getPlayLength();
        return lengthA > 0.0f ? time / lengthA * _pInputB->getPlayLength() : 0.0f;
    }

    float32 BlendPosesNode::computeWeight( const AnimationParameterSet& parameter ) const
    {
        return MathUtil::saturate( parameter.getFloat( _weightParameter ) );
    }
} // namespace sw

namespace sw
{
    MirrorPoseNode::MirrorPoseNode( const IAnimationPlayable& input, AnimationMirrorAxis axis )
        : _pInput{ &input }
        , _axis{ axis }
        , _scratchPose{}
        , _listModel{}
        , _listReferenceModel{}
        , _listMirroredModel{}
    {
    }

    bool MirrorPoseNode::evaluatePose( const AnimationPoseParams& params, float32 time, Pose& inoutPose ) const
    {
        if ( params._pSampler == nullptr || params._pSkeleton == nullptr || params._pMirroredBone == nullptr )
        {
            // 포즈 단계마다 도는 길이다 — 표가 없는 데이터 오류는 한 번만 알린다(포즈는 입력 없이 들어온 그대로).
            SW_LOG_ERROR_ONCE_ID( "AnimationPoseNode.MirrorNeedsSkeleton", "MirrorPoseNode needs a sampler, a skeleton and its mirror bone map (the skeleton's mirror table)" );
            return false;
        }
        const Skeleton&       skeleton     = *params._pSkeleton;
        const vector<uint32>& listMirrored = *params._pMirroredBone;
        const uint32          boneCount    = skeleton.getBoneCount();
        if ( listMirrored.size() != boneCount || inoutPose.getBoneCount() != boneCount )
        {
            SW_LOG_ERROR_ONCE_ID( "AnimationPoseNode.MirrorBoneCountMismatch", "MirrorPoseNode: the pose (%# bones) and the mirror bone map (%#) must match the skeleton (%# bones)", inoutPose.getBoneCount(),
                                  static_cast<uint32>( listMirrored.size() ), boneCount );
            return false;
        }
        params._pSampler->samplePose( *_pInput, time, inoutPose );
        const vector<int32>& listParent = skeleton.getParentIndices();
        inoutPose.computeModelSpace( listParent, _listModel );
        _scratchPose.setToReference( skeleton );
        _scratchPose.computeModelSpace( listParent, _listReferenceModel );

        // 축에 수직인 면으로 비추기 — 두 번 곱해(S M S) 회전 행렬식이 양으로 남는다.
        const float3   reflectScale{ _axis == AnimationMirrorAxis::X ? -1.0f : 1.0f, _axis == AnimationMirrorAxis::Y ? -1.0f : 1.0f, _axis == AnimationMirrorAxis::Z ? -1.0f : 1.0f };
        const float4x4 reflect = float4x4::makeScale( reflectScale );
        _listMirroredModel.resize( boneCount );
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            const uint32 sourceIndex = listMirrored[boneIndex];
            // 짝 본 축이 서로 거울상이 아닐 수 있다 — 레퍼런스에서 "비춘 짝 → 이 본" 의 로컬 차이를 구해 먼저 곱한다(행 벡터 규약: 로컬이 왼쪽).
            const float4x4 frameOffset    = _listReferenceModel[boneIndex] * ( reflect * _listReferenceModel[sourceIndex] * reflect ).invert();
            _listMirroredModel[boneIndex] = frameOffset * ( reflect * _listModel[sourceIndex] * reflect );
        }
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            const int32    parentIndex = listParent[boneIndex];
            const float4x4 local       = parentIndex < 0 ? _listMirroredModel[boneIndex] : _listMirroredModel[boneIndex] * _listMirroredModel[static_cast<uint32>( parentIndex )].invert();
            inoutPose.setBoneTransform( boneIndex, BoneTransform::makeFromMatrix( local ) );
        }
        return true;
    }

    BoneTransform MirrorPoseNode::computeRootMotion( const AnimationParameterSet& /*parameter*/, const AnimationTimeStep& step, const IAnimationRootMotionSource& source ) const
    {
        // 축 면 비추기: 이동은 축 성분이 뒤집히고, 회전(축 벡터는 유사 벡터)은 축이 아닌 두 성분이 뒤집힌다(S R S).
        BoneTransform delta = source.computeRootMotion( *_pInput, step );
        switch ( _axis )
        {
            case AnimationMirrorAxis::X:
                delta._translation._x = -delta._translation._x;
                delta._rotation._y    = -delta._rotation._y;
                delta._rotation._z    = -delta._rotation._z;
                break;
            case AnimationMirrorAxis::Y:
                delta._translation._y = -delta._translation._y;
                delta._rotation._x    = -delta._rotation._x;
                delta._rotation._z    = -delta._rotation._z;
                break;
            case AnimationMirrorAxis::Z:
                delta._translation._z = -delta._translation._z;
                delta._rotation._x    = -delta._rotation._x;
                delta._rotation._y    = -delta._rotation._y;
                break;
            case AnimationMirrorAxis::None:
                break; // 축 없는 Mirror 는 만들 때 거절된다(`isSatisfiedBy`)
        }
        return delta;
    }

    void MirrorPoseNode::accumulateCurves( const AnimationParameterSet& /*parameter*/, float32 time, float32 weight, IAnimationCurveSink& sink ) const
    {
        sink.accumulateCurves( *_pInput, time, weight ); // 커브는 좌우가 없다 — 입력 그대로
    }
} // namespace sw

namespace sw
{
    MontagePoseNode::MontagePoseNode( const IAnimationPlayable& input, vector<Section>&& listSection )
        : _pInput{ &input }
        , _listSection{ std::move( listSection ) }
        , _playLength{ 0.0f }
        , _lastTime{ -1.0f }
        , _clipTime{ 0.0f }
        , _currentSection{ 0 }
        , _listScratchSpan{}
    {
        SW_ASSERT( _listSection.empty() == false );
        // 첫 섹션에서 `next` 만 따라간 길이다. 같은 섹션에 다시 닿으면 반복이라 끝이 없다.
        vector<uint8> listVisited( _listSection.size(), SW_FALSE );
        for ( uint32 sectionIndex = 0; sectionIndex != kNoSection; sectionIndex = _listSection[sectionIndex]._next )
        {
            if ( listVisited[sectionIndex] == SW_TRUE )
            {
                _playLength = kEndlessLength;
                break;
            }
            listVisited[sectionIndex] = SW_TRUE;
            _playLength += _listSection[sectionIndex]._endSeconds - _listSection[sectionIndex]._startSeconds;
        }
        _clipTime = _listSection[0]._startSeconds;
    }

    bool MontagePoseNode::evaluatePose( const AnimationPoseParams& params, float32 time, Pose& inoutPose ) const
    {
        if ( params._pSampler == nullptr || params._pParameter == nullptr )
        {
            SW_LOG_ERROR_ID( "AnimationPoseNode.MontageNeedsSamplerParameter", "MontagePoseNode needs a sampler and a parameter set" );
            return false;
        }
        const Cursor cursor = findCursor( time, *params._pParameter );
        _currentSection     = cursor._section;
        _clipTime           = cursor._clipTime;
        _lastTime           = time;
        params._pSampler->samplePose( *_pInput, _clipTime, inoutPose );
        return true;
    }

    BoneTransform MontagePoseNode::computeRootMotion( const AnimationParameterSet& parameter, const AnimationTimeStep& step, const IAnimationRootMotionSource& source ) const
    {
        // 다시 재생한 걸음(시각이 뒤로 갔다)은 처음부터 지금까지다.
        const float32 previousTime = step._currentTime < step._previousTime ? 0.0f : step._previousTime;
        _listScratchSpan.clear();
        (void)advanceCursor( findCursor( previousTime, parameter ), step._currentTime - previousTime, parameter, &_listScratchSpan );
        // 조각은 차례로 잇는다 — 클립의 반복 경계 잇기(`AnimationClip::computeRootMotionDelta`)와 같은 합(이동은 더하고 회전은 곱한다).
        BoneTransform total{};
        for ( const ClipSpan& span : _listScratchSpan )
        {
            AnimationTimeStep spanStep;
            spanStep._previousTime    = span._start;
            spanStep._currentTime     = span._end;
            const BoneTransform delta = source.computeRootMotion( *_pInput, spanStep );
            total._translation        = total._translation + delta._translation;
            total._rotation           = ( total._rotation * delta._rotation ).normalize();
        }
        return total;
    }

    void MontagePoseNode::accumulateCurves( const AnimationParameterSet& parameter, float32 time, float32 weight, IAnimationCurveSink& sink ) const
    {
        sink.accumulateCurves( *_pInput, findCursor( time, parameter )._clipTime, weight );
    }

    MontagePoseNode::Cursor MontagePoseNode::findCursor( float32 time, const AnimationParameterSet& parameter ) const
    {
        // 마지막 평가 뒤의 시각이면 그 자리에서 이어 걷고, 앞이면(다시 재생 · 아직 평가 전) 첫 섹션부터 그 시각까지 다시 걷는다.
        if ( _lastTime >= 0.0f && time >= _lastTime )
            return advanceCursor( Cursor{ _currentSection, _clipTime }, time - _lastTime, parameter, nullptr );
        return advanceCursor( Cursor{ 0, _listSection[0]._startSeconds }, time, parameter, nullptr );
    }

    MontagePoseNode::Cursor MontagePoseNode::advanceCursor( Cursor cursor, float32 delta, const AnimationParameterSet& parameter, vector<ClipSpan>* pOutListSpan ) const
    {
        float32 remaining = delta;
        while ( remaining > 0.0f )
        {
            const Section& section = _listSection[cursor._section];
            const float32  toEnd   = section._endSeconds - cursor._clipTime;
            if ( remaining < toEnd )
            {
                if ( pOutListSpan != nullptr )
                    pOutListSpan->push_back( ClipSpan{ cursor._clipTime, cursor._clipTime + remaining } );
                cursor._clipTime += remaining;
                return cursor;
            }
            if ( pOutListSpan != nullptr )
                pOutListSpan->push_back( ClipSpan{ cursor._clipTime, section._endSeconds } );
            remaining -= toEnd;
            const bool   bBranch = section._branch != kNoSection && parameter.isTrue( section._branchParameter );
            const uint32 next    = bBranch ? section._branch : section._next;
            if ( next == kNoSection )
            {
                cursor._clipTime = section._endSeconds; // 다음 섹션이 없다 — 끝에서 멈춘다
                return cursor;
            }
            cursor._section  = next;
            cursor._clipTime = _listSection[next]._startSeconds; // 섹션 길이는 0 보다 크다(생성이 본다) — 고리가 돈다
        }
        return cursor;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        struct AnimationPoseNodeInternal
        {
            /** @brief `BlendPoses` — 입력 둘을 "weight" 파라미터로 섞는다. */
            static unique_ptr<IAnimationPoseNode> createBlendPoses( const vector<const IAnimationPlayable*>& listInput, const AnimationGraphPoseNodeDef& def, string_view /*nodeName*/ )
            {
                return make_unique<BlendPosesNode>( *listInput[0], *listInput[1], def._weightParameter );
            }

            static void fillBlendPoses( AnimationGraphPoseNodeDef& inoutDef ) { inoutDef._weightParameter = hashed_string( "Weight" ); }

            static bool validateBlendPoses( const AnimationGraphPoseNodeDef& def, string_view nodeName )
            {
                if ( def._weightParameter.empty() == false )
                    return true;
                SW_LOG_ERROR_ID( "AnimationPoseNode.BlendPosesNeedsWeight", "Pose node '%#' of type BlendPoses needs a 'weight' parameter", nodeName );
                return false;
            }

            /** @brief `Mirror` — 입력 하나를 축 면으로 비춘다. 짝 본은 평가에 받는 스켈레톤 옆 미러 표다. */
            static unique_ptr<IAnimationPoseNode> createMirror( const vector<const IAnimationPlayable*>& listInput, const AnimationGraphPoseNodeDef& def, string_view /*nodeName*/ )
            {
                return make_unique<MirrorPoseNode>( *listInput[0], def._mirrorAxis );
            }

            static void fillMirror( AnimationGraphPoseNodeDef& inoutDef ) { inoutDef._mirrorAxis = AnimationMirrorAxis::X; }

            static bool validateMirror( const AnimationGraphPoseNodeDef& def, string_view nodeName )
            {
                if ( def._mirrorAxis != AnimationMirrorAxis::None )
                    return true;
                SW_LOG_ERROR_ID( "AnimationPoseNode.MirrorNeedsAxis", "Pose node '%#' of type Mirror needs 'mirror_axis' (X, Y or Z)", nodeName );
                return false;
            }

            /** @brief 섹션 이름의 번호입니다. 비거나 없는 이름이면 없음입니다(검사가 먼저 본다). */
            [[nodiscard]] static optional<uint32> findSection( const vector<AnimationMontageSection>& listSection, const hashed_string& name )
            {
                if ( name.empty() )
                    return nullopt;
                return VectorUtil::findIndexBy<&AnimationMontageSection::_name, uint32>( listSection, name );
            }

            /** @brief 클립 처음부터 끝까지 한 섹션이다. */
            static void fillMontage( AnimationGraphPoseNodeDef& inoutDef ) { inoutDef._listSection.assign( 1u, AnimationMontageSection{ hashed_string( "Default" ), 0.0f, {}, {}, {} } ); }

            static bool validateMontage( const AnimationGraphPoseNodeDef& def, string_view nodeName )
            {
                const vector<AnimationMontageSection>& listSection = def._listSection;
                if ( listSection.empty() )
                {
                    SW_LOG_ERROR_ID( "AnimationPoseNode.MontageNeedsSections", "Pose node '%#' of type Montage needs at least one <section>", nodeName );
                    return false;
                }
                bool bValid = true;
                for ( uint32 index = 0; index < listSection.size(); ++index )
                {
                    const AnimationMontageSection& section = listSection[index];
                    if ( section._name.empty() || findSection( listSection, section._name ) != index )
                    {
                        SW_LOG_ERROR_ID( "AnimationPoseNode.MontageSectionName", "Pose node '%#': montage section %# needs a name no other section has (got '%#')", nodeName, index, section._name.c_str() );
                        bValid = false;
                    }
                    if ( index > 0 && section._startSeconds <= listSection[index - 1]._startSeconds )
                    {
                        SW_LOG_ERROR_ID( "AnimationPoseNode.MontageSectionOrder", "Pose node '%#': montage section '%#' must start after the previous section", nodeName, section._name.c_str() );
                        bValid = false;
                    }
                    const bool bNextKnown   = section._next.empty() || findSection( listSection, section._next ).has_value();
                    const bool bBranchKnown = section._branch.empty() || findSection( listSection, section._branch ).has_value();
                    if ( bNextKnown == false || bBranchKnown == false )
                    {
                        SW_LOG_ERROR_ID( "AnimationPoseNode.MontageSectionLink", "Pose node '%#': montage section '%#' links to an unknown section (next '%#', branch '%#')", nodeName, section._name.c_str(),
                                         section._next.c_str(), section._branch.c_str() );
                        bValid = false;
                    }
                    if ( section._branch.empty() != section._branchParameter.empty() )
                    {
                        SW_LOG_ERROR_ID( "AnimationPoseNode.MontageBranchNeedsBoth", "Pose node '%#': montage section '%#' needs both 'branch' and 'branch_param' or neither", nodeName, section._name.c_str() );
                        bValid = false;
                    }
                }
                return bValid;
            }

            /** @brief `Montage` — 섹션을 번호로 풀고 끝 시각(다음 섹션의 시작, 마지막은 입력 길이)을 정한다. 섹션이 입력 클립 밖에서 시작하면 오류다. */
            static unique_ptr<IAnimationPoseNode> createMontage( const vector<const IAnimationPlayable*>& listInput, const AnimationGraphPoseNodeDef& def, string_view nodeName )
            {
                const vector<AnimationMontageSection>& listDef     = def._listSection;
                const float32                          inputLength = listInput[0]->getPlayLength();
                vector<MontagePoseNode::Section>       listSection;
                listSection.reserve( listDef.size() );
                for ( uint32 index = 0; index < listDef.size(); ++index )
                {
                    const AnimationMontageSection& source = listDef[index];
                    MontagePoseNode::Section&      target = listSection.emplace_back();
                    target._startSeconds                  = source._startSeconds;
                    target._endSeconds                    = index + 1 < listDef.size() ? listDef[index + 1]._startSeconds : inputLength;
                    target._next                          = findSection( listDef, source._next ).value_or( MontagePoseNode::kNoSection ); // 빈 이름은 멈춤 · 갈래 없음
                    target._branch                        = findSection( listDef, source._branch ).value_or( MontagePoseNode::kNoSection );
                    target._branchParameter               = source._branchParameter;
                    if ( target._endSeconds <= target._startSeconds )
                    {
                        SW_LOG_ERROR_ID( "AnimationPoseNode.MontageSectionOutsideClip", "Pose node '%#': montage section '%#' starts at %# s, at or past the end of its %# s input", nodeName, source._name.c_str(),
                                         source._startSeconds, inputLength );
                        return nullptr;
                    }
                }
                return make_unique<MontagePoseNode>( *listInput[0], std::move( listSection ) );
            }
        };
    } // namespace

    AnimationPoseNodeRegistry::AnimationPoseNodeRegistry()
        : _registry{}
    {
        using Internal = AnimationPoseNodeInternal;
        // 빈 표의 엔진 이름들이다 — 겹치면 레지스트리가 오류로 알린다.
        (void)registerType( hashed_string( "BlendPoses" ), AnimationPoseNodeType{ 2u, &Internal::createBlendPoses, &Internal::validateBlendPoses, &Internal::fillBlendPoses } );
        (void)registerType( hashed_string( "Mirror" ), AnimationPoseNodeType{ 1u, &Internal::createMirror, &Internal::validateMirror, &Internal::fillMirror } );
        (void)registerType( hashed_string( "Montage" ), AnimationPoseNodeType{ 1u, &Internal::createMontage, &Internal::validateMontage, &Internal::fillMontage } );
    }

    SW_ENGINE_SERVICE_SUBSYSTEM( AnimationPoseNodeRegistry, GameVisible, Process, Services, (), SW_ENGINE_DEFAULT_FACTORY( AnimationPoseNodeRegistry ) );

    bool AnimationPoseNodeRegistry::registerType( const hashed_string& typeName, const AnimationPoseNodeType& type )
    {
        if ( typeName.empty() || type._create == nullptr )
        {
            SW_LOG_ERROR_ID( "AnimationPoseNode.PoseNodeTypeInvalid", "Pose node type '%#' needs a name and a factory", typeName.c_str() );
            return false;
        }
        return _registry.addLogged( typeName, type, "Animation pose node type" );
    }

    const AnimationPoseNodeType* AnimationPoseNodeRegistry::findType( const hashed_string& typeName ) const
    {
        return _registry.find( typeName );
    }

    vector<hashed_string> AnimationPoseNodeRegistry::getTypeNames() const
    {
        vector<hashed_string> listName;
        listName.reserve( _registry.getCount() );
        for ( uint32 index = 0; index < _registry.getCount(); ++index )
        {
            listName.push_back( _registry.getNameAt( index ) );
        }
        return listName;
    }

    bool AnimationPoseNodeRegistry::makeDefaultDef( const hashed_string& typeName, AnimationGraphPoseNodeDef& outDef ) const
    {
        const AnimationPoseNodeType* pType = findType( typeName );
        if ( pType == nullptr )
        {
            SW_LOG_ERROR_ID( "AnimationPoseNode.PoseNodeTypeUnknown", "Pose node type '%#' is not registered", typeName.c_str() );
            return false;
        }
        outDef       = AnimationGraphPoseNodeDef{};
        outDef._type = typeName;
        for ( uint32 inputIndex = 0; inputIndex < pType->_inputCount; ++inputIndex )
        {
            fixed_string<constant::buffer::kMax32> inputName;
            formatstring( inputName.data(), inputName.capacity(), "Input%#", inputIndex + 1u );
            outDef._listInput.push_back( hashed_string( inputName.view() ) );
        }
        if ( pType->_fillDefault != nullptr )
            pType->_fillDefault( outDef );
        return true;
    }

    bool AnimationPoseNodeRegistry::isSatisfiedBy( const AnimationPoseNodeType& type, const AnimationGraphPoseNodeDef& def, string_view nodeName )
    {
        const bool bHasEmptyInput = VectorUtil::contains( def._listInput, hashed_string{} );
        bool       bValid         = true;
        if ( def._listInput.size() != type._inputCount || bHasEmptyInput )
        {
            SW_LOG_ERROR_ID( "AnimationPoseNode.PoseNodeInputs", "Pose node '%#' of type '%#' needs %# named 'inputs' (got %#)", nodeName, def._type.c_str(), type._inputCount,
                             static_cast<uint32>( def._listInput.size() ) );
            bValid = false;
        }
        return ( type._validate == nullptr || type._validate( def, nodeName ) ) && bValid;
    }
} // namespace sw

namespace sw
{
    AnimationPoseNodeSet::AnimationPoseNodeSet()
        : _listEntry{}
        , _pClipSource{ nullptr }
    {
    }

    bool AnimationPoseNodeSet::rebuild( const AnimationGraphAsset& graph, const IAnimationPlayableSource& clipSource )
    {
        _listEntry.clear();
        _pClipSource = &clipSource;
        for ( const AnimationGraphNode& node : graph._listNode )
        {
            if ( node.hasPoseNode() == false )
                continue;
            unique_ptr<IAnimationPoseNode> poseNode = createNode( node, clipSource );
            if ( poseNode == nullptr )
            {
                _listEntry.clear();
                return false;
            }
            _listEntry.push_back( Entry{ hashed_string( node._name ), std::move( poseNode ) } );
        }
        return true;
    }

    const IAnimationPlayable* AnimationPoseNodeSet::findPlayable( const hashed_string& name ) const
    {
        const IAnimationPoseNode* pNode = findNode( name );
        if ( pNode != nullptr )
            return pNode;
        return _pClipSource != nullptr ? _pClipSource->findPlayable( name ) : nullptr;
    }

    const IAnimationPoseNode* AnimationPoseNodeSet::findNode( const hashed_string& name ) const
    {
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._name == name )
                return entry._node.get();
        }
        return nullptr;
    }

    void AnimationPoseNodeSet::clear()
    {
        _listEntry.clear();
        _pClipSource = nullptr;
    }

    unique_ptr<IAnimationPoseNode> AnimationPoseNodeSet::createNode( const AnimationGraphNode& node, const IAnimationPlayableSource& clipSource ) const
    {
        const AnimationGraphPoseNodeDef&  def = node._poseNode;
        vector<const IAnimationPlayable*> listInput;
        for ( const hashed_string& inputName : def._listInput )
        {
            const IAnimationPlayable* pInput = clipSource.findPlayable( inputName );
            if ( pInput == nullptr )
            {
                SW_LOG_ERROR_ID( "AnimationPoseNode.PoseNodeInputNot", "Pose node '%#' input '%#' was not found", node._name.c_str(), inputName.c_str() );
                return nullptr;
            }
            listInput.push_back( pInput );
        }
        const AnimationPoseNodeType* pType = engine::getSubsystem<AnimationPoseNodeRegistry>().findType( def._type );
        if ( pType == nullptr )
        {
            SW_LOG_ERROR_ID( "AnimationPoseNode.PoseNodeNoPose", "Pose node '%#' has an unknown pose node type '%#'", node._name.c_str(), def._type.c_str() );
            return nullptr;
        }
        // 로드(`AnimationGraphAsset::parsePoseNode`)가 확인하지만, 코드로 만든 그래프도 같은 계약을 지킨다.
        if ( AnimationPoseNodeRegistry::isSatisfiedBy( *pType, def, node._name ) == false )
            return nullptr;
        return pType->_create( listInput, def, node._name );
    }
} // namespace sw
