#include "pch.h"

#include "GameFramework/Kits/Online/Server/Account/Platform/PlatformLoginProviderSettings.h"

#include "Engine/Utility/Json/JsonDocument.h"

#include "GameFramework/Kits/Online/Account/Protocol/AccountTypes.h"
#include "GameFramework/Kits/Online/Server/Account/Platform/OidcLoginProvider.h"
#include "GameFramework/Kits/Online/Server/Account/Platform/ProfileApiLoginProvider.h"

namespace sw
{
    namespace
    {
        struct PlatformLoginProviderSettingsInternal
        {
            static constexpr const utf8* kKnownKeys[] = { "name", "kind", "issuer", "jwksUrl", "clientIds", "requireNonce", "profileUrl", "subjectPath", "displayNamePath" };

            static bool isKnownKey( string_view key )
            {
                for ( const utf8* pKnown : kKnownKeys )
                {
                    if ( key == pKnown )
                        return true;
                }
                return false;
            }

            static string readText( const JsonValue& object, const utf8* pKey, string_view fallback )
            {
                const JsonValue value = object.get( pKey, false );
                return value.isString() ? value.asString() : string( fallback );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<IPlatformLoginProvider> PlatformLoginProviderFactory::create( const PlatformLoginProviderSettings& settings, INetSecurityProvider* pProvider,
                                                                             HttpClient* pHttpClient )
    {
        if ( isValidSettings( settings ) == false || pHttpClient == nullptr )
            return nullptr;
        if ( settings._kind == PlatformLoginProviderKind::Oidc )
        {
            if ( pProvider == nullptr )
                return nullptr;
            return make_unique<OidcLoginProvider>( settings, pProvider, pHttpClient );
        }
        return make_unique<ProfileApiLoginProvider>( settings, pHttpClient );
    }

    bool PlatformLoginProviderFactory::readSettings( string_view jsonText, vector<PlatformLoginProviderSettings>& outListSettings, string& outError )
    {
        using Internal = PlatformLoginProviderSettingsInternal;
        outListSettings.clear();
        JsonDocument document;
        if ( document.tryParse( jsonText ) == false )
        {
            outError = "platform login settings are not JSON";
            return false;
        }
        const JsonValue providers = document.getRoot().get( "providers", false );
        if ( providers.isArray() == false )
        {
            outError = "platform login settings need a 'providers' array";
            return false;
        }
        for ( size_t index = 0; index < providers.size(); ++index )
        {
            const JsonValue entry = providers.at( index );
            if ( entry.isObject() == false )
            {
                outError = "provider entry is not an object";
                return false;
            }
            for ( const string& key : entry.getMemberNames() )
            {
                if ( Internal::isKnownKey( key ) == false )
                {
                    outError = "unknown provider setting '" + key + "'";
                    return false;
                }
            }
            PlatformLoginProviderSettings& settings = outListSettings.emplace_back();
            settings._name                          = Internal::readText( entry, "name", "" );
            const string kind                       = Internal::readText( entry, "kind", "" );
            if ( kind == "oidc" )
                settings._kind = PlatformLoginProviderKind::Oidc;
            else if ( kind == "profile" )
                settings._kind = PlatformLoginProviderKind::AccessTokenProfile;
            else
            {
                outError = "provider '" + settings._name + "' has an unknown kind '" + kind + "'";
                return false;
            }
            settings._issuer          = Internal::readText( entry, "issuer", "" );
            settings._jwksUrl         = Internal::readText( entry, "jwksUrl", "" );
            settings._profileUrl      = Internal::readText( entry, "profileUrl", "" );
            settings._subjectPath     = Internal::readText( entry, "subjectPath", settings._kind == PlatformLoginProviderKind::Oidc ? "sub" : "id" );
            settings._displayNamePath = Internal::readText( entry, "displayNamePath", "" );
            settings._bRequireNonce   = entry.get( "requireNonce", false ).asBool( false ) ? SW_TRUE : SW_FALSE;
            const JsonValue clientIds = entry.get( "clientIds", false );
            for ( size_t clientIndex = 0; clientIds.isArray() && clientIndex < clientIds.size(); ++clientIndex )
            {
                settings._listClientId.push_back( clientIds.at( clientIndex ).asString() );
            }
            if ( isValidSettings( settings ) == false )
            {
                outError = "provider '" + settings._name + "' is missing a required setting or has an invalid name";
                return false;
            }
        }
        return true;
    }

    bool PlatformLoginProviderFactory::isValidSettings( const PlatformLoginProviderSettings& settings )
    {
        if ( AccountUtil::isValidLowerToken( settings._name, LoginConstant::kMaxProviderNameSize ) == false || settings._subjectPath.empty() )
            return false;
        if ( settings._kind == PlatformLoginProviderKind::Oidc )
            return settings._issuer.empty() == false && settings._jwksUrl.empty() == false && settings._listClientId.empty() == false;
        return settings._profileUrl.empty() == false;
    }
} // namespace sw
