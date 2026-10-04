#include "pch.h"

#include "Engine/Telemetry/TelemetryEvent.h"

namespace sw
{
    TelemetryEvent::TelemetryEvent( const hashed_string& eventId )
        : _listValue{}
        , _id{ eventId }
    {
    }

    TelemetryValue& TelemetryEvent::acquireValue( const hashed_string& name, TelemetryFieldType type )
    {
        for ( TelemetryValue& value : _listValue )
        {
            if ( value._name == name )
            {
                value       = TelemetryValue{};
                value._name = name;
                value._type = type;
                return value;
            }
        }
        TelemetryValue value;
        value._name = name;
        value._type = type;
        _listValue.push_back( value );
        return _listValue.back();
    }

    TelemetryEvent& TelemetryEvent::setBool( const hashed_string& name, bool bValue )
    {
        acquireValue( name, TelemetryFieldType::Bool )._bValue = bValue ? SW_TRUE : SW_FALSE;
        return *this;
    }

    TelemetryEvent& TelemetryEvent::setInt( const hashed_string& name, int64 value )
    {
        acquireValue( name, TelemetryFieldType::Int )._integer = value;
        return *this;
    }

    TelemetryEvent& TelemetryEvent::setFloat( const hashed_string& name, float64 value )
    {
        acquireValue( name, TelemetryFieldType::Float )._number = value;
        return *this;
    }

    TelemetryEvent& TelemetryEvent::setString( const hashed_string& name, string_view value )
    {
        acquireValue( name, TelemetryFieldType::String )._text.assign( value.data(), value.size() );
        return *this;
    }

    const TelemetryValue* TelemetryEvent::findValue( const hashed_string& name ) const
    {
        for ( const TelemetryValue& value : _listValue )
        {
            if ( value._name == name )
                return &value;
        }
        return nullptr;
    }
} // namespace sw
