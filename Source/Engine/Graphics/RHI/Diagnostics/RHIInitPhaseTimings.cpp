#include "pch.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIInitPhaseTimings.h"

#include "Core/Foundation/Constants.h"
#include "Core/String/formatString.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    void RHIInitPhaseTimings::begin()
    {
        _phaseCount = 0;
        _startMicro = MonotonicClock::nowMicroseconds();
    }

    void RHIInitPhaseTimings::mark( const utf8* pPhaseName )
    {
        SW_ASSERT( _phaseCount < kMaxPhaseCount && "RHI 초기화 단계가 상한을 넘었다 - kMaxPhaseCount 를 올린다" );
        if ( _phaseCount >= kMaxPhaseCount )
            return;
        _arrPhaseName[_phaseCount]     = pPhaseName;
        _arrPhaseEndMicro[_phaseCount] = MonotonicClock::nowMicroseconds();
        ++_phaseCount;
    }

    string RHIInitPhaseTimings::describe() const
    {
        constexpr int64 kMicroPerMs = constant::time::kMicrosecondsPerMillisecond;
        const int64     endMicro    = MonotonicClock::nowMicroseconds();
        string          text;
        int64           previousMicro = _startMicro;
        utf8            arrPart[constant::buffer::kMax64]{};
        for ( uint32 index = 0; index < _phaseCount; ++index )
        {
            formatstring( arrPart, constant::buffer::kMax64, "%#%# %# ms", index == 0 ? "" : ", ", _arrPhaseName[index], ( _arrPhaseEndMicro[index] - previousMicro ) / kMicroPerMs );
            text += arrPart;
            previousMicro = _arrPhaseEndMicro[index];
        }
        formatstring( arrPart, constant::buffer::kMax64, "%#rest %# ms (total %# ms)", _phaseCount == 0 ? "" : ", ", ( endMicro - previousMicro ) / kMicroPerMs,
                      ( endMicro - _startMicro ) / kMicroPerMs );
        text += arrPart;
        return text;
    }
} // namespace sw
