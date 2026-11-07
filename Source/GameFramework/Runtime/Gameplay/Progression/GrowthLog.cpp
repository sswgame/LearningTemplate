#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Progression/GrowthLog.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Reflection/EnumName.h"

namespace sw
{
    GrowthLog::GrowthLog()
        : _arrEntry{}
        , _recordedCount{ 0 }
        , _head{ 0 }
        , _count{ 0 }
    {
    }

    void GrowthLog::record( GrowthEventKind kind, string_view subject, string_view what, int64 value, string_view source )
    {
        if constexpr ( build::kShipping == false )
        {
            GrowthEvent& entry = _arrEntry[_head];
            entry._subject     = subject.substr( 0, entry._subject.capacity() - 1 );
            entry._what        = what.substr( 0, entry._what.capacity() - 1 );
            entry._source      = source.substr( 0, entry._source.capacity() - 1 );
            entry._value       = value;
            entry._kind        = kind;
            _head              = ( _head + 1 ) % kCapacity;
            _count             = _count < kCapacity ? _count + 1 : kCapacity;
            ++_recordedCount;
        }
        else
        {
            (void)kind;
            (void)subject;
            (void)what;
            (void)value;
            (void)source;
        }
    }

    void GrowthLog::clear()
    {
        _head  = 0;
        _count = 0;
    }

    const GrowthEvent& GrowthLog::getEntry( uint32 index ) const
    {
        if ( index >= _count )
            SW_FATAL( "Growth log entry %# is outside %# entries", index, _count );
        const uint32 oldest = ( _head + kCapacity - _count ) % kCapacity;
        return _arrEntry[( oldest + index ) % kCapacity];
    }

    uint32 GrowthLog::countKind( GrowthEventKind kind ) const
    {
        uint32 kindCount = 0;
        for ( uint32 index = 0; index < _count; ++index )
        {
            kindCount += getEntry( index )._kind == kind ? 1u : 0u;
        }
        return kindCount;
    }

    void GrowthLog::writeDebugLines( DebugHUDSectionWriter& writer, uint32 maxLines ) const
    {
        uint32 written = 0;
        for ( uint32 back = 0; back < _count && written < maxLines; ++back, ++written )
        {
            const GrowthEvent& entry = getEntry( _count - 1 - back );
            string             value = string( getEnumName( entry._kind ) ) + " " + entry._what.c_str() + " " + to_string( entry._value );
            value += entry._source.empty() ? string( " (no source)" ) : string( " from " ) + entry._source.c_str();
            writer.addLine( string( entry._subject.c_str() ), value );
        }
        if ( _count == 0 )
            writer.addLine( "growth", "no change recorded yet" );
    }
} // namespace sw
