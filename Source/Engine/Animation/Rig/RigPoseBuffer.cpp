#include "pch.h"

#include "Engine/Animation/Rig/RigPoseBuffer.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Rig/RigIkSolver.h"

namespace sw
{
    namespace
    {
        struct RigPoseBufferInternal
        {
            static float3 multiply( const float3& lhs, const float3& rhs ) { return float3{ lhs._x * rhs._x, lhs._y * rhs._y, lhs._z * rhs._z }; }

            static float3 divide( const float3& lhs, const float3& rhs )
            {
                const float32 x = MathUtil::abs( rhs._x ) > MathUtil::kEpsilon ? lhs._x / rhs._x : lhs._x;
                const float32 y = MathUtil::abs( rhs._y ) > MathUtil::kEpsilon ? lhs._y / rhs._y : lhs._y;
                const float32 z = MathUtil::abs( rhs._z ) > MathUtil::kEpsilon ? lhs._z / rhs._z : lhs._z;
                return float3{ x, y, z };
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RigPoseBuffer::RigPoseBuffer()
        : _listParent{}
        , _listLocalTranslation{}
        , _listLocalRotation{}
        , _listLocalScale{}
        , _listModelPosition{}
        , _listModelRotation{}
        , _listModelScale{}
        , _listDirty{}
        , _firstDirty{ 0 }
    {
    }

    void RigPoseBuffer::initialize( const Pose& localPose, const vector<int32>& listParent )
    {
        const uint32 boneCount = MathUtil::min( localPose.getBoneCount(), static_cast<uint32>( listParent.size() ) );
        _listParent.assign( listParent.begin(), listParent.begin() + boneCount );
        _listLocalTranslation.assign( localPose.getTranslations().begin(), localPose.getTranslations().begin() + boneCount );
        _listLocalRotation.assign( localPose.getRotations().begin(), localPose.getRotations().begin() + boneCount );
        _listLocalScale.assign( localPose.getScales().begin(), localPose.getScales().begin() + boneCount );
        _listModelPosition.resize( boneCount );
        _listModelRotation.resize( boneCount );
        _listModelScale.resize( boneCount );
        _listDirty.assign( boneCount, SW_TRUE );
        _firstDirty = 0;
    }

    void RigPoseBuffer::writeTo( Pose& outPose ) const
    {
        const uint32 boneCount    = MathUtil::min( outPose.getBoneCount(), getBoneCount() );
        float3*      pTranslation = outPose.getTranslationData();
        quaternion*  pRotation    = outPose.getRotationData();
        float3*      pScale       = outPose.getScaleData();
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            pTranslation[boneIndex] = _listLocalTranslation[boneIndex];
            pRotation[boneIndex]    = _listLocalRotation[boneIndex];
            pScale[boneIndex]       = _listLocalScale[boneIndex];
        }
    }

    bool RigPoseBuffer::isAncestorOf( int32 ancestorIndex, int32 boneIndex ) const
    {
        if ( ancestorIndex < 0 )
            return false;
        int32 current = boneIndex;
        while ( current >= 0 )
        {
            if ( current == ancestorIndex )
                return true;
            current = _listParent[static_cast<uint32>( current )];
        }
        return false;
    }

    BoneTransform RigPoseBuffer::getLocal( uint32 boneIndex ) const
    {
        BoneTransform transform{};
        transform._translation = _listLocalTranslation[boneIndex];
        transform._rotation    = _listLocalRotation[boneIndex];
        transform._scale       = _listLocalScale[boneIndex];
        return transform;
    }

    void RigPoseBuffer::setLocal( uint32 boneIndex, const BoneTransform& transform )
    {
        _listLocalTranslation[boneIndex] = transform._translation;
        _listLocalRotation[boneIndex]    = transform._rotation;
        _listLocalScale[boneIndex]       = transform._scale;
        markDirty( boneIndex );
    }

    void RigPoseBuffer::setLocalRotation( uint32 boneIndex, const quaternion& rotation )
    {
        _listLocalRotation[boneIndex] = rotation;
        markDirty( boneIndex );
    }

    void RigPoseBuffer::markDirty( uint32 boneIndex )
    {
        _listDirty[boneIndex] = SW_TRUE;
        if ( boneIndex < _firstDirty )
            _firstDirty = boneIndex;
    }

