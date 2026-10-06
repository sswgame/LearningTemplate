#include "pch.h"

#include "GameFramework/Base/AI/AiPerception.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Navigation/NavGrid.h"

namespace sw
{
    AiPerception::AiPerception()
        : _settings{}
        , _listTarget{}
    {
    }

    bool AiPerception::canSee( const float3& eyePosition, const float3& forward, const float3& target, const NavGrid* pGrid, bool bAlreadySeen ) const
    {
        const float3  toTarget = float3{ target._x - eyePosition._x, 0.0f, target._z - eyePosition._z };
        const float32 distance = toTarget.getLength();
        const float32 range    = bAlreadySeen ? MathUtil::max( _settings._sightRange, _settings._loseSightRange ) : _settings._sightRange;
        if ( distance > range )
            return false;
        if ( distance > _settings._peripheralRange )
        {
            const float3  flatForward{ forward._x, 0.0f, forward._z };
            const float32 forwardLength = flatForward.getLength();
            if ( forwardLength > 1.0e-5f && toTarget.dot( flatForward ) / ( distance * forwardLength ) < MathUtil::cos( _settings._sightHalfAngle ) )
                return false;
        }
        return pGrid == nullptr || pGrid->hasLineOfSight( pGrid->computeCell( eyePosition ), pGrid->computeCell( target ) );
    }

    void AiPerception::sense( const float3& eyePosition, const float3& forward, const vector<AiStimulus>& listCandidate, const NavGrid* pGrid, float32 deltaTime )
    {
        for ( AiPerceivedTarget& target : _listTarget )
        {
            target._age += deltaTime;
            target._bHeard = SW_FALSE;
        }
        for ( const AiStimulus& stimulus : listCandidate )
        {
            AiPerceivedTarget* pTarget = nullptr;
            for ( AiPerceivedTarget& target : _listTarget )
            {
                if ( target._id == stimulus._id )
                    pTarget = &target;
            }
            const bool bAlreadySeen = pTarget != nullptr && pTarget->_bSeen != SW_FALSE;
            const bool bSeen        = canSee( eyePosition, forward, stimulus._position, pGrid, bAlreadySeen );
            const bool bHeard       = stimulus._noiseRadius > 0.0f &&
                                float3::getDistance( eyePosition, stimulus._position ) <= stimulus._noiseRadius * _settings._hearingScale;
            if ( bSeen == false && bHeard == false )
            {
                if ( pTarget != nullptr )
                    pTarget->_bSeen = SW_FALSE;
                continue;
            }
            if ( pTarget == nullptr )
            {
                _listTarget.push_back( AiPerceivedTarget{} );
                pTarget      = &_listTarget.back();
                pTarget->_id = stimulus._id;
            }
            pTarget->_lastKnownPosition = stimulus._position;
            pTarget->_age               = 0.0f;
            pTarget->_bSeen             = bSeen ? SW_TRUE : SW_FALSE;
            pTarget->_bHeard            = bHeard ? SW_TRUE : SW_FALSE;
        }
        // 후보에 없는 대상(죽었거나 멀리 갔다)도 안 보이는 것이다. 오래되면 잊는다.
        for ( AiPerceivedTarget& target : _listTarget )
        {
            if ( target._age > 0.0f )
                target._bSeen = SW_FALSE;
        }
        _listTarget.erase( std::remove_if( _listTarget.begin(), _listTarget.end(),
                                           [this]( const AiPerceivedTarget& target )
        { return target._age > _settings._memoryDuration; } ),
                           _listTarget.end() );
    }

    const AiPerceivedTarget* AiPerception::findTarget( uint64 id ) const
    {
        for ( const AiPerceivedTarget& target : _listTarget )
        {
            if ( target._id == id )
                return &target;
        }
        return nullptr;
    }

    const AiPerceivedTarget* AiPerception::findNearestSeen( const float3& origin ) const
    {
        const AiPerceivedTarget* pNearest = nullptr;
        float32                  best     = MathUtil::kMaxFloat;
        for ( const AiPerceivedTarget& target : _listTarget )
        {
            const float32 distance = float3::getDistance( origin, target._lastKnownPosition );
            if ( target._bSeen != SW_FALSE && distance < best )
            {
                best     = distance;
                pNearest = &target;
            }
        }
        return pNearest;
    }
} // namespace sw
