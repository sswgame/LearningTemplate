#include "pch.h"

#include "Engine/Automation/Scenario/AutomationFrameStats.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Serialization/JSON/JSONWriter.h"

#include <algorithm>
#include <cmath>

namespace sw
{
    void AutomationFrameStats::begin()
    {
        _listFrameMilliseconds.clear();
        _lastFrameEndNanoseconds = MonotonicClock::nowNanoseconds();
        _bRecording              = true;
    }

    bool AutomationFrameStats::end( FrameStatsSummary& outSummary )
    {
        if ( _bRecording == false )
            return false;
        _bRecording = false;
        outSummary  = summarize( std::move( _listFrameMilliseconds ) );
        _listFrameMilliseconds.clear();
        return true;
    }

    void AutomationFrameStats::onFrameEnd()
    {
        if ( _bRecording == false )
            return;
        const int64 now = MonotonicClock::nowNanoseconds();
        _listFrameMilliseconds.push_back( static_cast<float64>( now - _lastFrameEndNanoseconds ) / static_cast<float64>( constant::kNanosecondsPerMillisecond ) );
        _lastFrameEndNanoseconds = now;
    }

    FrameStatsSummary AutomationFrameStats::summarize( vector<float64> listMilliseconds )
    {
        FrameStatsSummary summary;
        if ( listMilliseconds.empty() )
            return summary;
        std::sort( listMilliseconds.begin(), listMilliseconds.end() );
        const size_t count = listMilliseconds.size();
        float64      total = 0.0;
        for ( const float64 milliseconds : listMilliseconds )
        {
            total += milliseconds;
        }
        const auto percentile = [&listMilliseconds, count]( float64 fraction )
        {
            const size_t rank = static_cast<size_t>( std::ceil( fraction * static_cast<float64>( count ) ) );
            return listMilliseconds[MathUtil::clamp<size_t>( rank, 1, count ) - 1];
        };
        summary._frameCount             = static_cast<uint32>( count );
        summary._averageMilliseconds    = total / static_cast<float64>( count );
        summary._minMilliseconds        = listMilliseconds.front();
        summary._maxMilliseconds        = listMilliseconds.back();
        summary._p50Milliseconds        = percentile( 0.50 );
        summary._p95Milliseconds        = percentile( 0.95 );
        summary._p99Milliseconds        = percentile( 0.99 );
        summary._averageFramesPerSecond = summary._averageMilliseconds > 0.0 ? 1000.0 / summary._averageMilliseconds : 0.0;
        return summary;
    }

    string AutomationFrameStats::toJSON( const FrameStatsSummary& summary, string_view scenarioName )
    {
        string     json;
        JSONWriter writer{ json, 2 };
        writer.beginObject();
        writer.key( "file" );
        writer.valueString( scenarioName );
        writer.key( "frames" );
        writer.valueUint( summary._frameCount );
        writer.key( "averageMs" );
        writer.valueFloat( summary._averageMilliseconds );
        writer.key( "minMs" );
        writer.valueFloat( summary._minMilliseconds );
        writer.key( "maxMs" );
        writer.valueFloat( summary._maxMilliseconds );
        writer.key( "p50Ms" );
        writer.valueFloat( summary._p50Milliseconds );
        writer.key( "p95Ms" );
        writer.valueFloat( summary._p95Milliseconds );
        writer.key( "p99Ms" );
        writer.valueFloat( summary._p99Milliseconds );
        writer.key( "averageFps" );
        writer.valueFloat( summary._averageFramesPerSecond );
        writer.endObject();
        return json;
    }
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "AutomationFrameStats" );

    namespace
    {
        struct AutomationFrameStatsSteps
        {
            static bool validateBegin( const AutomationStep& step, string& outError ) { return step.hasRequiredAttributes( {}, {}, outError ); }
            static bool validateEnd( const AutomationStep& step, string& outError ) { return step.hasRequiredAttributes( { "file" }, { "file" }, outError ); }

            static bool runBegin( AutomationStepContext& context, const AutomationStep& step )
            {
                (void)step;
                context.getFrameStats().begin();
                return true;
            }

            static bool runEnd( AutomationStepContext& context, const AutomationStep& step )
            {
                FrameStatsSummary summary;
                if ( context.getFrameStats().end( summary ) == false )
                {
                    context.recordFailure( step, "no <BeginFrameStats/> before this step" );
                    return true;
                }
                const string path = context.resolveOutputPath( *step.findAttribute( "file" ) );
                if ( FileUtil::ensureParentDirectoryExists( path ) == false || FileUtil::writeTextFile( path, AutomationFrameStats::toJSON( summary, path ) ) == false )
                {
                    context.recordFailure( step, "could not write the frame stats to " + path );
                    return true;
                }
                SW_LOG_INFO( "[Scenario] frame stats: %# frames, avg %# ms (%# fps), p95 %# ms, p99 %# ms, max %# ms -> %#", summary._frameCount, summary._averageMilliseconds,
                             summary._averageFramesPerSecond, summary._p95Milliseconds, summary._p99Milliseconds, summary._maxMilliseconds, path.c_str() );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_STEP( frameStatsBegin, "BeginFrameStats", &AutomationFrameStatsSteps::runBegin, &AutomationFrameStatsSteps::validateBegin, false );
    SW_AUTOMATION_STEP( frameStatsEnd, "EndFrameStats", &AutomationFrameStatsSteps::runEnd, &AutomationFrameStatsSteps::validateEnd, false );
} // namespace sw
