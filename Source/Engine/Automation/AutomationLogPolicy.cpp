#include "pch.h"

#include "Engine/Automation/AutomationLogPolicy.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    bool AutomationLogPolicy::loadAllowList( string& outError )
    {
        JSONDocument document;
        string       absolutePath;
        if ( document.loadResource( kAllowListPath, &absolutePath ) == false )
        {
            outError = string( kAllowListPath ) + " cannot be read";
            return false;
        }
        return readAllowList( document.getRoot(), kAllowListPath, outError );
    }

    bool AutomationLogPolicy::parseAllowList( string_view jsonText, string_view sourceName, string& outError )
    {
        JSONDocument document;
        if ( document.parse( FileUtil::skipUtf8Bom( jsonText ), sourceName ) == false )
        {
            outError = string( sourceName ) + ": not valid JSON";
            return false;
        }
        return readAllowList( document.getRoot(), sourceName, outError );
    }

    bool AutomationLogPolicy::readAllowList( const JSONValue& root, string_view sourceName, string& outError )
    {
        const JSONValue listAllowed = root.get( "allowed", false );
        if ( listAllowed.isArray() == false )
        {
            outError = string( sourceName ) + ": needs an \"allowed\" array";
            return false;
        }
        for ( size_t index = 0; index < listAllowed.size(); ++index )
        {
            const JSONValue entry    = listAllowed.at( index );
            const string    contains = entry.get( "contains", false ).asString();
            const string    reason   = entry.get( "reason", false ).asString();
            const bool      bValid   = entry.isObject() && contains.empty() == false && reason.empty() == false;
            if ( bValid == false )
            {
                outError = string( sourceName ) + ": entry " + to_string( index ) + " needs non-empty \"contains\" and \"reason\"";
                return false;
            }
            addAllowed( contains, reason );
        }
        return true;
    }

    void AutomationLogPolicy::addAllowed( string_view contains, string_view reason )
    {
        if ( contains.empty() )
            return;
        _listAllowed.push_back( AutomationAllowedLog{ string( contains ), string( reason ) } );
    }

    bool AutomationLogPolicy::isExemptMessage( string_view message )
    {
        return StringUtil::startsWith( message, kRunnerPrefix ) || StringUtil::startsWith( message, kDefensivePrefix );
    }

    bool AutomationLogPolicy::isAllowed( string_view text ) const
    {
        return VectorUtil::containsIf( _listAllowed, [&]( const AutomationAllowedLog& allowed )
        { return text.find( string_view( allowed._contains ) ) != string_view::npos; } );
    }
} // namespace sw
