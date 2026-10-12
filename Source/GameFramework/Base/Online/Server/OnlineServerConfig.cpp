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
