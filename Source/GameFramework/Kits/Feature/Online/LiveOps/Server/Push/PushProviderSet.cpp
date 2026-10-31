#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushProviderSet.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"

#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Online/HTTP/OwnedHTTP2Client.h"
#include "GameFramework/Base/Online/HTTP/OwnedHTTPClient.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Apns/ApnsPushProvider.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fcm/FcmPushProvider.h"

namespace sw
{
    SW_LOG_CALLER( "PushProviderSet" );

    namespace
    {
        struct PushProviderSetInternal
        {
            static constexpr const utf8* kKindFcm  = "fcm";
            static constexpr const utf8* kKindApns = "apns";

            /** @brief 종류마다 쓸 수 있는 키입니다 — 다른 종류의 키는 철자 오류와 같이 거절한다. */
            static constexpr const utf8* kKeysFcm[]  = { "kind", "serviceAccountEnvironment", "tokenURL", "sendURL" };
            static constexpr const utf8* kKeysApns[] = { "kind", "keyEnvironment", "keyId", "teamID", "topic", "sendURL" };

            static string readText( const JSONValue& object, const utf8* pKey )
            {
                const JSONValue value = object.get( pKey );
                return value.isString() ? value.asString() : string();
            }

            /** @brief 환경 변수의 서비스 계정으로 FCM 제공자를 만들고 HTTP 클라이언트의 TLS 를 준비합니다. */
            [[nodiscard]] static unique_ptr<IPushNotificationProvider> createFcm( const PushProviderConfig& config, string_view trustFile, OwnedHTTPClient& httpClient,
                                                                                  string& outError )
            {
                string     serviceAccountJSON;
                const bool bNamed      = config._serviceAccountEnvironment.empty() == false;
                const bool bSecretRead = bNamed && ServerSecret::read( config._serviceAccountEnvironment, serviceAccountJSON ) && serviceAccountJSON.empty() == false;
                if ( bSecretRead == false )
                {
                    outError = "the FCM service account environment variable '" + config._serviceAccountEnvironment + "' is not set";
                    return nullptr;
                }
                FcmPushSettings settings;
                const bool      bRead = ServiceAccountCredentials::read( serviceAccountJSON, settings._credentials, outError );
                Memory::set( serviceAccountJSON.data(), 0, serviceAccountJSON.size() ); // 키가 든 글을 남기지 않는다
                if ( bRead == false )
                    return nullptr;
                if ( config._tokenURL.empty() == false )
                    settings._credentials._tokenURL = config._tokenURL;
                if ( config._sendBaseURL.empty() == false )
                    settings._sendBaseURL = config._sendBaseURL;
                if ( httpClient.registerHostTLS( settings._credentials._tokenURL, trustFile, "pushTrustFile", outError ) == false ||
                     httpClient.registerHostTLS( settings._sendBaseURL, trustFile, "pushTrustFile", outError ) == false )
                    return nullptr;
                return make_unique<FcmPushProvider>( std::move( settings ), &game::getSubsystem<INetSecurityProvider>(), &httpClient.get() );
            }

