#include "pch.h"

#include "Engine/Animation/Skeletal/AnimationPoseNode.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/Graph/AnimationGraphAsset.h"

namespace sw
{
    SW_LOG_CALLER( "AnimationPoseNode" );

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
            SW_LOG_ERROR( "BlendPosesNode needs a sampler and a parameter set" );
            return false;
        }
        // 둘째 입력은 첫 입력과 같은 정규화 위치에서 샘플한다 — 길이가 달라도 위상이 맞는다.
        const float32 lengthA        = _pInputA->getPlayLength();
        const float32 normalizedTime = lengthA > 0.0f ? time / lengthA : 0.0f;
        const float32 timeB          = normalizedTime * _pInputB->getPlayLength();
        const float32 weight         = MathUtil::saturate( params._pParameter->getFloat( _weightParameter ) );
        _scratchPose                 = inoutPose;
        params._pSampler->samplePose( *_pInputA, time, inoutPose );
        params._pSampler->samplePose( *_pInputB, timeB, _scratchPose );
        Pose::blend( inoutPose, _scratchPose, weight, inoutPose );
        return true;
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
        for ( const Entry& entry : _listEntry )
        {
            if ( entry._name == name )
                return entry._node.get();
        }
        return _pClipSource != nullptr ? _pClipSource->findPlayable( name ) : nullptr;
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
                SW_LOG_ERROR( "Pose node '%#' input '%#' was not found", node._name.c_str(), inputName.c_str() );
                return nullptr;
            }
            listInput.push_back( pInput );
        }
        switch ( def._type )
        {
            case AnimationPoseNodeType::BlendPoses:
            {
                // 로드(`AnimationGraphAsset::parsePoseNode`)가 확인하지만, 코드로 만든 그래프도 같은 규칙을 지킨다.
                if ( listInput.size() != 2 || def._weightParameter.empty() )
                {
                    SW_LOG_ERROR( "Pose node '%#' blend_poses needs two inputs and a weight parameter", node._name.c_str() );
                    return nullptr;
                }
                return make_unique<BlendPosesNode>( *listInput[0], *listInput[1], def._weightParameter );
            }
            case AnimationPoseNodeType::None:
            {
                break;
            }
        }
        SW_LOG_ERROR( "Pose node '%#' has no pose node type", node._name.c_str() );
        return nullptr;
    }
} // namespace sw
