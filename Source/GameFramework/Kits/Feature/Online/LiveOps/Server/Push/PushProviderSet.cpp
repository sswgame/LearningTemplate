#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushProviderSet.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/APNS/APNSPushProvider.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/FCM/FCMPushProvider.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/HTTP/OwnedHTTP2Client.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/HTTP/OwnedHTTPClient.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"

namespace sw
{
    namespace
    {
        struct PushProviderSetInternal
        {
            static constexpr const utf8* kKindFCM  = "fcm";
            static constexpr const utf8* kKindAPNS = "apns";

            /** @brief 종류마다 쓸 수 있는 키입니다 — 다른 종류의 키는 철자 오류와 같이 거절한다. */
            static constexpr const utf8* kKeysFCM[]  = { "kind", "serviceAccountEnvironment", "tokenUrl", "sendUrl" };
            static constexpr const utf8* kKeysAPNS[] = { "kind", "keyEnvironment", "keyId", "teamId", "topic", "sendUrl" };

            static string readText( const JSONValue& object, const utf8* pKey )
            {
                const JSONValue value = object.get( pKey );
                return value.isString() ? value.asString() : string();
            }

            /** @brief 환경 변수의 서비스 계정으로 FCM 제공자를 만들고 HTTP 클라이언트의 TLS 를 준비합니다. */
            [[nodiscard]] static expected<unique_ptr<IPushNotificationProvider>> createFCM( const PushProviderConfig& config, string_view trustFile, OwnedHTTPClient& httpClient )
            {
                string     serviceAccountJSON;
                const bool bNamed      = config._serviceAccountEnvironment.empty() == false;
                const bool bSecretRead = bNamed && ServerSecret::read( config._serviceAccountEnvironment, serviceAccountJSON ) && serviceAccountJSON.empty() == false;
                if ( bSecretRead == false )
                    return unexpected( Error{ ErrorKind::NotFound, "the FCM service account environment variable is not set", config._serviceAccountEnvironment } );
                FCMPushSettings      settings;
                const expected<void> read = ServiceAccountCredentials::read( serviceAccountJSON, settings._credentials );
                Memory::set( serviceAccountJSON.data(), 0, serviceAccountJSON.size() ); // 키가 든 글을 남기지 않는다
                if ( read.has_value() == false )
                    return unexpected( read.error() );
                if ( config._tokenURL.empty() == false )
                    settings._credentials._tokenURL = config._tokenURL;
                if ( config._sendBaseURL.empty() == false )
                    settings._sendBaseURL = config._sendBaseURL;
                if ( expected<void> tls = httpClient.registerHostTLS( settings._credentials._tokenURL, trustFile, "pushTrustFile" ).and_then( [&]
                {
                    return httpClient.registerHostTLS( settings._sendBaseURL, trustFile, "pushTrustFile" );
                } );
                     tls.has_value() == false )
                    return unexpected( tls.error() );
                return unique_ptr<IPushNotificationProvider>( make_unique<FCMPushProvider>( std::move( settings ), &game::getSubsystem<INetSecurityProvider>(), &httpClient.get() ) );
            }

