#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptProviderSet.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"

#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Base/Online/HTTP/OwnedHTTPClient.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Google/GoogleReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Steam/SteamReceiptValidator.h"

namespace sw
{
    SW_LOG_CALLER( "ReceiptProviderSet" );

    namespace
    {
        struct ReceiptProviderSetInternal
        {
            static constexpr const utf8* kKindSteam  = "steam";
            static constexpr const utf8* kKindGoogle = "google";

            static constexpr const utf8* kKnownKeys[] = { "kind", "appId", "keySecretEnvironment", "packageName", "serviceAccountEnvironment",
                                                          "baseUrl", "tokenUrl", "sandbox", "reportPollSeconds", "voidedPollSeconds" };

            static constexpr int64 kMillisecondsPerSecond = 1000;
            static constexpr int64 kMinPollSeconds        = 60;

            static string readText( const JSONValue& object, const utf8* pKey )
            {
                const JSONValue value = object.get( pKey, false );
                return value.isString() ? value.asString() : string();
            }

            [[nodiscard]] static unique_ptr<IReceiptValidator> createSteam( const ReceiptProviderConfig& config, string_view trustFile, OwnedHTTPClient& httpClient, string& outError )
            {
                SteamReceiptSettings settings;
                if ( ServerSecret::read( config._keySecretEnvironment, settings._key ) == false || settings._key.empty() )
                {
                    outError = "the steam publisher key environment variable '" + config._keySecretEnvironment + "' is not set";
                    return nullptr;
                }
                if ( settings._key.find_first_of( "&=?# %+/" ) != string::npos )
                {
                    outError = "the steam publisher key has characters that cannot go into a query";
                    return nullptr;
                }
                settings._appID = config._appID;
                if ( config._baseURL.empty() == false )
                    settings._baseURL = config._baseURL;
                settings._bSandbox             = config._bSandbox;
                settings._reportPollIntervalMs = config._pollSeconds * kMillisecondsPerSecond;
                if ( httpClient.registerHostTLS( settings._baseURL, trustFile, outError ) == false )
                    return nullptr;
                return make_unique<SteamReceiptValidator>( std::move( settings ), &httpClient.get() );
            }