    void RigPoseBuffer::refreshModel()
    {
        const uint32 boneCount = getBoneCount();
        if ( _firstDirty >= boneCount )
            return;
        for ( uint32 boneIndex = _firstDirty; boneIndex < boneCount; ++boneIndex )
        {
            const int32 parentIndex  = _listParent[boneIndex];
            const bool  bParentDirty = parentIndex >= 0 && _listDirty[static_cast<uint32>( parentIndex )] == SW_TRUE;
            if ( _listDirty[boneIndex] == SW_FALSE && bParentDirty == false )
                continue;
            _listDirty[boneIndex] = SW_TRUE; // 자손에게 전한다
            if ( parentIndex < 0 )
            {
                _listModelPosition[boneIndex] = _listLocalTranslation[boneIndex];
                _listModelRotation[boneIndex] = _listLocalRotation[boneIndex];
                _listModelScale[boneIndex]    = _listLocalScale[boneIndex];
                continue;
            }
            const uint32      parent         = static_cast<uint32>( parentIndex );
            const quaternion& parentRotation = _listModelRotation[parent];
            const float3      scaled         = RigPoseBufferInternal::multiply( _listLocalTranslation[boneIndex], _listModelScale[parent] );
            _listModelPosition[boneIndex]    = _listModelPosition[parent] + float3::transform( scaled, parentRotation );
            _listModelRotation[boneIndex]    = ( parentRotation * _listLocalRotation[boneIndex] ).normalize();
            _listModelScale[boneIndex]       = RigPoseBufferInternal::multiply( _listLocalScale[boneIndex], _listModelScale[parent] );
        }
        for ( uint32 boneIndex = _firstDirty; boneIndex < boneCount; ++boneIndex )
            _listDirty[boneIndex] = SW_FALSE;
        _firstDirty = boneCount;
    }

    float3 RigPoseBuffer::getModelPosition( uint32 boneIndex )
    {
        refreshModel();
        return _listModelPosition[boneIndex];
    }

    quaternion RigPoseBuffer::getModelRotation( uint32 boneIndex )
    {
        refreshModel();
        return _listModelRotation[boneIndex];
    }

    float3 RigPoseBuffer::getModelScale( uint32 boneIndex )
    {
        refreshModel();
        return _listModelScale[boneIndex];
    }

    float4x4 RigPoseBuffer::getModelMatrix( uint32 boneIndex )
    {
        refreshModel();
        return float4x4::createTrs( _listModelPosition[boneIndex], _listModelRotation[boneIndex], _listModelScale[boneIndex] );
    }

    void RigPoseBuffer::setModelRotation( uint32 boneIndex, const quaternion& rotation )
    {
        const int32 parentIndex = _listParent[boneIndex];
        if ( parentIndex < 0 )
        {
            setLocalRotation( boneIndex, rotation.normalize() );
            return;
        }
        const quaternion parentRotation = getModelRotation( static_cast<uint32>( parentIndex ) );
        setLocalRotation( boneIndex, ( parentRotation.inverse() * rotation ).normalize() );
    }

    void RigPoseBuffer::setModelPosition( uint32 boneIndex, const float3& position )
    {
        const int32 parentIndex = _listParent[boneIndex];
        if ( parentIndex < 0 )
        {
            _listLocalTranslation[boneIndex] = position;
            markDirty( boneIndex );
            return;
        }
        const uint32     parent          = static_cast<uint32>( parentIndex );
        const float3     parentPosition  = getModelPosition( parent );
        const quaternion parentRotation  = getModelRotation( parent );
        const float3     unrotated       = float3::transform( position - parentPosition, parentRotation.inverse() );
        _listLocalTranslation[boneIndex] = RigPoseBufferInternal::divide( unrotated, _listModelScale[parent] );
        markDirty( boneIndex );
    }

    void RigPoseBuffer::rotateModel( uint32 boneIndex, const quaternion& delta )
    {
        setModelRotation( boneIndex, delta * getModelRotation( boneIndex ) );
    }

    void RigPoseBuffer::aimBoneAt( uint32 boneIndex, uint32 childIndex, const float3& newChildPosition )
    {
        const float3 bonePosition = getModelPosition( boneIndex );
        const float3 current      = getModelPosition( childIndex ) - bonePosition;
        const float3 desired      = newChildPosition - bonePosition;
        if ( current.getLengthSquared() <= MathUtil::kEpsilonSquared || desired.getLengthSquared() <= MathUtil::kEpsilonSquared )
            return;
        rotateModel( boneIndex, RigIkSolver::makeFromToRotation( current, desired ) );
    }
} // namespace sw
