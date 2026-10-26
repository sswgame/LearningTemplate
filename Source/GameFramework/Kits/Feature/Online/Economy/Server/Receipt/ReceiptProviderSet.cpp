#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptProviderSet.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Base/Online/HTTP/OwnedHTTPClient.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Apple/AppleReceiptValidator.h"
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
            static constexpr const utf8* kKindApple  = "apple";

            static constexpr const utf8* kKnownKeys[] = { "kind", "appId", "keySecretEnvironment", "packageName", "serviceAccountEnvironment",
                                                          "baseUrl", "tokenUrl", "sandbox", "reportPollSeconds", "voidedPollSeconds", "issuerId",
                                                          "keyId", "bundleId", "signingKeySecretEnvironment", "rootCertificateFile", "historyPollSeconds" };

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
                if ( httpClient.registerHostTLS( settings._baseURL, trustFile, "receiptTrustFile", outError ) == false )
                    return nullptr;
                return make_unique<SteamReceiptValidator>( std::move( settings ), &httpClient.get() );
            }

            [[nodiscard]] static unique_ptr<IReceiptValidator> createApple( const ReceiptProviderConfig& config, string_view trustFile, OwnedHTTPClient& httpClient, string& outError )
            {
                AppleReceiptSettings settings;
                const bool           bNamed      = config._signingKeySecretEnvironment.empty() == false;
                const bool           bSecretRead = bNamed && ServerSecret::read( config._signingKeySecretEnvironment, settings._privateKeyPem ) && settings._privateKeyPem.empty() == false;
                if ( bSecretRead == false )
                {
                    outError = "the apple signing key environment variable '" + config._signingKeySecretEnvironment + "' is not set";
                    return nullptr;
                }
                const string rootPath = NetSecurity::makeProjectPath( config._rootCertificateFile );
                if ( FileUtil::readTextFile( rootPath, settings._trustedRootsPem ) == false || settings._trustedRootsPem.empty() )
                {
                    outError = "cannot read the apple root certificate file " + rootPath;
                    return nullptr;
                }
                settings._issuerID = config._issuerID;
                settings._keyID    = config._keyID;
                settings._bundleID = config._bundleID;
                settings._baseURL  = config._bSandbox == SW_TRUE ? AppleReceiptSettings::kSandboxBaseURL : AppleReceiptSettings::kProductionBaseURL;
                if ( config._baseURL.empty() == false )
                    settings._baseURL = config._baseURL;
                settings._bSandboxServer        = config._bSandbox;
                settings._historyPollIntervalMs = config._pollSeconds * kMillisecondsPerSecond;
                if ( httpClient.registerHostTLS( settings._baseURL, trustFile, "receiptTrustFile", outError ) == false )
                    return nullptr;
                return make_unique<AppleReceiptValidator>( std::move( settings ), &NetSecurity::getProvider(), &httpClient.get() );
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
                if ( httpClient.registerHostTLS( settings._credentials._tokenURL, trustFile, "receiptTrustFile", outError ) == false ||
                     httpClient.registerHostTLS( settings._baseURL, trustFile, "receiptTrustFile", outError ) == false )
                    return nullptr;
                return make_unique<GoogleReceiptValidator>( std::move( settings ), &NetSecurity::getProvider(), &httpClient.get() );
            }

            /** @brief 항목의 `sandbox`(참 · 거짓, 없으면 거짓)를 읽습니다. 참 · 거짓이 아니면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool readSandbox( const JSONValue& entry, const utf8* pKind, ReceiptProviderConfig& outConfig, string& outError )
            {
                bool bSandbox = false;
                if ( entry.readOptionalInto( "sandbox", bSandbox ) == false )
                {
                    outError = string( "receipt provider '" ) + pKind + "' has a sandbox that is not true or false";
                    return false;
                }
                outConfig._bSandbox = bSandbox ? SW_TRUE : SW_FALSE;
                return true;
            }

            /** @brief 스팀 항목의 설정을 읽고 검사합니다. 틀리면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool readSteamEntry( const JSONValue& entry, ReceiptProviderConfig& outConfig, string& outError )
            {
                outConfig._appID                = readText( entry, "appId" );
                outConfig._keySecretEnvironment = readText( entry, "keySecretEnvironment" );
                if ( readSandbox( entry, kKindSteam, outConfig, outError ) == false )
                    return false;
                if ( SteamReceiptValidator::isOrderID( outConfig._appID ) == false || outConfig._keySecretEnvironment.empty() )
                {
                    outError = "receipt provider 'steam' needs a numeric appId and keySecretEnvironment";
                    return false;
                }
                return true;
            }

            /** @brief 애플 항목의 설정을 읽고 검사합니다. 틀리면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool readAppleEntry( const JSONValue& entry, ReceiptProviderConfig& outConfig, string& outError )
            {
                outConfig._issuerID                    = readText( entry, "issuerId" );
                outConfig._keyID                       = readText( entry, "keyId" );
                outConfig._bundleID                    = readText( entry, "bundleId" );
                outConfig._signingKeySecretEnvironment = readText( entry, "signingKeySecretEnvironment" );
                outConfig._rootCertificateFile         = readText( entry, "rootCertificateFile" );
                if ( readSandbox( entry, kKindApple, outConfig, outError ) == false )
                    return false;
                const bool bIdentityGiven = outConfig._issuerID.empty() == false && outConfig._keyID.empty() == false && outConfig._bundleID.empty() == false;
                const bool bFilesGiven    = outConfig._signingKeySecretEnvironment.empty() == false && outConfig._rootCertificateFile.empty() == false;
                if ( bIdentityGiven == false || bFilesGiven == false )
                {
                    outError = "receipt provider 'apple' needs issuerId, keyId, bundleId, signingKeySecretEnvironment and rootCertificateFile";
                    return false;
                }
                return true;
            }

            /** @brief 구글 항목의 설정을 읽고 검사합니다. 틀리면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool readGoogleEntry( const JSONValue& entry, ReceiptProviderConfig& outConfig, string& outError )
            {
                outConfig._packageName               = readText( entry, "packageName" );
                outConfig._serviceAccountEnvironment = readText( entry, "serviceAccountEnvironment" );
                outConfig._tokenURL                  = readText( entry, "tokenUrl" );
                if ( outConfig._packageName.empty() || outConfig._serviceAccountEnvironment.empty() )
                {
                    outError = "receipt provider 'google' needs packageName and serviceAccountEnvironment";
                    return false;
                }
                return true;
            }

            /** @brief `providers` 배열의 항목 하나를 읽습니다 — 알려진 키만, 종류와 폴링 주기를 검사하고 종류별 설정을 읽습니다. */
            [[nodiscard]] static bool readEntry( const JSONValue& entry, ReceiptProviderConfig& outConfig, string& outError )
            {
                if ( entry.isObject() == false )
                {
                    outError = "receipt provider entry is not an object";
                    return false;
                }
                for ( const string& key : entry.getMemberNames() )
                {
                    if ( VectorUtil::contains( kKnownKeys, key ) == false )
                    {
                        outError = "unknown receipt provider setting '" + key + "'";
                        return false;
                    }
                }
                outConfig._kind       = readText( entry, "kind" );
                outConfig._baseURL    = readText( entry, "baseUrl" );
                const bool bSteam     = outConfig._kind == kKindSteam;
                const bool bGoogle    = outConfig._kind == kKindGoogle;
                const bool bApple     = outConfig._kind == kKindApple;
                const bool bKnownKind = bSteam || bGoogle || bApple;
                if ( bKnownKind == false )
                {
                    outError = "receipt provider kind '" + outConfig._kind + "' is unknown (steam, google, apple)";
                    return false;
                }
                const utf8* pPollKey = bSteam ? "reportPollSeconds" : ( bGoogle ? "voidedPollSeconds" : "historyPollSeconds" );
                if ( entry.readOptionalInto( pPollKey, outConfig._pollSeconds ) == false || outConfig._pollSeconds < kMinPollSeconds )
                {
                    outError = "receipt provider '" + outConfig._kind + "' needs " + pPollKey + " of at least " + to_string( kMinPollSeconds );
                    return false;
                }
                if ( bSteam )
                    return readSteamEntry( entry, outConfig, outError );
                if ( bApple )
                    return readAppleEntry( entry, outConfig, outError );
                return readGoogleEntry( entry, outConfig, outError );
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
            ReceiptProviderConfig& config = outListSettings.emplace_back();
            if ( ReceiptProviderSetInternal::readEntry( providers.at( index ), config, outError ) == false )
                return false;
        }
        return true;
    }

    bool ReceiptProviderSet::initialize( const vector<ReceiptProviderConfig>& listSettings, string_view trustFile, const IClock& clock, string& outError )
    {
        using Internal = ReceiptProviderSetInternal;
        shutdown();
        for ( const ReceiptProviderConfig& config : listSettings )
        {
            Entry& entry      = _listEntry.emplace_back();
            entry._httpClient = make_unique<OwnedHTTPClient>( clock );
            if ( entry._httpClient->initialize() == false )
            {
                outError = "cannot start the HTTP client of receipt provider '" + config._kind + "'";
                shutdown();
                return false;
            }
            if ( config._kind == Internal::kKindSteam )
                entry._provider = Internal::createSteam( config, trustFile, *entry._httpClient, outError );
            else if ( config._kind == Internal::kKindApple )
                entry._provider = Internal::createApple( config, trustFile, *entry._httpClient, outError );
            else
                entry._provider = Internal::createGoogle( config, trustFile, *entry._httpClient, outError );
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
