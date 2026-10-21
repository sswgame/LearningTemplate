#include "pch.h"

#include "GameFramework/Base/World/Encounter/StepEncounter.h"

#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"

namespace sw
{
    void StepEncounterCounter::advance()
    {
        ++_totalSteps;
        if ( _stepsSinceEncounter < kNoEncounterYet )
            ++_stepsSinceEncounter;
    }

    bool StepEncounterCounter::roll( const StepEncounterRule& rule, GameRandom* pRandom ) const
    {
        if ( _stepsSinceEncounter <= rule._graceSteps )
            return false;
        switch ( rule._mode )
        {
            case StepEncounterMode::Period:
            {
                return isPeriodicEncounter( rule._rate, static_cast<uint32>( _totalSteps ) );
            }
            case StepEncounterMode::Chance:
            {
                SW_ASSERT( pRandom != nullptr );
                // 걸음마다 난수를 하나 쓴다(유예 중에는 쓰지 않는다). 끄는 값(0 이하)도 난수를 쓴다 — 예전 JRPG 판정과 같은 바이트를 낸다.
                return pRandom != nullptr && pRandom->nextChance( rule._rate );
            }
        }
        return false;
    }

    void StepEncounterCounter::reset()
    {
        _stepsSinceEncounter = kNoEncounterYet;
        _totalSteps          = 0;
    }

    bool StepEncounterCounter::restore( int32 stepsSinceEncounter, int32 totalSteps )
    {
        const bool bValid = 0 <= stepsSinceEncounter && stepsSinceEncounter <= kNoEncounterYet && 0 <= totalSteps;
        if ( bValid == false )
            return false;
        _stepsSinceEncounter = stepsSinceEncounter;
        _totalSteps          = totalSteps;
        return true;
    }

    bool StepEncounterCounter::isPeriodicEncounter( float32 rate, uint32 stepCount )
    {
        // 0이면 **조우가 나지 않는다**. 야생 조우를 끄려고 rate 0 을 넣는 경우가 이에 해당한다.
        if ( rate <= 0.0f )
            return false;
        // 1 이상이면 매 걸음 조우한다. 1/rate 를 uint32 로 자르면 그 구간이 통째로 0이 되어
        // "주기 0" 이 된다. 이것을 다른 주기로 바꾸면 **높은 확률이 낮은 빈도**가 되어 버린다.
        if ( rate >= 1.0f )
            return true;

        const uint32 period = static_cast<uint32>( 1.0f / rate );
        return period <= 1u || ( stepCount % period ) == 0u;
    }
} // namespace sw