            /** @brief 환경 변수의 인증 키로 APNs 제공자를 만들고 HTTP/2 클라이언트의 TLS(ALPN h2)를 준비합니다. 키가 ES256 서명에 못 쓰이면 기동을 멈춘다. */
            [[nodiscard]] static unique_ptr<IPushNotificationProvider> createApns( const PushProviderConfig& config, string_view trustFile, OwnedHTTP2Client& httpClient,
                                                                                   string& outError )
            {
                ApnsPushSettings settings;
                const bool       bNamed      = config._keyEnvironment.empty() == false;
                const bool       bSecretRead = bNamed && ServerSecret::read( config._keyEnvironment, settings._privateKeyPem ) && settings._privateKeyPem.empty() == false;
                if ( bSecretRead == false )
                {
                    outError = "the APNs authentication key environment variable '" + config._keyEnvironment + "' is not set";
                    return nullptr;
                }
                settings._keyID  = config._keyID;
                settings._teamID = config._teamID;
                settings._topic  = config._topic;
                if ( config._sendBaseURL.empty() == false )
                    settings._baseURL = config._sendBaseURL;
                vector<uint8> probeSignature;
                const uint8   probe      = 0;
                const bool    bKeyUsable = game::getSubsystem<INetSecurityProvider>().signData( NetSignatureAlgorithm::EcdsaP256Sha256, settings._privateKeyPem, &probe, 1, probeSignature );
                bool          bReady     = bKeyUsable;
                if ( bKeyUsable == false )
                    outError = "the APNs authentication key in '" + config._keyEnvironment + "' is not a usable ES256 private key (.p8 PEM)";
                else
                    bReady = httpClient.registerHostTLS( settings._baseURL, trustFile, "pushTrustFile", outError ); // 실패 이유는 등록이 채운다
                if ( bReady == false )
                {
                    Memory::set( settings._privateKeyPem.data(), 0, settings._privateKeyPem.size() ); // 키가 든 글을 남기지 않는다
                    return nullptr;
                }
                return make_unique<ApnsPushProvider>( std::move( settings ), &game::getSubsystem<INetSecurityProvider>(), &httpClient.get() );
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

    bool PushProviderSet::readEntry( const JSONValue& entry, PushProviderConfig& outConfig, string& outError )
    {
        using Internal  = PushProviderSetInternal;
        outConfig._kind = Internal::readText( entry, "kind" );
        const bool bFcm = outConfig._kind == Internal::kKindFcm;
        if ( bFcm == false && outConfig._kind != Internal::kKindApns )
        {
            outError = "push provider kind '" + outConfig._kind + "' is unknown (fcm, apns)";
            return false;
        }
        for ( const string& key : entry.getMemberNames() )
        {
            const bool bKnown = bFcm ? VectorUtil::contains( Internal::kKeysFcm, key ) : VectorUtil::contains( Internal::kKeysApns, key );
            if ( bKnown == false )
            {
                outError = "unknown " + outConfig._kind + " push provider setting '" + key + "'";
                return false;
            }
        }
        outConfig._sendBaseURL = Internal::readText( entry, "sendURL" );
        if ( bFcm )
        {
            outConfig._serviceAccountEnvironment = Internal::readText( entry, "serviceAccountEnvironment" );
            outConfig._tokenURL                  = Internal::readText( entry, "tokenURL" );
            if ( outConfig._serviceAccountEnvironment.empty() )
            {
                outError = "push provider 'fcm' needs serviceAccountEnvironment";
                return false;
            }
            return true;
        }
        outConfig._keyEnvironment   = Internal::readText( entry, "keyEnvironment" );
        outConfig._keyID            = Internal::readText( entry, "keyId" );
        outConfig._teamID           = Internal::readText( entry, "teamID" );
        outConfig._topic            = Internal::readText( entry, "topic" );
        const bool bKeyMissing      = outConfig._keyEnvironment.empty() || outConfig._keyID.empty();
        const bool bIdentityMissing = outConfig._teamID.empty() || outConfig._topic.empty();
        if ( bKeyMissing || bIdentityMissing )
        {
            outError = "push provider 'apns' needs keyEnvironment, keyId, teamID and topic";
            return false;
        }
        return true;
    }

    bool PushProviderSet::initialize( const vector<PushProviderConfig>& listSettings, string_view trustFile, const IClock& clock, string& outError )
    {
        using Internal = PushProviderSetInternal;
        shutdown();
        for ( const PushProviderConfig& config : listSettings )
        {
            Entry&     entry        = _listEntry.emplace_back();
            const bool bFcm         = config._kind == Internal::kKindFcm;
            bool       bHTTPStarted = false;
            if ( bFcm )
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
                outError = "cannot start the HTTP client of push provider '" + config._kind + "'";
                shutdown();
                return false;
            }
            entry._provider = bFcm ? Internal::createFcm( config, trustFile, *entry._httpClient, outError ) : Internal::createApns( config, trustFile, *entry._http2Client, outError );
            if ( entry._provider == nullptr )
            {
                outError = "push provider '" + config._kind + "': " + outError;
                shutdown();
                return false;
            }
        }
        return true;
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
