#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderConfig.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/Protocol/AccountTypes.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/OidcLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/ProfileAPILoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/TicketQueryLoginProvider.h"

namespace sw
{
    namespace
    {
        struct PlatformLoginProviderSettingsInternal
        {
            static constexpr const utf8* kKnownKeys[] = { "name", "kind", "issuer", "jwksUrl",
                                                          "clientIds", "requireNonce", "profileUrl", "subjectPath",
                                                          "displayNamePath", "ticketParam", "keyParam", "keySecretEnvironment",
                                                          "resultPath", "resultValue", "teamId", "signingKeyId",
                                                          "tokenUrl", "revokeUrl" };

            static bool isKnownKey( string_view key ) { return VectorUtil::contains( kKnownKeys, key ); }

            static string readText( const JSONValue& object, const utf8* pKey, string_view fallback )
            {
                const JSONValue value = object.get( pKey );
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
        if ( settings._kind == PlatformLoginProviderKind::AppleSignIn )
            return nullptr; // HTTP 클라이언트가 둘 필요해 `PlatformLoginProviderSet` 이 `AppleLoginProvider::create` 로 만든다
        return make_unique<ProfileAPILoginProvider>( settings, pHTTPClient );
    }

    bool PlatformLoginProviderFactory::readEntry( const JSONValue& entry, PlatformLoginProviderConfig& outSettings, string& outError )
    {
        using Internal = PlatformLoginProviderSettingsInternal;
        for ( const string& key : entry.getMemberNames() )
        {
            if ( Internal::isKnownKey( key ) == false )
            {
                outError = "unknown provider setting '" + key + "'";
                return false;
            }
        }
        outSettings._name = Internal::readText( entry, "name", "" );
        const string kind = Internal::readText( entry, "kind", "" );
        if ( kind == "oidc" )
            outSettings._kind = PlatformLoginProviderKind::Oidc;
        else if ( kind == "profile" )
            outSettings._kind = PlatformLoginProviderKind::AccessTokenProfile;
        else if ( kind == "ticketQuery" )
            outSettings._kind = PlatformLoginProviderKind::TicketQuery;
        else if ( kind == "appleSignIn" )
            outSettings._kind = PlatformLoginProviderKind::AppleSignIn;
        else
        {
            outError = "provider '" + outSettings._name + "' has an unknown kind '" + kind + "'";
            return false;
        }
        outSettings._issuer               = Internal::readText( entry, "issuer", "" );
        outSettings._jwksURL              = Internal::readText( entry, "jwksUrl", "" );
        outSettings._profileURL           = Internal::readText( entry, "profileUrl", "" );
        outSettings._subjectPath          = Internal::readText( entry, "subjectPath", ( outSettings._kind == PlatformLoginProviderKind::Oidc || outSettings._kind == PlatformLoginProviderKind::AppleSignIn ) ? "sub" : "id" );
        outSettings._displayNamePath      = Internal::readText( entry, "displayNamePath", "" );
        outSettings._ticketParam          = Internal::readText( entry, "ticketParam", "" );
        outSettings._keyParam             = Internal::readText( entry, "keyParam", "" );
        outSettings._keySecretEnvironment = Internal::readText( entry, "keySecretEnvironment", "" );
        outSettings._resultPath           = Internal::readText( entry, "resultPath", "" );
        outSettings._resultValue          = Internal::readText( entry, "resultValue", "" );
        outSettings._teamID               = Internal::readText( entry, "teamId", "" );
        outSettings._signingKeyID         = Internal::readText( entry, "signingKeyId", "" );
        outSettings._tokenURL             = Internal::readText( entry, "tokenUrl", "" );
        outSettings._revokeURL            = Internal::readText( entry, "revokeUrl", "" );
        bool bRequireNonce                = outSettings._bRequireNonce != SW_FALSE; // 없으면 설정의 초기값이다
        if ( entry.readOptionalInto( "requireNonce", bRequireNonce ) == false )
        {
            outError = "provider '" + outSettings._name + "' has a requireNonce that is not true or false";
            return false;
        }
        outSettings._bRequireNonce = bRequireNonce ? SW_TRUE : SW_FALSE;
        const JSONValue clientIDs  = entry.get( "clientIds" );
        for ( size_t clientIndex = 0; clientIDs.isArray() && clientIndex < clientIDs.size(); ++clientIndex )
        {
            outSettings._listClientID.push_back( clientIDs.at( clientIndex ).asString() );
        }
        if ( isValidSettings( outSettings ) == false )
        {
            outError = "provider '" + outSettings._name + "' is missing a required setting or has an invalid name";
            return false;
        }
        return true;
    }

    bool PlatformLoginProviderFactory::isValidSettings( const PlatformLoginProviderConfig& settings )
    {
        if ( AccountUtil::isValidLowerToken( settings._name, LoginConstant::kMaxProviderNameSize ) == false || settings._subjectPath.empty() )
            return false;
        if ( settings._kind == PlatformLoginProviderKind::Oidc )
            return settings._issuer.empty() == false && settings._jwksURL.empty() == false && settings._listClientID.empty() == false;
        if ( settings._kind == PlatformLoginProviderKind::AppleSignIn )
        {
            const bool bOidcOk = settings._issuer.empty() == false && settings._jwksURL.empty() == false && settings._listClientID.empty() == false;
            const bool bKeyOk  = settings._teamID.empty() == false && settings._signingKeyID.empty() == false && settings._keySecretEnvironment.empty() == false;
            return bOidcOk && bKeyOk;
        }
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
