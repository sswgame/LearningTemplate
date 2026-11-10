#include "pch.h"

#include "Engine/Animation/Skeletal/AnimationPoseNode.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/Graph/AnimationGraphAsset.h"
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
    namespace
    {
        struct AnimationPoseNodeInternal
        {
            /** @brief `BlendPoses` — 입력 둘을 "weight" 파라미터로 섞는다. */
            static unique_ptr<IAnimationPoseNode> createBlendPoses( const vector<const IAnimationPlayable*>& listInput, const AnimationGraphPoseNodeDef& def )
            {
                return make_unique<BlendPosesNode>( *listInput[0], *listInput[1], def._weightParameter );
            }
        };
    } // namespace

    AnimationPoseNodeRegistry::AnimationPoseNodeRegistry()
        : _registry{}
    {
        registerType( hashed_string( "BlendPoses" ), AnimationPoseNodeType{ 2u, &AnimationPoseNodeInternal::createBlendPoses, true } );
    }

    SW_ENGINE_SERVICE_SUBSYSTEM( AnimationPoseNodeRegistry, GameVisible, Services, (), SW_ENGINE_DEFAULT_FACTORY( AnimationPoseNodeRegistry ) );

    void AnimationPoseNodeRegistry::registerType( const hashed_string& typeName, const AnimationPoseNodeType& type )
    {
        if ( typeName.empty() || type._create == nullptr )
        {
            SW_LOG_ERROR_ID( "AnimationPoseNode.PoseNodeTypeInvalid", "Pose node type '%#' needs a name and a factory", typeName.c_str() );
            return;
        }
        _registry.addOrReplace( typeName, type, "Animation pose node type" );
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

    bool AnimationPoseNodeRegistry::isSatisfiedBy( const AnimationPoseNodeType& type, const AnimationGraphPoseNodeDef& def )
    {
        const bool bHasEmptyInput = VectorUtil::contains( def._listInput, hashed_string{} );
        const bool bWeightOk      = type._bRequiresWeight == false || def._weightParameter.empty() == false;
        return def._listInput.size() == type._inputCount && bHasEmptyInput == false && bWeightOk;
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
        if ( AnimationPoseNodeRegistry::isSatisfiedBy( *pType, def ) == false )
        {
            SW_LOG_ERROR_ID( "AnimationPoseNode.PoseNodeBlendPoses", "Pose node '%#' of type '%#' needs %# named inputs%#", node._name.c_str(), def._type.c_str(), pType->_inputCount,
                             pType->_bRequiresWeight ? " and a weight parameter" : "" );
            return nullptr;
        }
        return pType->_create( listInput, def );
    }
} // namespace sw
