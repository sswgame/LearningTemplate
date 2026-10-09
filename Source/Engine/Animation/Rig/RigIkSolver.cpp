#include "pch.h"

#include "Engine/Animation/Rig/RigIkSolver.h"

#include "Engine/Animation/Rig/RigPoseBuffer.h"

namespace sw
{
    namespace
    {
        struct RigIkSolverInternal
        {
            /** @brief 사슬 하나가 가질 수 있는 본 수의 상한입니다(풀이가 스택 배열을 쓴다 — 프레임마다 힙을 잡지 않게). */
            static constexpr uint32 kMaxChainBone = 32;

            /** @brief @p direction 에 수직인 아무 단위 벡터입니다. */
            static float3 makePerpendicular( const float3& direction, const RigSolveSpace& space )
            {
                if ( space._bPlanar == SW_TRUE )
                    return space._planeNormal.cross( direction ).normalize();
                float3 perpendicular = direction.cross( float3::Up );
                if ( perpendicular.getLengthSquared() < 1e-6f )
                    perpendicular = direction.cross( float3::Right );
                return perpendicular.normalize();
            }

            /** @brief 회전을 @p fraction(0..1)만큼만 남깁니다(단위 회전에서 짧은 쪽 slerp). */
            static quaternion scaleRotation( const quaternion& rotation, float32 fraction )
            {
                if ( fraction >= 1.0f )
                    return rotation;
                if ( fraction <= 0.0f )
                    return quaternion::Identity;
                return quaternion::slerp( quaternion::Identity, rotation, fraction );
            }

            /** @brief 단위 회전과의 각(라디안, 0..π)입니다. */
            static float32 computeRotationAngle( const quaternion& rotation )
            {
                const float32 w = MathUtil::clamp( MathUtil::abs( rotation._w ), 0.0f, 1.0f );
                return 2.0f * MathUtil::acos( w );
            }

            /** @brief 사슬 위치 · 길이를 읽습니다. */
            static void readChain( RigPoseBuffer& pose, span<const uint32> listChainBone, float3* pOutPosition, float32* pOutLength )
            {
                const uint32 count = static_cast<uint32>( listChainBone.size() );
                for ( uint32 index = 0; index < count; ++index )
                {
                    pOutPosition[index] = pose.getModelPosition( listChainBone[index] );
                }
                for ( uint32 index = 0; index + 1 < count; ++index )
                {
                    pOutLength[index] = ( pOutPosition[index + 1] - pOutPosition[index] ).getLength();
                }
            }

            /** @brief 풀어 낸 위치로 사슬을 돌리고(뿌리부터) 제한을 겁니다. */
            static void applyChain( RigPoseBuffer& pose, span<const uint32> listChainBone, const float3* pPosition, span<const RigJointLimit> listLimit )
            {
                const uint32 count = static_cast<uint32>( listChainBone.size() );
                for ( uint32 index = 0; index + 1 < count; ++index )
                {
                    pose.aimBoneAt( listChainBone[index], listChainBone[index + 1], pPosition[index + 1] );
                    if ( index < listLimit.size() && listLimit[index]._type != RigJointLimitType::None )
                        RigIkSolver::applyJointLimit( pose, listChainBone[index], listLimit[index] );
                }
            }

