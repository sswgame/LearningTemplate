#include "pch.h"

#include "Engine/Animation/Rig/RigInstance.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Skeleton.h"

namespace sw
{
    SW_LOG_CALLER( "RigInstance" );

    namespace
    {
        struct RigInstanceInternal
        {
            /** @brief 본 모델 변환 위에 본 기준 오프셋을 얹습니다(행벡터: 오프셋이 먼저). @p bScaled 면 오프셋 이동에 본 스케일을 곱합니다. */
            static void composeOnBone( RigPoseBuffer& pose, uint32 bone, const float3& offsetPosition, const quaternion& offsetRotation, bool bScaled,
                                       float3& outPosition, quaternion& outRotation )
            {
                const quaternion boneRotation = pose.getModelRotation( bone );
                float3           local        = offsetPosition;
                if ( bScaled )
                {
                    const float3 scale = pose.getModelScale( bone );
                    local              = float3{ local._x * scale._x, local._y * scale._y, local._z * scale._z };
                }
                outPosition = pose.getModelPosition( bone ) + float3::transform( local, boneRotation );
                outRotation = ( boneRotation * offsetRotation ).normalize();
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RigInstance::RigInstance()
        : _asset{}
        , _listTargetDef{}
        , _listTargetValue{}
        , _listTargetBone{}
        , _listTargetOffset{}
        , _listNode{}
        , _listNodeWrittenBone{}
        , _listSlotWeight{}
        , _listMorphWeight{}
        , _listSharedCollider{}
        , _listBlendScratch{}
        , _pose{}
        , _space{}
        , _pCurveSource{ nullptr }
        , _pendingDeltaSeconds{ 0.0f }
        , _evaluationCount{ 0 }
    {
    }

    RigInstance::~RigInstance() = default;

    bool RigInstance::initialize( shared_ptr<const RigAsset> asset, const Skeleton& skeleton, const IRigSocketResolver* pOwnSockets, string_view label )
    {
        shutdown();
        if ( asset == nullptr )
            return false;

        _listTargetDef = asset->getTargets();
        _listTargetValue.assign( _listTargetDef.size(), RigTargetValue{} );
        _listTargetBone.assign( _listTargetDef.size(), kExternalBone );
        _listTargetOffset.assign( _listTargetDef.size(), BoneTransform{} );
        bool bOk = true;
        for ( size_t targetIndex = 0; targetIndex < _listTargetDef.size(); ++targetIndex )
        {
            const RigTargetDef& target = _listTargetDef[targetIndex];
            if ( target.isExternal() )
                continue;
            if ( target._space.empty() == false )
            {
                SW_LOG_ERROR( "Rig '%#': target '%#' uses 'space' but is on this unit - 'space' is only for other units and objects", label, target._name.c_str() );
                bOk = false;
                continue;
            }
            hashed_string boneName = target._bone;
            BoneTransform offset   = target._offset;
            if ( target._kind == RigTargetKind::Socket )
            {
                BoneTransform         socketLocal{};
                const RigSocketLookup lookup = ( pOwnSockets != nullptr ) ? pOwnSockets->findSocket( target._socket, boneName, socketLocal ) : RigSocketLookup::Missing;
                if ( lookup == RigSocketLookup::OtherUnit )
                    continue; // 해석된 외형의 다른 유닛 소켓 — 호스트가 값을 넣는다
                if ( lookup == RigSocketLookup::Missing )
                {
                    SW_LOG_ERROR( "Rig '%#': target '%#' names unknown socket '%#'", label, target._name.c_str(), target._socket.c_str() );
                    bOk = false;
                    continue;
                }
                // 데이터 오프셋은 소켓 공간에서 먼저 걸린다(행벡터: 오프셋 × 소켓 로컬).
                offset = BoneTransform::makeFromMatrix( target._offset.toMatrix() * socketLocal.toMatrix() );
            }
            if ( boneName.empty() && target._kind == RigTargetKind::Socket )
            {
                // 부모가 빈 소켓은 유닛 뿌리(모델 원점) 기준이다.
                _listTargetBone[targetIndex]   = kModelOriginBone;
                _listTargetOffset[targetIndex] = offset;
                continue;
            }
            const int32 boneIndex = skeleton.findBoneIndex( boneName );
            if ( boneIndex < 0 )
            {
                SW_LOG_ERROR( "Rig '%#': target '%#' names unknown bone '%#'", label, target._name.c_str(), boneName.c_str() );
                bOk = false;
                continue;
            }
            _listTargetBone[targetIndex]   = boneIndex;
            _listTargetOffset[targetIndex] = offset;
        }

        _space._bPlanar = asset->isPlanar() ? SW_TRUE : SW_FALSE;
        _asset          = asset; // 노드 묶기가 대상 이름을 찾는다
        RigBindContext bindContext{};
        bindContext._pSkeleton = &skeleton;
        bindContext._pInstance = this;
        bindContext._label     = label;
        for ( const unique_ptr<RigNode>& prototype : asset->getNodes() )
        {
            unique_ptr<RigNode> node = prototype->clone();
            if ( node->bind( bindContext ) == false )
            {
                SW_LOG_ERROR( "Rig '%#': node '%#' (%#) could not be bound", label, node->getName().c_str(), node->getTypeName() );
                bOk = false;
                continue;
            }
            vector<uint32> listWritten;
            node->collectWrittenBones( listWritten );
            _listNodeWrittenBone.push_back( std::move( listWritten ) );
            _listNode.push_back( std::move( node ) );
        }
        if ( bOk == false )
        {
            shutdown();
            return false;
        }
        return true;
    }

    void RigInstance::shutdown()
    {
        _asset.reset();
        _listTargetDef.clear();
        _listTargetValue.clear();
        _listTargetBone.clear();
        _listTargetOffset.clear();
        _listNode.clear();
        _listNodeWrittenBone.clear();
        _listMorphWeight.clear();
        _pendingDeltaSeconds = 0.0f;
    }

    int32 RigInstance::findTargetIndex( const hashed_string& name ) const
    {
        for ( size_t index = 0; index < _listTargetDef.size(); ++index )
        {
            if ( _listTargetDef[index]._name == name )
                return static_cast<int32>( index );
        }
        return -1;
    }

    void RigInstance::setExternalTarget( uint32 targetIndex, const RigTargetValue& value )
    {
        if ( targetIndex < _listTargetValue.size() )
            _listTargetValue[targetIndex] = value;
    }

    bool RigInstance::resolveTarget( uint32 targetIndex, RigPoseBuffer& pose, float3& outPosition, quaternion& outRotation ) const
    {
        if ( targetIndex >= _listTargetDef.size() )
            return false;
        const int32 bone = _listTargetBone[targetIndex];
        if ( bone == kModelOriginBone )
        {
            outPosition = _listTargetOffset[targetIndex]._translation;
            outRotation = _listTargetOffset[targetIndex]._rotation;
            return true;
        }
        if ( bone >= 0 )
        {
            const BoneTransform& offset = _listTargetOffset[targetIndex];
            RigInstanceInternal::composeOnBone( pose, static_cast<uint32>( bone ), offset._translation, offset._rotation, true, outPosition, outRotation );
            return true;
        }
        const RigTargetValue& value = _listTargetValue[targetIndex];
        if ( value._bValid == SW_FALSE )
            return false;
        if ( 0 <= value._relativeBone && static_cast<uint32>( value._relativeBone ) < pose.getBoneCount() )
        {
            RigInstanceInternal::composeOnBone( pose, static_cast<uint32>( value._relativeBone ), value._position, value._rotation, false, outPosition, outRotation );
            return true;
        }
        outPosition = value._position;
        outRotation = value._rotation;
        return true;
    }

    void RigInstance::setSlotWeight( const hashed_string& slotName, float32 weight )
    {
        for ( SlotWeight& slot : _listSlotWeight )
        {
            if ( slot._name == slotName )
            {
                slot._weight = weight;
                return;
            }
        }
        _listSlotWeight.push_back( SlotWeight{ slotName, weight } );
    }

    void RigInstance::clearSlotWeight( const hashed_string& slotName )
    {
        for ( size_t index = 0; index < _listSlotWeight.size(); ++index )
        {
            if ( _listSlotWeight[index]._name == slotName )
            {
                _listSlotWeight.erase( _listSlotWeight.begin() + static_cast<ptrdiff_t>( index ) );
                return;
            }
        }
    }

    float32 RigInstance::computeNodeWeight( uint32 nodeIndex ) const
    {
        const RigNode& node   = *_listNode[nodeIndex];
        float32        weight = node.getWeight();
        if ( node.getWeightCurve().empty() == false )
            weight *= ( _pCurveSource != nullptr ) ? _pCurveSource->getCurveValue( node.getWeightCurve() ) : 0.0f;
        if ( node.getWeightSlot().empty() == false )
        {
            for ( const SlotWeight& slot : _listSlotWeight )
            {
                if ( slot._name == node.getWeightSlot() )
                    weight *= slot._weight;
            }
        }
        return MathUtil::saturate( weight );
    }

    RigNode* RigInstance::findNode( const hashed_string& name ) const
    {
        for ( const unique_ptr<RigNode>& node : _listNode )
        {
            if ( node->getName() == name )
                return node.get();
        }
        return nullptr;
    }

    bool RigInstance::setNodeControl( const hashed_string& nodeName, const hashed_string& control, float32 value )
    {
        RigNode* pNode = findNode( nodeName );
        return pNode != nullptr && pNode->setControl( control, value );
    }

    void RigInstance::addMorphWeight( const hashed_string& name, float32 weight )
    {
        for ( RigMorphWeight& morph : _listMorphWeight )
        {
            if ( morph._name == name )
            {
                morph._weight += weight;
                return;
            }
        }
        _listMorphWeight.push_back( RigMorphWeight{ name, weight } );
    }

    void RigInstance::prepare( const RigPrepareContext& context )
    {
        _pendingDeltaSeconds += MathUtil::max( context._deltaSeconds, 0.0f );
        for ( const unique_ptr<RigNode>& node : _listNode )
            node->prepare( context );
    }

    void RigInstance::evaluate( Pose& inoutLocalPose, const vector<int32>& listParent, const float4x4& worldFromModel )
    {
        if ( isInitialized() == false )
            return;
        _pose.initialize( inoutLocalPose, listParent );
        for ( RigMorphWeight& morph : _listMorphWeight )
            morph._weight = 0.0f; // 이름 목록은 남긴다 — 프레임마다 다시 잡지 않게

        RigEvaluateContext context{};
        context._pPose          = &_pose;
        context._pInstance      = this;
        context._worldFromModel = worldFromModel;
        context._modelFromWorld = worldFromModel.invert();
        context._pSpace         = &_space;
        context._deltaSeconds   = _pendingDeltaSeconds;
        for ( uint32 nodeIndex = 0; nodeIndex < static_cast<uint32>( _listNode.size() ); ++nodeIndex )
        {
            const float32 weight = computeNodeWeight( nodeIndex );
            if ( weight <= 0.0f )
                continue;
            const vector<uint32>& listWritten = _listNodeWrittenBone[nodeIndex];
            const bool            bBlend      = weight < 1.0f;
            if ( bBlend )
            {
                _listBlendScratch.resize( listWritten.size() );
                for ( size_t index = 0; index < listWritten.size(); ++index )
                    _listBlendScratch[index] = _pose.getLocal( listWritten[index] );
            }
            context._nodeWeight = weight;
            _listNode[nodeIndex]->evaluate( context );
            if ( bBlend )
            {
                // 노드 앞 포즈와 섞는다 — 로컬에서 섞으므로 쓰지 않은 자손은 그대로 따라온다.
                for ( size_t index = 0; index < listWritten.size(); ++index )
                    _pose.setLocal( listWritten[index], BoneTransform::blend( _listBlendScratch[index], _pose.getLocal( listWritten[index] ), weight ) );
            }
        }
        _pose.writeTo( inoutLocalPose );
        _pendingDeltaSeconds = 0.0f;
        ++_evaluationCount;
    }

    void RigInstance::reset()
    {
        for ( const unique_ptr<RigNode>& node : _listNode )
            node->reset();
    }
} // namespace sw
