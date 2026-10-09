#include "pch.h"

#include "Engine/Utility/DebugOverlayState.h"

#include "Core/Common/Defines.h"
#include "Core/Container/formatString.h"
#include "Core/String/fixed_string.h"

namespace sw
{
    void DebugOverlayState::setFloat( hashed_string key, float32 value )
    {
        _mapFloat[key] = value;
    }

    float32 DebugOverlayState::getFloat( hashed_string key, float32 defaultValue ) const
    {
        const auto it = _mapFloat.find( key );
        if ( it == _mapFloat.end() )
            return defaultValue;
        return it->second;
    }

    void DebugOverlayState::setString( hashed_string key, string_view value )
    {
        _mapString[key] = string( value );
    }

    string DebugOverlayState::getString( hashed_string key ) const
    {
        const auto it = _mapString.find( key );
        if ( it == _mapString.end() )
            return {};
        return it->second;
    }

    void DebugOverlayState::remove( hashed_string key )
    {
        _mapFloat.erase( key );
        _mapString.erase( key );
    }

    void DebugOverlayState::clear()
    {
        _mapFloat.clear();
        _mapString.clear();
    }

    void DebugOverlayState::collectRows( vector<DebugOverlayRow>& outListRow ) const
    {
        outListRow.clear();
        outListRow.reserve( _mapFloat.size() + _mapString.size() );
        for ( const auto& [key, value] : _mapFloat )
        {
            fixed_string<constant::kMaxBuffer32> text;
            formatstring( text.data(), text.capacity(), "%.2f", value );
            outListRow.push_back( DebugOverlayRow{ string( key.c_str() ), string( text.c_str() ) } );
        }
        for ( const auto& [key, value] : _mapString )
        {
            if ( value.empty() == false )
                outListRow.push_back( DebugOverlayRow{ string( key.c_str() ), value } );
        }
        std::sort( outListRow.begin(), outListRow.end(), []( const DebugOverlayRow& left, const DebugOverlayRow& right )
        { return left._key < right._key; } );
    }
} // namespace sw
