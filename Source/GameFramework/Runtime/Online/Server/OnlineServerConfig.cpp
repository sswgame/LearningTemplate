#include "pch.h"

#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"

#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/Constants.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/ConfigManager.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceTTLRule.h"

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
    int64 ServiceTTLConfig::getExpireAfterMs() const
    {
        return static_cast<int64>( _expireAfterDays ) * constant::time::kSecondsPerDay * constant::time::kMillisecondsPerSecond;
    }

    void ServiceTTLConfig::applySweepLimits( ServiceTTLRule& inoutRule ) const
    {
        inoutRule._maxScanPerSweep   = _maxScanPerSweep;
        inoutRule._maxDeletePerSweep = _maxDeletePerSweep;
    }

    expected<void> OnlineServerKitEntry::readSettingsInto( void* pSettings, const TypeInfo& typeInfo ) const
    {
        const string label = "kit '" + _name + "' settings";
        if ( ConfigManager::readConfigJSON( pSettings, typeInfo, _settings.empty() ? string_view( "{}" ) : string_view( _settings ), label.c_str() ) )
            return {};
        return unexpected( Error{ ErrorKind::InvalidArgument, "the kit settings are invalid (see the errors above)", _name } );
    }

    expected<optional<AbsolutePath>> OnlineServerKitEntry::resolveFile( const string& text, const utf8* pFieldName ) const
    {
        if ( text.empty() )
            return optional<AbsolutePath>{};
        optional<AbsolutePath> path = AbsolutePath::tryResolve( text, ProcessRoots::get()._project );
        if ( path.has_value() )
            return path;
        Error error{ ErrorKind::InvalidArgument, "a kit setting must be a file path (absolute or relative to the project root)", pFieldName };
        error.appendDetail( " = '" ).appendDetail( text ).appendDetail( "'" );
        return unexpected( error );
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
