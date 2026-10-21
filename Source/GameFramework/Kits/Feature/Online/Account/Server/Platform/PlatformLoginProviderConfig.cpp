#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderConfig.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/OidcLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/ProfileAPILoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/TicketQueryLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/Protocol/AccountTypes.h"

namespace sw
{
    namespace
    {
        struct PlatformLoginProviderSettingsInternal
        {
            static constexpr const utf8* kKnownKeys[] = { "name", "kind", "issuer", "jwksUrl",
                                                          "clientIds", "requireNonce", "profileUrl", "subjectPath",
                                                          "displayNamePath", "ticketParam", "keyParam", "keySecretEnvironment",
                                                          "resultPath", "resultValue" };

            static bool isKnownKey( string_view key ) { return VectorUtil::contains( kKnownKeys, key ); }

            static string readText( const JSONValue& object, const utf8* pKey, string_view fallback )
            {
                const JSONValue value = object.get( pKey, false );
                return value.isString() ? value.asString() : string( fallback );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<IPlatformLoginProvider> PlatformLoginProviderFactory::create( const PlatformLoginProviderConfig& settings, INetSecurityProvider* pProvider,
                                                                             HTTPClient* pHTTPClient )
    {
        if ( isValidSettings( settings ) == false || pHTTPClient == nullptr )
            return nullptr;
        if ( settings._kind == PlatformLoginProviderKind::Oidc )
        {
            if ( pProvider == nullptr )
                return nullptr;
            return make_unique<OidcLoginProvider>( settings, pProvider, pHTTPClient );
        }
        if ( settings._kind == PlatformLoginProviderKind::TicketQuery )
            return TicketQueryLoginProvider::create( settings, pHTTPClient ); // 비밀 키를 읽지 못하면 nullptr(그 함수가 알린다)
        return make_unique<ProfileAPILoginProvider>( settings, pHTTPClient );
    }

    bool PlatformLoginProviderFactory::readSettings( string_view jsonText, vector<PlatformLoginProviderConfig>& outListSettings, string& outError )
    {
        using Internal = PlatformLoginProviderSettingsInternal;
        outListSettings.clear();
        JSONDocument document;
        if ( document.tryParse( jsonText ) == false )
        {
            outError = "platform login settings are not JSON";
            return false;
        }
        const JSONValue providers = document.getRoot().get( "providers", false );
        if ( providers.isArray() == false )
        {
            outError = "platform login settings need a 'providers' array";
            return false;
        }
        for ( size_t index = 0; index < providers.size(); ++index )
        {
            const JSONValue entry = providers.at( index );
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
            PlatformLoginProviderConfig& settings = outListSettings.emplace_back();
            settings._name                        = Internal::readText( entry, "name", "" );
            const string kind                     = Internal::readText( entry, "kind", "" );
            if ( kind == "oidc" )
                settings._kind = PlatformLoginProviderKind::Oidc;
            else if ( kind == "profile" )
                settings._kind = PlatformLoginProviderKind::AccessTokenProfile;
            else if ( kind == "ticketQuery" )
                settings._kind = PlatformLoginProviderKind::TicketQuery;
            else
            {
                outError = "provider '" + settings._name + "' has an unknown kind '" + kind + "'";
                return false;
            }
            settings._issuer               = Internal::readText( entry, "issuer", "" );
            settings._jwksURL              = Internal::readText( entry, "jwksUrl", "" );
            settings._profileURL           = Internal::readText( entry, "profileUrl", "" );
            settings._subjectPath          = Internal::readText( entry, "subjectPath", settings._kind == PlatformLoginProviderKind::Oidc ? "sub" : "id" );
            settings._displayNamePath      = Internal::readText( entry, "displayNamePath", "" );
            settings._ticketParam          = Internal::readText( entry, "ticketParam", "" );
            settings._keyParam             = Internal::readText( entry, "keyParam", "" );
            settings._keySecretEnvironment = Internal::readText( entry, "keySecretEnvironment", "" );
            settings._resultPath           = Internal::readText( entry, "resultPath", "" );
            settings._resultValue          = Internal::readText( entry, "resultValue", "" );
            bool bRequireNonce             = settings._bRequireNonce != SW_FALSE; // 없으면 설정의 초기값이다
            if ( entry.readOptionalInto( "requireNonce", bRequireNonce ) == false )
            {
                outError = "provider '" + settings._name + "' has a requireNonce that is not true or false";
                return false;
            }
            settings._bRequireNonce   = bRequireNonce ? SW_TRUE : SW_FALSE;
            const JSONValue clientIDs = entry.get( "clientIds", false );
            for ( size_t clientIndex = 0; clientIDs.isArray() && clientIndex < clientIDs.size(); ++clientIndex )
            {
                settings._listClientID.push_back( clientIDs.at( clientIndex ).asString() );
            }
            if ( isValidSettings( settings ) == false )
            {
                outError = "provider '" + settings._name + "' is missing a required setting or has an invalid name";
                return false;
            }
        }
        return true;
    }

    bool PlatformLoginProviderFactory::isValidSettings( const PlatformLoginProviderConfig& settings )
    {
        if ( AccountUtil::isValidLowerToken( settings._name, LoginConstant::kMaxProviderNameSize ) == false || settings._subjectPath.empty() )
            return false;
        if ( settings._kind == PlatformLoginProviderKind::Oidc )
            return settings._issuer.empty() == false && settings._jwksURL.empty() == false && settings._listClientID.empty() == false;
        if ( settings._kind == PlatformLoginProviderKind::TicketQuery )
        {
            const bool bHasQuery  = settings._profileURL.empty() == false && settings._ticketParam.empty() == false;
            const bool bHasKey    = settings._keyParam.empty() == false && settings._keySecretEnvironment.empty() == false;
            const bool bHasResult = settings._resultPath.empty() == false && settings._resultValue.empty() == false;
            return bHasQuery && bHasKey && bHasResult;
        }
        return settings._profileURL.empty() == false;
    }
} // namespace sw