            static bool hasAnyLimit( span<const RigJointLimit> listLimit )
            {
                for ( const RigJointLimit& limit : listLimit )
                {
                    if ( limit._type != RigJointLimitType::None )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float3 RigSolveSpace::projectVector( const float3& vector ) const
    {
        if ( _bPlanar == SW_FALSE )
            return vector;
        return vector - _planeNormal * vector.dot( _planeNormal );
    }

    float3 RigSolveSpace::projectPoint( const float3& point, const float3& origin ) const
    {
        return origin + projectVector( point - origin );
    }

    bool RigIkSolver::solveTwoBone( RigPoseBuffer& pose, uint32 rootBone, uint32 midBone, uint32 endBone, const float3& target, const float3* pPole,
                                    const RigSolveSpace& space )
    {
        const float3  rootPosition = pose.getModelPosition( rootBone );
        const float3  midPosition  = pose.getModelPosition( midBone );
        const float3  endPosition  = pose.getModelPosition( endBone );
        const float32 upperLength  = ( midPosition - rootPosition ).getLength();
        const float32 lowerLength  = ( endPosition - midPosition ).getLength();
        const float3  goal         = space.projectPoint( target, rootPosition );
        const float3  toGoal       = goal - rootPosition;
        const float32 goalDistance = toGoal.getLength();
        if ( goalDistance < MathUtil::kEpsilon || upperLength < MathUtil::kEpsilon || lowerLength < MathUtil::kEpsilon )
            return false;

        const float3  direction = toGoal / goalDistance;
        const float32 reach     = upperLength + lowerLength;
        const bool    bReached  = goalDistance <= reach;
        // 곧게 펴진 무릎은 굽힘 방향을 잃는다 — 아주 조금 남겨 다음 프레임도 같은 쪽으로 굽게 한다.
        const float32 minDistance = MathUtil::abs( upperLength - lowerLength ) + 1e-4f;
        const float32 distance    = MathUtil::clamp( goalDistance, minDistance, reach * 0.9999f );

        const float3 bendHint      = space.projectVector( ( pPole != nullptr ) ? ( *pPole - rootPosition ) : ( midPosition - rootPosition ) );
        float3       perpendicular = bendHint - direction * bendHint.dot( direction );
        if ( perpendicular.getLengthSquared() < 1e-8f )
            perpendicular = RigIkSolverInternal::makePerpendicular( direction, space );
        perpendicular = perpendicular.normalize();

        const float32 cosRoot = MathUtil::clamp( ( upperLength * upperLength + distance * distance - lowerLength * lowerLength ) / ( 2.0f * upperLength * distance ),
                                                 -1.0f, 1.0f );
        const float32 sinRoot = MathUtil::sqrt( MathUtil::max( 0.0f, 1.0f - cosRoot * cosRoot ) );
        const float3  newMid  = rootPosition + direction * ( upperLength * cosRoot ) + perpendicular * ( upperLength * sinRoot );
        const float3  newEnd  = rootPosition + direction * distance;
        pose.aimBoneAt( rootBone, midBone, newMid );
        pose.aimBoneAt( midBone, endBone, newEnd );
        return bReached;
    }

    bool RigIkSolver::solveFabrik( RigPoseBuffer& pose, span<const uint32> listChainBone, const float3& target, span<const RigJointLimit> listLimit,
                                   const RigChainSettings& settings, const RigSolveSpace& space )
    {
        const uint32 count = static_cast<uint32>( listChainBone.size() );
        if ( count < 2 || count > RigIkSolverInternal::kMaxChainBone )
            return false;
        float3  arrPosition[RigIkSolverInternal::kMaxChainBone];
        float32 arrLength[RigIkSolverInternal::kMaxChainBone];
        RigIkSolverInternal::readChain( pose, listChainBone, arrPosition, arrLength );

        const float3 root  = arrPosition[0];
        const float3 goal  = space.projectPoint( target, root );
        float32      total = 0.0f;
        for ( uint32 index = 0; index + 1 < count; ++index )
        {
            total += arrLength[index];
        }

        const bool bLimited = RigIkSolverInternal::hasAnyLimit( listLimit );
        if ( ( goal - root ).getLength() >= total )
        {
            // 닿지 않는다 — 목표 쪽으로 곧게 편다.
            const float3 direction = ( goal - root ).normalize();
            for ( uint32 index = 1; index < count; ++index )
            {
                arrPosition[index] = arrPosition[index - 1] + direction * arrLength[index - 1];
            }
            RigIkSolverInternal::applyChain( pose, listChainBone, arrPosition, listLimit );
            return false;
        }

        for ( uint32 iteration = 0; iteration < settings._iterationCount; ++iteration )
        {
            // 뒤로: 끝을 목표에 두고 뿌리 쪽으로 길이를 지킨다.
            arrPosition[count - 1] = goal;
            for ( uint32 index = count - 1; index > 0; --index )
            {
                const float3 direction = space.projectVector( arrPosition[index - 1] - arrPosition[index] ).normalize();
                arrPosition[index - 1] = arrPosition[index] + direction * arrLength[index - 1];
            }
            // 앞으로: 뿌리를 제자리에 두고 끝 쪽으로.
            arrPosition[0] = root;
            for ( uint32 index = 1; index < count; ++index )
            {
                const float3 direction = space.projectVector( arrPosition[index] - arrPosition[index - 1] ).normalize();
                arrPosition[index]     = arrPosition[index - 1] + direction * arrLength[index - 1];
            }
            if ( bLimited )
            {
                // 제한은 회전에 건다 — 위치를 회전으로 옮겨 제한하고, 제한된 위치에서 다음 반복을 시작한다.
                RigIkSolverInternal::applyChain( pose, listChainBone, arrPosition, listLimit );
                RigIkSolverInternal::readChain( pose, listChainBone, arrPosition, arrLength );
            }
            if ( ( arrPosition[count - 1] - goal ).getLength() <= settings._tolerance )
                break;
        }
        if ( bLimited == false )
            RigIkSolverInternal::applyChain( pose, listChainBone, arrPosition, listLimit );
        return ( pose.getModelPosition( listChainBone[count - 1] ) - goal ).getLength() <= settings._tolerance;
    }

    bool RigIkSolver::solveCcd( RigPoseBuffer& pose, span<const uint32> listChainBone, const float3& target, span<const RigJointLimit> listLimit,
                                const RigChainSettings& settings, const RigSolveSpace& space )
    {
        const uint32 count = static_cast<uint32>( listChainBone.size() );
        if ( count < 2 )
            return false;
        const uint32 endBone = listChainBone[count - 1];
        const float3 goal    = space.projectPoint( target, pose.getModelPosition( listChainBone[0] ) );
        for ( uint32 iteration = 0; iteration < settings._iterationCount; ++iteration )
        {
            for ( uint32 index = count - 1; index > 0; --index )
            {
                const uint32 joint         = listChainBone[index - 1];
                const float3 jointPosition = pose.getModelPosition( joint );
                const float3 toEnd         = space.projectVector( pose.getModelPosition( endBone ) - jointPosition );
                const float3 toGoal        = space.projectVector( goal - jointPosition );
                if ( toEnd.getLengthSquared() < 1e-10f || toGoal.getLengthSquared() < 1e-10f )
                    continue;
                quaternion    delta = makeFromToRotation( toEnd, toGoal );
                const float32 angle = RigIkSolverInternal::computeRotationAngle( delta );
                if ( angle > settings._maxStepAngle )
                    delta = RigIkSolverInternal::scaleRotation( delta, settings._maxStepAngle / angle );
                pose.rotateModel( joint, delta );
                if ( index - 1 < listLimit.size() && listLimit[index - 1]._type != RigJointLimitType::None )
                    applyJointLimit( pose, joint, listLimit[index - 1] );
            }
            if ( ( pose.getModelPosition( endBone ) - goal ).getLength() <= settings._tolerance )
                return true;
        }
        return ( pose.getModelPosition( endBone ) - goal ).getLength() <= settings._tolerance;
    }

    float32 RigIkSolver::aimBone( RigPoseBuffer& pose, uint32 bone, const float3& localAxis, const float3& target, float32 maxAngle, float32 weight,
                                  const RigSolveSpace& space )
    {
        const float3     position = pose.getModelPosition( bone );
        const quaternion rotation = pose.getModelRotation( bone );
        const float3     current  = space.projectVector( float3::transform( localAxis, rotation ) );
        const float3     desired  = space.projectVector( target - position );
        if ( current.getLengthSquared() < 1e-10f || desired.getLengthSquared() < 1e-10f )
            return 0.0f;
        quaternion    delta = makeFromToRotation( current, desired );
        const float32 angle = RigIkSolverInternal::computeRotationAngle( delta );
        if ( angle > maxAngle && angle > MathUtil::kEpsilon )
            delta = RigIkSolverInternal::scaleRotation( delta, maxAngle / angle );
        delta = RigIkSolverInternal::scaleRotation( delta, weight );
        pose.rotateModel( bone, delta );
        return RigIkSolverInternal::computeRotationAngle( delta );
    }

    quaternion RigIkSolver::makeFromToRotation( const float3& from, const float3& to )
    {
        const float3  fromUnit = from.normalize();
        const float3  toUnit   = to.normalize();
        const float32 cosine   = fromUnit.dot( toUnit );
        if ( cosine < -0.999999f )
        {
            // 반대 방향 — 아무 수직 축으로 180°.
            float3 axis = fromUnit.cross( float3::Right );
            if ( axis.getLengthSquared() < 1e-6f )
                axis = fromUnit.cross( float3::Up );
            return quaternion::createFromAxisAngle( axis.normalize(), MathUtil::kPi );
        }
        const float3 axis = fromUnit.cross( toUnit );
        return quaternion{ axis._x, axis._y, axis._z, 1.0f + cosine }.normalize();
    }

    void RigIkSolver::decomposeSwingTwist( const quaternion& delta, const float3& axis, quaternion& outSwing, quaternion& outTwist )
    {
        const float3     vectorPart{ delta._x, delta._y, delta._z };
        const float3     projected = axis * vectorPart.dot( axis );
        const quaternion twist{ projected._x, projected._y, projected._z, delta._w };
        if ( twist.normSquared() < 1e-12f )
        {
            // 축에 수직인 180° 회전 — 비틀림이 없다.
            outTwist = quaternion::Identity;
            outSwing = delta;
            return;
        }
        outTwist = twist.normalize();
        outSwing = ( delta * makeInverse( outTwist ) ).normalize();
    }

    float32 RigIkSolver::computeTwistAngle( const quaternion& twist, const float3& axis )
    {
        quaternion positive = twist;
        if ( positive._w < 0.0f )
            positive = -positive;
        const float3 vectorPart{ positive._x, positive._y, positive._z };
        return 2.0f * MathUtil::atan2( vectorPart.dot( axis ), positive._w );
    }

    void RigIkSolver::applyJointLimit( RigPoseBuffer& pose, uint32 bone, const RigJointLimit& limit )
    {
        if ( limit._type == RigJointLimitType::None )
            return;
        const quaternion delta = ( limit._referenceRotation.inverse() * pose.getLocalRotation( bone ) ).normalize();
        quaternion       swing{};
        quaternion       twist{};
        quaternion       limited{};
        if ( limit._type == RigJointLimitType::Cone )
        {
            decomposeSwingTwist( delta, limit._boneAxis, swing, twist );
            if ( swing._w < 0.0f )
                swing = -swing;
            const float32 swingAngle = RigIkSolverInternal::computeRotationAngle( swing );
            if ( swingAngle > limit._swingLimit && swingAngle > MathUtil::kEpsilon )
                swing = RigIkSolverInternal::scaleRotation( swing, limit._swingLimit / swingAngle );
            const float32 twistAngle = MathUtil::clamp( computeTwistAngle( twist, limit._boneAxis ), -limit._twistLimit, limit._twistLimit );
            limited                  = swing * quaternion::createFromAxisAngle( limit._boneAxis, twistAngle );
        }
        else
        {
            decomposeSwingTwist( delta, limit._hingeAxis, swing, twist );
            const float32 angle = MathUtil::clamp( computeTwistAngle( twist, limit._hingeAxis ), limit._minAngle, limit._maxAngle );
            limited             = quaternion::createFromAxisAngle( limit._hingeAxis, angle );
        }
        pose.setLocalRotation( bone, ( limit._referenceRotation * limited ).normalize() );
    }
} // namespace sw
