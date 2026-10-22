#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushProviderSet.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Base/Online/HTTP/OwnedHTTPClient.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fcm/FcmPushProvider.h"

namespace sw
{
    SW_LOG_CALLER( "PushProviderSet" );

    namespace
    {
        struct PushProviderSetInternal
        {
            static constexpr const utf8* kKindFcm = "fcm";

            static constexpr const utf8* kKnownKeys[] = { "kind", "serviceAccountEnvironment", "tokenUrl", "sendUrl" };

            static string readText( const JSONValue& object, const utf8* pKey )
            {
                const JSONValue value = object.get( pKey, false );
                return value.isString() ? value.asString() : string();
            }

            /** @brief 환경 변수의 서비스 계정으로 FCM 제공자를 만들고 HTTP 클라이언트의 TLS 를 준비합니다. */
            [[nodiscard]] static unique_ptr<IPushNotificationProvider> createFcm( const PushProviderConfig& config, string_view trustFile, OwnedHTTPClient& httpClient,
                                                                                  string& outError )
            {
                string serviceAccountJSON;
                if ( config._serviceAccountEnvironment.empty() || ServerSecret::read( config._serviceAccountEnvironment, serviceAccountJSON ) == false ||
                     serviceAccountJSON.empty() )
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
                if ( httpClient.registerHostTLS( settings._credentials._tokenURL, trustFile, outError ) == false ||
                     httpClient.registerHostTLS( settings._sendBaseURL, trustFile, outError ) == false )
                    return nullptr;
                return make_unique<FcmPushProvider>( std::move( settings ), &NetSecurity::getProvider(), &httpClient.get() );
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

    bool PushProviderSet::readSettings( string_view jsonText, vector<PushProviderConfig>& outListSettings, string& outError )
    {
        using Internal = PushProviderSetInternal;
        outListSettings.clear();
        JSONDocument document;
        if ( document.tryParse( jsonText ) == false )
        {
            outError = "push provider settings are not JSON";
            return false;
        }
        const JSONValue providers = document.getRoot().get( "providers", false );
        if ( providers.isArray() == false )
        {
            outError = "push provider settings need a 'providers' array";
            return false;
        }
        for ( size_t index = 0; index < providers.size(); ++index )
        {
            const JSONValue entry = providers.at( index );
            if ( entry.isObject() == false )
            {
                outError = "push provider entry is not an object";
                return false;
            }
            for ( const string& key : entry.getMemberNames() )
            {
                if ( VectorUtil::contains( Internal::kKnownKeys, key ) == false )
                {
                    outError = "unknown push provider setting '" + key + "'";
                    return false;
                }
            }
            PushProviderConfig& config = outListSettings.emplace_back();
            config._kind               = Internal::readText( entry, "kind" );
            if ( config._kind != Internal::kKindFcm )
            {
                outError = "push provider kind '" + config._kind + "' is unknown (fcm)";
                return false;
            }
            config._serviceAccountEnvironment = Internal::readText( entry, "serviceAccountEnvironment" );
            config._tokenURL                  = Internal::readText( entry, "tokenUrl" );
            config._sendBaseURL               = Internal::readText( entry, "sendUrl" );
            if ( config._serviceAccountEnvironment.empty() )
            {
                outError = "push provider '" + config._kind + "' needs serviceAccountEnvironment";
                return false;
            }
        }
        return true;
    }

    bool PushProviderSet::initialize( const vector<PushProviderConfig>& listSettings, string_view trustFile, string& outError )
    {
        using Internal = PushProviderSetInternal;
        shutdown();
        for ( const PushProviderConfig& config : listSettings )
        {
            Entry& entry      = _listEntry.emplace_back();
            entry._httpClient = make_unique<OwnedHTTPClient>();
            if ( entry._httpClient->initialize() == false )
            {
                outError = "cannot start the HTTP client of push provider '" + config._kind + "'";
                shutdown();
                return false;
            }
            entry._provider = Internal::createFcm( config, trustFile, *entry._httpClient, outError );
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
        }
        _listEntry.clear();
    }
} // namespace sw
