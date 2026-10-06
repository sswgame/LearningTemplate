#include "pch.h"

#include "GameFramework/Base/Combat/LockOnSelector.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct LockOnSelectorInternal
        {
            static constexpr float32 kRadianToDegree = 180.0f / 3.14159265358979f;

            /** @brief 눈에서 대상까지의 3D 거리입니다(공중 대상 · 높낮이가 있는 록온도 같은 기준). */
            static float32 computeDistance( const float3& lhs, const float3& rhs ) { return ( lhs - rhs ).getLength(); }

            /** @brief 앞 방향과 이루는 각(도, 0..180)입니다. */
            static float32 computeAngle( const float3& eye, const float3& forward, const float3& position )
            {
                const float3  toTarget = position - eye;
                const float32 length   = toTarget.getLength() * forward.getLength();
                if ( length < 1.0e-6f )
                    return 0.0f;
                const float32 cosine = MathUtil::clamp( toTarget.dot( forward ) / length, -1.0f, 1.0f );
                return MathUtil::acos( cosine ) * kRadianToDegree;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 LockOnSelector::computeYawOffset( const float3& eye, const float3& forward, const float3& position )
    {
        const float32 forwardYaw = MathUtil::atan2( forward._x, forward._z );
        const float32 targetYaw  = MathUtil::atan2( position._x - eye._x, position._z - eye._z );
        float32       offset     = ( targetYaw - forwardYaw ) * LockOnSelectorInternal::kRadianToDegree;
        while ( offset > 180.0f )
            offset -= 360.0f;
        while ( offset < -180.0f )
            offset += 360.0f;
        return offset;
    }

    uint64 LockOnSelector::pickBest( const float3& eye, const float3& forward, const vector<LockOnCandidate>& listCandidate )
    {
        uint64  bestId    = 0;
        float32 bestScore = MathUtil::kMaxFloat;
        for ( const LockOnCandidate& candidate : listCandidate )
        {
            if ( candidate._bVisible == SW_FALSE || candidate._id == 0 )
                continue;
            const float32 distance = LockOnSelectorInternal::computeDistance( eye, candidate._position );
            const float32 angle    = LockOnSelectorInternal::computeAngle( eye, forward, candidate._position );
            if ( distance > _settings._maxDistance || angle > _settings._maxAngle )
                continue;
            const float32 score = distance / _settings._maxDistance + angle / MathUtil::max( 1.0f, _settings._maxAngle ) * _settings._angleWeight - candidate._priority;
            if ( score < bestScore )
            {
                bestScore = score;
                bestId    = candidate._id;
            }
        }
        _target     = bestId;
        _hiddenTime = 0.0f;
        return _target;
    }

    uint64 LockOnSelector::cycle( const float3& eye, const float3& forward, const vector<LockOnCandidate>& listCandidate, int32 direction )
    {
        float32 currentYaw = 0.0f;
        for ( const LockOnCandidate& candidate : listCandidate )
        {
            if ( candidate._id == _target )
                currentYaw = computeYawOffset( eye, forward, candidate._position );
        }
        uint64  bestId  = 0;
        float32 bestGap = MathUtil::kMaxFloat;
        for ( const LockOnCandidate& candidate : listCandidate )
        {
            if ( candidate._id == _target || candidate._id == 0 || candidate._bVisible == SW_FALSE ||
                 LockOnSelectorInternal::computeDistance( eye, candidate._position ) > _settings._maxDistance ||
                 LockOnSelectorInternal::computeAngle( eye, forward, candidate._position ) > _settings._maxAngle )
                continue;
            const float32 gap = ( computeYawOffset( eye, forward, candidate._position ) - currentYaw ) * static_cast<float32>( direction >= 0 ? 1 : -1 );
            if ( gap > 0.0f && gap < bestGap )
            {
                bestGap = gap;
                bestId  = candidate._id;
            }
        }
        if ( bestId != 0 )
        {
            _target     = bestId;
            _hiddenTime = 0.0f;
        }
        return _target;
    }

    bool LockOnSelector::update( const float3& eye, const vector<LockOnCandidate>& listCandidate, float32 deltaTime )
    {
        if ( _target == 0 )
            return false;
        const LockOnCandidate* pTarget = nullptr;
        for ( const LockOnCandidate& candidate : listCandidate )
        {
            if ( candidate._id == _target )
                pTarget = &candidate;
        }
        if ( pTarget == nullptr || LockOnSelectorInternal::computeDistance( eye, pTarget->_position ) > _settings._breakDistance )
        {
            release();
            return false;
        }
        _hiddenTime = pTarget->_bVisible == SW_TRUE ? 0.0f : _hiddenTime + deltaTime;
        if ( _hiddenTime > _settings._lostSightGrace )
        {
            release();
            return false;
        }
        return true;
    }

    void LockOnSelector::release()
    {
        _target     = 0;
        _hiddenTime = 0.0f;
    }

    void LockOnSelector::writeState( Archive& outArchive ) const
    {
        outArchive << _target;
        outArchive << _hiddenTime;
    }

    bool LockOnSelector::readState( Archive& archive )
    {
        uint64  target     = 0;
        float32 hiddenTime = 0.0f;
        archive >> target;
        archive >> hiddenTime;
        if ( archive.isError() || ( 0.0f <= hiddenTime ) == false )
            return false;
        _target     = target;
        _hiddenTime = hiddenTime;
        return true;
    }
} // namespace sw
