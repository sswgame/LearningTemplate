#include "pch.h"

#include "Core/Diagnostics/PhaseTimings.h"

#include "Core/Foundation/Constants.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    void PhaseTimings::begin()
    {
        _phaseCount    = 0;
        _startMicro    = MonotonicClock::nowMicroseconds();
        _lastMarkMicro = _startMicro;
    }

    void PhaseTimings::mark( const utf8* pPhaseName )
    {
        const int64 nowMicro = MonotonicClock::nowMicroseconds();
        add( pPhaseName, nowMicro - _lastMarkMicro );
        _lastMarkMicro = nowMicro;
    }

    void PhaseTimings::add( const utf8* pPhaseName, int64 microseconds )
    {
        for ( uint32 index = 0; index < _phaseCount; ++index )
        {
            if ( StringUtil::equals( _arrPhaseName[index], pPhaseName ) )
            {
                _arrPhaseMicro[index] += microseconds;
                return;
            }
        }
        SW_ASSERT( _phaseCount < kMaxPhaseCount && "단계 수가 상한을 넘었다 - kMaxPhaseCount 를 올린다" );
        if ( _phaseCount >= kMaxPhaseCount )
            return;
        _arrPhaseName[_phaseCount]  = pPhaseName;
        _arrPhaseMicro[_phaseCount] = microseconds;
        ++_phaseCount;
    }

    int64 PhaseTimings::findMicroseconds( const utf8* pPhaseName ) const
    {
        for ( uint32 index = 0; index < _phaseCount; ++index )
        {
            if ( StringUtil::equals( _arrPhaseName[index], pPhaseName ) )
                return _arrPhaseMicro[index];
        }
        return 0;
    }

    string PhaseTimings::describe() const
    {
        constexpr int64 kMicroPerMs = constant::time::kMicrosecondsPerMillisecond;
        const int64     endMicro    = MonotonicClock::nowMicroseconds();
        string          text;
        utf8            arrPart[constant::buffer::kMax64]{};
        for ( uint32 index = 0; index < _phaseCount; ++index )
        {
            formatstring( arrPart, constant::buffer::kMax64, "%#%# %# ms", index == 0 ? "" : ", ", _arrPhaseName[index], _arrPhaseMicro[index] / kMicroPerMs );
            text += arrPart;
        }
        formatstring( arrPart, constant::buffer::kMax64, "%#rest %# ms (total %# ms)", _phaseCount == 0 ? "" : ", ", ( endMicro - _lastMarkMicro ) / kMicroPerMs,
                      ( endMicro - _startMicro ) / kMicroPerMs );
        text += arrPart;
        return text;
    }

    ScopedPhaseTiming::ScopedPhaseTiming( PhaseTimings& table, const utf8* pPhaseName )
        : _table{ table }
        , _pPhaseName{ pPhaseName }
        , _startMicro{ MonotonicClock::nowMicroseconds() }
    {
    }

    ScopedPhaseTiming::~ScopedPhaseTiming()
    {
        _table.add( _pPhaseName, MonotonicClock::nowMicroseconds() - _startMicro );
    }
} // namespace sw
