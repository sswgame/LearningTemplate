#include "pch.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"

#include "Core/File/ProcessRoots.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/ConfigManager.h"

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
    bool OnlineServerKitEntry::readSettingsInto( void* pSettings, const TypeInfo& typeInfo, string& outError ) const
    {
        const string label = "kit '" + _name + "' settings";
        if ( ConfigManager::readConfigJSON( pSettings, typeInfo, _settings.empty() ? string_view( "{}" ) : string_view( _settings ), label.c_str() ) )
            return true;
        outError = "the settings of kit '" + _name + "' are invalid (see the errors above)";
        return false;
    }

    bool OnlineServerKitEntry::resolveFile( const string& text, const utf8* pFieldName, std::optional<AbsolutePath>& outPath, string& outError ) const
    {
        outPath.reset();
        if ( text.empty() )
            return true;
        outPath = AbsolutePath::tryResolve( text, ProcessRoots::get()._project );
        if ( outPath.has_value() )
            return true;
        outError = "setting '" + string( pFieldName ) + "' of kit '" + _name + "' must be a file path (absolute or relative to the project root; got '" + text + "')";
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
