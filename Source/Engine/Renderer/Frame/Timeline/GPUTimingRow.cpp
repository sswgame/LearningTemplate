#include "pch.h"

#include "Engine/Renderer/Frame/Timeline/GPUTimingRow.h"

#include "Core/Container/CompareUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Foundation/Constants.h"

#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    vector<GPUTimingRow> GPUTimingRow::collect( const FrameProfiler& profiler )
    {
        vector<GPUTimingRow> listRow;
        const string_view    prefix     = kScopePrefix;
        const uint32         scopeCount = profiler.getScopeCount();
        for ( uint32 slot = 0; slot < scopeCount; ++slot )
        {
            const utf8* pName = profiler.findScopeName( slot );
            if ( pName == nullptr || profiler.isCounterScope( slot ) || StringUtil::startsWith( pName, prefix ) == false )
                continue;
            GPUTimingRow& row = listRow.emplace_back();
            row._name         = string( string_view( pName ).substr( prefix.size() ) );
            row._milliseconds = static_cast<float64>( profiler.getLastFrameNanos( slot ) ) / static_cast<float64>( constant::time::kNanosecondsPerMillisecond );
        }
        VectorUtil::sort( listRow, Descending<&GPUTimingRow::_milliseconds>{} );
        return listRow;
    }

    bool GPUTimingRow::findMilliseconds( const FrameProfiler& profiler, string_view passName, float64& outMilliseconds )
    {
        const string_view prefix     = kScopePrefix;
        const uint32      scopeCount = profiler.getScopeCount();
        for ( uint32 slot = 0; slot < scopeCount; ++slot )
        {
            const utf8* pName = profiler.findScopeName( slot );
            if ( pName == nullptr || profiler.isCounterScope( slot ) )
                continue;
            const string_view name = pName;
            if ( name.size() != prefix.size() + passName.size() || StringUtil::startsWith( name, prefix ) == false || name.substr( prefix.size() ) != passName )
                continue;
            outMilliseconds = static_cast<float64>( profiler.getLastFrameNanos( slot ) ) / static_cast<float64>( constant::time::kNanosecondsPerMillisecond );
            return true;
        }
        return false;
    }
} // namespace sw