            /** @brief 환경 변수의 인증 키로 APNs 제공자를 만들고 HTTP/2 클라이언트의 TLS(ALPN h2)를 준비합니다. 키가 ES256 서명에 못 쓰이면 기동을 멈춘다. */
            [[nodiscard]] static expected<unique_ptr<IPushNotificationProvider>> createAPNS( const PushProviderConfig& config, string_view trustFile, OwnedHTTP2Client& httpClient )
            {
                APNSPushSettings settings;
                const bool       bNamed      = config._keyEnvironment.empty() == false;
                const bool       bSecretRead = bNamed && ServerSecret::read( config._keyEnvironment, settings._privateKeyPem ) && settings._privateKeyPem.empty() == false;
                if ( bSecretRead == false )
                    return unexpected( Error{ ErrorKind::NotFound, "the APNs authentication key environment variable is not set", config._keyEnvironment } );
                settings._keyID  = config._keyID;
                settings._teamID = config._teamID;
                settings._topic  = config._topic;
                if ( config._sendBaseURL.empty() == false )
                    settings._baseURL = config._sendBaseURL;
                vector<uint8>        probeSignature;
                const uint8          probe      = 0;
                const bool           bKeyUsable = game::getSubsystem<INetSecurityProvider>().signData( NetSignatureAlgorithm::EcdsaP256Sha256, settings._privateKeyPem, &probe, 1, probeSignature );
                const expected<void> ready      = bKeyUsable ? httpClient.registerHostTLS( settings._baseURL, trustFile, "pushTrustFile" ) // 실패 이유는 등록이 채운다
                                                             : expected<void>{ unexpected( Error{ ErrorKind::InvalidArgument, "the APNs authentication key is not a usable ES256 private key (.p8 PEM)",
                                                                                             config._keyEnvironment } ) };
                if ( ready.has_value() == false )
                {
                    Memory::set( settings._privateKeyPem.data(), 0, settings._privateKeyPem.size() ); // 키가 든 글을 남기지 않는다
                    return unexpected( ready.error() );
                }
                return unique_ptr<IPushNotificationProvider>( make_unique<APNSPushProvider>( std::move( settings ), &game::getSubsystem<INetSecurityProvider>(), &httpClient.get() ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    PushProviderSet::PushProviderSet()
        : _listEntry{}
    {
    }

    PushProviderSet::~PushProviderSet()
    {
        shutdown();
    }

    expected<void> PushProviderSet::readEntry( const JSONValue& entry, PushProviderConfig& outConfig )
    {
        using Internal  = PushProviderSetInternal;
        outConfig._kind = Internal::readText( entry, "kind" );
        const bool bFCM = outConfig._kind == Internal::kKindFCM;
        if ( bFCM == false && outConfig._kind != Internal::kKindAPNS )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the push provider kind is unknown (fcm, apns)", outConfig._kind } );
        for ( const string& key : entry.getMemberNames() )
        {
            const bool bKnown = bFCM ? VectorUtil::contains( Internal::kKeysFCM, key ) : VectorUtil::contains( Internal::kKeysAPNS, key );
            if ( bKnown == false )
            {
                Error error{ ErrorKind::InvalidArgument, "unknown push provider setting", outConfig._kind };
                error.appendDetail( " " ).appendDetail( key );
                return unexpected( error );
            }
        }
        outConfig._sendBaseURL = Internal::readText( entry, "sendUrl" );
        if ( bFCM )
        {
            outConfig._serviceAccountEnvironment = Internal::readText( entry, "serviceAccountEnvironment" );
            outConfig._tokenURL                  = Internal::readText( entry, "tokenUrl" );
            if ( outConfig._serviceAccountEnvironment.empty() )
                return unexpected( Error{ ErrorKind::InvalidArgument, "push provider 'fcm' needs serviceAccountEnvironment" } );
            return {};
        }
        outConfig._keyEnvironment   = Internal::readText( entry, "keyEnvironment" );
        outConfig._keyID            = Internal::readText( entry, "keyId" );
        outConfig._teamID           = Internal::readText( entry, "teamId" );
        outConfig._topic            = Internal::readText( entry, "topic" );
        const bool bKeyMissing      = outConfig._keyEnvironment.empty() || outConfig._keyID.empty();
        const bool bIdentityMissing = outConfig._teamID.empty() || outConfig._topic.empty();
        if ( bKeyMissing || bIdentityMissing )
            return unexpected( Error{ ErrorKind::InvalidArgument, "push provider 'apns' needs keyEnvironment, keyId, teamId and topic" } );
        return {};
    }

    expected<void> PushProviderSet::initialize( const vector<PushProviderConfig>& listSettings, string_view trustFile, const IClock& clock )
    {
        using Internal = PushProviderSetInternal;
        shutdown();
        for ( const PushProviderConfig& config : listSettings )
        {
            Entry&     entry        = _listEntry.emplace_back();
            const bool bFCM         = config._kind == Internal::kKindFCM;
            bool       bHTTPStarted = false;
            if ( bFCM )
            {
                entry._httpClient = make_unique<OwnedHTTPClient>( clock );
                bHTTPStarted      = entry._httpClient->initialize();
            }
            else
            {
                entry._http2Client = make_unique<OwnedHTTP2Client>( clock );
                bHTTPStarted       = entry._http2Client->initialize();
            }
            if ( bHTTPStarted == false )
            {
                shutdown();
                return unexpected( Error{ ErrorKind::ExternalFailure, "cannot start the HTTP client of the push provider", config._kind } );
            }
            expected<unique_ptr<IPushNotificationProvider>> provider =
                bFCM ? Internal::createFCM( config, trustFile, *entry._httpClient ) : Internal::createAPNS( config, trustFile, *entry._http2Client );
            if ( provider.has_value() == false )
            {
                Error error{ provider.error().kind(), provider.error().message().data(), config._kind };
                error.appendDetail( ": " ).appendDetail( provider.error().detail() );
                shutdown();
                return unexpected( error );
            }
            entry._provider = std::move( *provider );
        }
        return {};
    }

    void PushProviderSet::shutdown()
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._httpClient != nullptr )
                entry._httpClient->shutdown(); // 날아가던 요청을 끝내고 TLS 컨텍스트를 놓는다
            if ( entry._http2Client != nullptr )
                entry._http2Client->shutdown();
        }
        _listEntry.clear();
    }
} // namespace sw