            [[nodiscard]] static unique_ptr<IReceiptValidator> createGoogle( const ReceiptProviderConfig& config, string_view trustFile, OwnedHTTPClient& httpClient, string& outError )
            {
                string serviceAccountJSON;
                if ( ServerSecret::read( config._serviceAccountEnvironment, serviceAccountJSON ) == false || serviceAccountJSON.empty() )
                {
                    outError = "the google service account environment variable '" + config._serviceAccountEnvironment + "' is not set";
                    return nullptr;
                }
                GoogleReceiptSettings settings;
                const bool            bRead = ServiceAccountCredentials::read( serviceAccountJSON, settings._credentials, outError );
                Memory::set( serviceAccountJSON.data(), 0, serviceAccountJSON.size() ); // 키가 든 글을 남기지 않는다
                if ( bRead == false )
                    return nullptr;
                settings._packageName = config._packageName;
                if ( config._tokenURL.empty() == false )
                    settings._credentials._tokenURL = config._tokenURL;
                if ( config._baseURL.empty() == false )
                    settings._baseURL = config._baseURL;
                settings._voidedPollIntervalMs = config._pollSeconds * kMillisecondsPerSecond;
                if ( httpClient.registerHostTLS( settings._credentials._tokenURL, trustFile, outError ) == false ||
                     httpClient.registerHostTLS( settings._baseURL, trustFile, outError ) == false )
                    return nullptr;
                return make_unique<GoogleReceiptValidator>( std::move( settings ), &NetSecurity::getProvider(), &httpClient.get() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ReceiptProviderSet::ReceiptProviderSet()
        : _listEntry{}
    {
    }

    ReceiptProviderSet::~ReceiptProviderSet()
    {
        shutdown();
    }

    bool ReceiptProviderSet::readSettings( string_view jsonText, vector<ReceiptProviderConfig>& outListSettings, string& outError )
    {
        using Internal = ReceiptProviderSetInternal;
        outListSettings.clear();
        JSONDocument document;
        if ( document.tryParse( jsonText ) == false )
        {
            outError = "receipt provider settings are not JSON";
            return false;
        }
        const JSONValue providers = document.getRoot().get( "providers", false );
        if ( providers.isArray() == false )
        {
            outError = "receipt provider settings need a 'providers' array";
            return false;
        }
        for ( size_t index = 0; index < providers.size(); ++index )
        {
            const JSONValue entry = providers.at( index );
            if ( entry.isObject() == false )
            {
                outError = "receipt provider entry is not an object";
                return false;
            }
            for ( const string& key : entry.getMemberNames() )
            {
                if ( VectorUtil::contains( Internal::kKnownKeys, key ) == false )
                {
                    outError = "unknown receipt provider setting '" + key + "'";
                    return false;
                }
            }
            ReceiptProviderConfig& config = outListSettings.emplace_back();
            config._kind                  = Internal::readText( entry, "kind" );
            config._baseURL               = Internal::readText( entry, "baseUrl" );
            const bool bSteam             = config._kind == Internal::kKindSteam;
            const bool bGoogle            = config._kind == Internal::kKindGoogle;
            if ( bSteam == false && bGoogle == false )
            {
                outError = "receipt provider kind '" + config._kind + "' is unknown (steam, google)";
                return false;
            }
            const utf8* pPollKey = bSteam ? "reportPollSeconds" : "voidedPollSeconds";
            if ( entry.readOptionalInto( pPollKey, config._pollSeconds ) == false || config._pollSeconds < Internal::kMinPollSeconds )
            {
                outError = "receipt provider '" + config._kind + "' needs " + pPollKey + " of at least " + to_string( Internal::kMinPollSeconds );
                return false;
            }
            if ( bSteam )
            {
                config._appID                = Internal::readText( entry, "appId" );
                config._keySecretEnvironment = Internal::readText( entry, "keySecretEnvironment" );
                bool bSandbox                = false;
                if ( entry.readOptionalInto( "sandbox", bSandbox ) == false )
                {
                    outError = "receipt provider 'steam' has a sandbox that is not true or false";
                    return false;
                }
                config._bSandbox = bSandbox ? SW_TRUE : SW_FALSE;
                if ( SteamReceiptValidator::isOrderID( config._appID ) == false || config._keySecretEnvironment.empty() )
                {
                    outError = "receipt provider 'steam' needs a numeric appId and keySecretEnvironment";
                    return false;
                }
            }
            else
            {
                config._packageName               = Internal::readText( entry, "packageName" );
                config._serviceAccountEnvironment = Internal::readText( entry, "serviceAccountEnvironment" );
                config._tokenURL                  = Internal::readText( entry, "tokenUrl" );
                if ( config._packageName.empty() || config._serviceAccountEnvironment.empty() )
                {
                    outError = "receipt provider 'google' needs packageName and serviceAccountEnvironment";
                    return false;
                }
            }
        }
        return true;
    }

    bool ReceiptProviderSet::initialize( const vector<ReceiptProviderConfig>& listSettings, string_view trustFile, string& outError )
    {
        using Internal = ReceiptProviderSetInternal;
        shutdown();
        for ( const ReceiptProviderConfig& config : listSettings )
        {
            Entry& entry      = _listEntry.emplace_back();
            entry._httpClient = make_unique<OwnedHTTPClient>();
            if ( entry._httpClient->initialize() == false )
            {
                outError = "cannot start the HTTP client of receipt provider '" + config._kind + "'";
                shutdown();
                return false;
            }
            entry._provider = config._kind == Internal::kKindSteam ? Internal::createSteam( config, trustFile, *entry._httpClient, outError )
                                                                   : Internal::createGoogle( config, trustFile, *entry._httpClient, outError );
            if ( entry._provider == nullptr )
            {
                outError = "receipt provider '" + config._kind + "': " + outError;
                shutdown();
                return false;
            }
        }
        return true;
    }

    void ReceiptProviderSet::shutdown()
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._httpClient != nullptr )
                entry._httpClient->shutdown(); // 날아가던 요청을 끝내고 TLS 컨텍스트를 놓는다
        }
        _listEntry.clear();
    }
} // namespace sw
