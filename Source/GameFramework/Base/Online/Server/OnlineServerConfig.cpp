#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"

#include "Core/Container/StringUtil.h"

namespace sw
{
    namespace
    {
        struct OnlineServerConfigInternal
        {
            static constexpr string_view kJSONExtension   = ".json";
            static constexpr string_view kOnlineExtension = ".online.json";
        };
    } // namespace
} // namespace sw

namespace sw
{
    const string* OnlineServerKitEntry::findOption( string_view key ) const
    {
        for ( const OnlineServerOption& option : _listOption )
        {
            if ( option._key == key )
                return &option._value;
        }
        return nullptr;
    }

    void OnlineServerKitEntry::collectOptions( string_view key, vector<string>& outListValue ) const
    {
        for ( const OnlineServerOption& option : _listOption )
        {
            if ( option._key == key )
                outListValue.push_back( option._value );
        }
    }

    const string* OnlineServerKitEntry::findUnknownOption( std::initializer_list<string_view> listKnownKey ) const
    {
        for ( const OnlineServerOption& option : _listOption )
        {
            bool bKnown = false;
            for ( const string_view knownKey : listKnownKey )
            {
                bKnown = bKnown || option._key == knownKey;
            }
            if ( bKnown == false )
                return &option._key;
        }
        return nullptr;
    }

    bool OnlineServerKitEntry::readIntOption( string_view key, int32& inoutValue, string& outError ) const
    {
        const string* pValue = findOption( key );
        if ( pValue == nullptr )
            return true;
        if ( StringUtil::parseInt( *pValue, inoutValue ) )
            return true;
        outError = "option '" + string( key ) + "' of kit '" + _name + "' must be an integer (got '" + *pValue + "')";
        return false;
    }

    bool OnlineServerKitEntry::readBoolOption( string_view key, bool& inoutValue, string& outError ) const
    {
        const string* pValue = findOption( key );
        if ( pValue == nullptr )
            return true;
        if ( *pValue == "1" || *pValue == "true" )
        {
            inoutValue = true;
            return true;
        }
        if ( *pValue == "0" || *pValue == "false" )
        {
            inoutValue = false;
            return true;
        }
        outError = "option '" + string( key ) + "' of kit '" + _name + "' must be 0 or 1 (got '" + *pValue + "')";
        return false;
    }

    string OnlineServerConfig::makeConfigPath( string_view serverConfigPath )
    {
        string_view stem = serverConfigPath;
        if ( StringUtil::endsWith( stem, OnlineServerConfigInternal::kJSONExtension ) )
            stem = stem.substr( 0, stem.size() - OnlineServerConfigInternal::kJSONExtension.size() );
        string path{ stem };
        path += OnlineServerConfigInternal::kOnlineExtension;
        return path;
    }
} // namespace sw
