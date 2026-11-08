#include "pch.h"

#include "Core/Diagnostics/SubsystemTimingReport.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/Constants.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    void SubsystemTimingReport::enableTiming( SubsystemCollection& collection )
    {
        if constexpr ( build::kShipping == false )
            collection.setTimingClock( &MonotonicClock::nowNanoseconds );
        else
            (void)collection;
    }

    string SubsystemTimingReport::describe( vector<SubsystemCollection::SubsystemTiming> listTiming )
    {
        VectorUtil::sort( listTiming, []( const SubsystemCollection::SubsystemTiming& lhs, const SubsystemCollection::SubsystemTiming& rhs )
        { return lhs._micros > rhs._micros; } );
        StringBuilder text;
        for ( const SubsystemCollection::SubsystemTiming& timing : listTiming )
        {
            const int64 millis = timing._micros / constant::time::kMicrosecondsPerMillisecond;
            if ( millis < 1 )
                break; // 오래 걸린 순이라 뒤는 모두 1 ms 미만이다
            if ( text.size() > 0 )
                text.append( ", " );
            text.append( timing._pName ).append( '=' ).append( millis );
        }
        return string{ text.view() };
    }
} // namespace sw
