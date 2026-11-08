#include "pch.h"

#include "Engine/Animation/Rig/RigJiggleNode.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigInstance.h"
#include "Engine/Animation/Rig/RigPoseBuffer.h"

namespace sw
{
    namespace
    {
        struct RigJiggleNodeFactory
        {
            static unique_ptr<RigNode> create() { return make_unique<RigJiggleNode>(); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RigJiggleNode::RigJiggleNode()
        : RigNode{}
        , _listBoneName{}
        , _listBone{}
        , _chain{}
        , _group{}
        , _bUseSharedColliders{ SW_FALSE }
    {
    }

    unique_ptr<RigNode> RigJiggleNode::clone() const
    {
        unique_ptr<RigJiggleNode> node = make_unique<RigJiggleNode>( *this );
        node->reset();
        return node;
    }

    bool RigJiggleNode::parse( RigXMLReader& reader )
    {
        bool bShared         = false;
        bool bOk             = reader.readNameList( "bones", _listBoneName, true );
        bOk                  = reader.readName( "group", _group, true ) && bOk;
        bOk                  = reader.readBool( "use_shared_colliders", bShared, false ) && bOk;
        _bUseSharedColliders = bShared ? SW_TRUE : SW_FALSE;
        if ( bOk && _listBoneName.size() < 2 )
        {
            reader.fail( "'bones' needs at least two bones (root and tip)" );
            return false;
        }
        return bOk && reader.isOk();
    }

    bool RigJiggleNode::bind( const RigBindContext& context )
    {
        return context.findBones( _listBoneName, _listBone, true );
    }

    RigSpringSettings RigJiggleNode::makeSettings( const RigJiggleGroupParams& group, float32 motionScale )
    {
        const float32     looseness = ( 1.0f - MathUtil::saturate( group._stiffness ) ) * MathUtil::clamp( motionScale, 0.0f, RigBodyInput::kMaxMotionScale );
        RigSpringSettings settings;
        settings._stiffness       = MathUtil::saturate( 1.0f - looseness );
        settings._damping         = group._damping;
        settings._gravityScale    = group._gravityScale * MathUtil::min( motionScale, 1.0f ); // 배율 0 이면 처짐도 없다(애니메이션 그대로)
        settings._particleRadius  = group._particleRadius;
        settings._maxSwingDegrees = group._maxSwingDegrees;
        return settings;
    }

    void RigJiggleNode::evaluate( RigEvaluateContext& context )
    {
        const RigBodyInput&         input  = context._pInstance->getBodyInput();
        const RigJiggleGroupParams* pGroup = input.findJiggleGroup( _group );
        if ( pGroup == nullptr )
            return; // 묶음이 없는 것은 `checkGroups` 가 오류로 알렸다 — 프레임마다 다시 남기지 않는다
        const RigSpringSettings settings = makeSettings( *pGroup, input.getMotionScale() );
        const float3            impulse  = input.getFrameImpulse() * ( pGroup->_impulseScale * input.getMotionScale() );
        if ( impulse.getLengthSquared() > 0.0f )
            _chain.addVelocity( impulse, settings._fixedStep );
        const vector<RigSpringCollider>&    listShared = context._pInstance->getSharedColliders();
        const span<const RigSpringCollider> colliders  = _bUseSharedColliders == SW_TRUE ? span<const RigSpringCollider>( listShared.data(), listShared.size() ) : span<const RigSpringCollider>{};
        _chain.simulate( *context._pPose, _listBone, settings, colliders, context._worldFromModel, context._worldGravity, context._deltaSeconds, *context._pSpace );
    }

    void RigJiggleNode::collectWrittenBones( vector<uint32>& inoutListBone ) const
    {
        inoutListBone.insert( inoutListBone.end(), _listBone.begin(), _listBone.end() );
    }

    bool RigJiggleNode::checkGroups( const RigInstance& instance, string_view label )
    {
        bool bValid = true;
        for ( uint32 nodeIndex = 0; nodeIndex < instance.getNodeCount(); ++nodeIndex )
        {
            const RigNode& node = instance.getNode( nodeIndex );
            if ( string_view( node.getTypeName() ) != kTypeName )
                continue;
            const hashed_string& group = static_cast<const RigJiggleNode&>( node ).getGroup();
            if ( instance.getBodyInput().findJiggleGroup( group ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "Rig.JiggleGroupMissing", "Rig '%#': Jiggle node '%#' names group '%#' the body profile does not define - it stays still", label, node.getName().c_str(), group.c_str() );
            bValid = false;
        }
        return bValid;
    }

    void RigJiggleNode::registerNodes( RigNodeRegistry& registry )
    {
        registry.registerNode( kTypeName, &RigJiggleNodeFactory::create );
    }
} // namespace sw
