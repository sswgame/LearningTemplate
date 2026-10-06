#include "pch.h"

#include "Engine/Animation/Rig/RigSpringChain.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Rig/RigIkSolver.h"
#include "Engine/Animation/Rig/RigPoseBuffer.h"

namespace sw
{
    namespace
    {
        struct RigSpringChainInternal
        {
            /** @brief 선분 위에서 @p point 에 가장 가까운 점입니다. */
            static float3 findClosestOnSegment( const float3& point, const float3& start, const float3& end )
            {
                const float3  segment       = end - start;
                const float32 lengthSquared = segment.getLengthSquared();
                if ( lengthSquared < 1e-12f )
                    return start;
                const float32 ratio = MathUtil::clamp( ( point - start ).dot( segment ) / lengthSquared, 0.0f, 1.0f );
                return start + segment * ratio;
            }

            /** @brief 입자를 부모에서 @p length 만큼 떨어지게 둡니다. */
            static float3 keepLength( const float3& parent, const float3& child, float32 length )
            {
                const float3  offset  = child - parent;
                const float32 current = offset.getLength();
                if ( current < 1e-8f )
                    return child;
                return parent + offset * ( length / current );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RigSpringChain::RigSpringChain()
        : _listPosition{}
        , _listPreviousPosition{}
        , _listAnimated{}
        , _listLength{}
        , _lastRootPosition{}
        , _accumulator{ 0.0f }
        , _lastStepCount{ 0 }
        , _bInitialized{ SW_FALSE }
    {
    }

    void RigSpringChain::reset()
    {
        _bInitialized = SW_FALSE;
        _accumulator  = 0.0f;
    }

    void RigSpringChain::simulate( RigPoseBuffer& pose, span<const uint32> listBone, const RigSpringSettings& settings, span<const RigSpringCollider> listCollider,
                                   const float4x4& worldFromModel, const float3& worldGravity, float32 deltaSeconds, const RigSolveSpace& space )
    {
        const uint32 count = static_cast<uint32>( listBone.size() );
        _lastStepCount     = 0;
        if ( count < 2 )
            return;
        _listAnimated.resize( count );
        _listLength.resize( count );
        for ( uint32 index = 0; index < count; ++index )
            _listAnimated[index] = float3::transform( pose.getModelPosition( listBone[index] ), worldFromModel );
        _listLength[0] = 0.0f;
        for ( uint32 index = 1; index < count; ++index )
            _listLength[index] = ( _listAnimated[index] - _listAnimated[index - 1] ).getLength();

        const float3 root      = _listAnimated[0];
        const bool   bTeleport = _bInitialized == SW_TRUE && ( root - _lastRootPosition ).getLength() > settings._teleportDistance;
        if ( _bInitialized == SW_FALSE || bTeleport || _listPosition.size() != count )
        {
            _listPosition         = _listAnimated;
            _listPreviousPosition = _listAnimated;
            _accumulator          = 0.0f;
            _bInitialized         = SW_TRUE;
            _lastRootPosition     = root;
        }

        const float32 step = MathUtil::max( settings._fixedStep, 1e-4f );
        _accumulator += MathUtil::max( deltaSeconds, 0.0f );
        uint32 stepCount = static_cast<uint32>( _accumulator / step );
        _accumulator -= static_cast<float32>( stepCount ) * step;
        if ( stepCount > settings._maxSubStep )
        {
            stepCount    = settings._maxSubStep;
            _accumulator = 0.0f; // 넘는 시간은 버린다 — 히치 뒤에 한꺼번에 따라잡느라 튀지 않게
        }

        const float3  planeNormal  = ( space._bPlanar == SW_TRUE ) ? float3::transformVector( space._planeNormal, worldFromModel ).normalize() : float3::Zero;
        const float32 retain       = 1.0f - MathUtil::saturate( settings._damping );
        const float32 stiffness    = MathUtil::saturate( settings._stiffness );
        const float3  gravity      = ( settings._bUseGravityOverride == SW_TRUE ) ? settings._gravityOverride : worldGravity * settings._gravityScale;
        const float3  gravityStep  = gravity * ( step * step );
        const float3  previousRoot = _lastRootPosition;
        for ( uint32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            // 뿌리는 애니메이션을 따른다 — 스텝마다 지난 프레임 자리에서 이번 자리까지 나눠 옮겨 빠른 움직임에도 끝이 튀지 않게 한다.
            const float32 ratio      = static_cast<float32>( stepIndex + 1 ) / static_cast<float32>( stepCount );
            _listPosition[0]         = float3::lerp( previousRoot, root, ratio );
            _listPreviousPosition[0] = _listPosition[0];
            for ( uint32 index = 1; index < count; ++index )
            {
                const float3 velocity        = ( _listPosition[index] - _listPreviousPosition[index] ) * retain;
                _listPreviousPosition[index] = _listPosition[index];
                float3 next                  = _listPosition[index] + velocity + gravityStep;
                // 애니메이션 자세 쪽으로: 부모 입자에서 애니메이션의 상대 방향만큼.
                const float3 goal = _listPosition[index - 1] + ( _listAnimated[index] - _listAnimated[index - 1] );
                next              = next + ( goal - next ) * stiffness;
                if ( space._bPlanar == SW_TRUE )
                    next = next - planeNormal * ( next - root ).dot( planeNormal );
                next = RigSpringChainInternal::keepLength( _listPosition[index - 1], next, _listLength[index] );
                for ( const RigSpringCollider& collider : listCollider )
                {
                    if ( collider._bone >= pose.getBoneCount() )
                        continue;
                    const float4x4 boneWorld = pose.getModelMatrix( collider._bone ) * worldFromModel;
                    const float3   pointA    = float3::transform( collider._pointA, boneWorld );
                    const float3   closest   = ( collider._shape == RigSpringColliderShape::Capsule )
                                                 ? RigSpringChainInternal::findClosestOnSegment( next, pointA, float3::transform( collider._pointB, boneWorld ) )
                                                 : pointA;
                    const float3   away      = next - closest;
                    const float32  distance  = away.getLength();
                    const float32  minimum   = collider._radius + settings._particleRadius;
                    if ( distance < minimum && distance > 1e-8f )
                        next = closest + away * ( minimum / distance );
                }
                _listPosition[index] = RigSpringChainInternal::keepLength( _listPosition[index - 1], next, _listLength[index] );
            }
        }
        _lastRootPosition = root;
        _lastStepCount    = stepCount;

        // 본을 뿌리부터 입자 쪽으로 돌린다(부모를 돌리면 자식 자리가 바뀌므로 차례로).
        const float4x4 modelFromWorld = worldFromModel.invert();
        for ( uint32 index = 0; index + 1 < count; ++index )
            pose.aimBoneAt( listBone[index], listBone[index + 1], float3::transform( _listPosition[index + 1], modelFromWorld ) );
    }
} // namespace sw
