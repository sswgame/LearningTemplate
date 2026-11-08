#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptProviderSet.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/Server/ServerSecret.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Apple/AppleReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Google/GoogleReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Steam/SteamReceiptValidator.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/HTTP/OwnedHTTPClient.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"

namespace sw
{
    namespace
    {
        struct ReceiptProviderSetInternal
        {
            static constexpr const utf8* kKindSteam  = "steam";
            static constexpr const utf8* kKindGoogle = "google";
            static constexpr const utf8* kKindApple  = "apple";

            static constexpr const utf8* kKnownKeys[] = { "kind", "appId", "keySecretEnvironment", "packageName", "serviceAccountEnvironment",
                                                          "baseUrl", "tokenUrl", "sandbox", "reportPollSeconds", "voidedPollSeconds", "issuerId",
                                                          "keyId", "bundleId", "signingKeySecretEnvironment", "rootCertificateFile", "historyPollSeconds", "buyerBinding",
                                                          "buyerLoginProvider" };

            static constexpr int64 kMinPollSeconds = 60;

            static constexpr const utf8* kBuyerNone         = "none";
            static constexpr const utf8* kBuyerLinkedLogin  = "linkedLogin";
            static constexpr const utf8* kBuyerAccountToken = "accountToken";

            static string readText( const JSONValue& object, const utf8* pKey )
            {
                const JSONValue value = object.get( pKey );
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
                settings._appID     = config._appID;
                settings._buyerRule = config._buyerRule;
                if ( config._baseURL.empty() == false )
                    settings._baseURL = config._baseURL;
                settings._bSandbox             = config._bSandbox;
                settings._reportPollIntervalMs = config._pollSeconds * constant::time::kMillisecondsPerSecond;
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
                string rootPath;
                if ( NetSecurity::resolveProjectPath( config._rootCertificateFile, rootPath, outError ) == false )
                    return nullptr;
                if ( FileUtil::readTextFile( rootPath, settings._trustedRootsPem ) == false || settings._trustedRootsPem.empty() )
                {
                    outError = "cannot read the apple root certificate file " + rootPath;
                    return nullptr;
                }
                settings._issuerID  = config._issuerID;
                settings._keyID     = config._keyID;
                settings._bundleID  = config._bundleID;
                settings._buyerRule = config._buyerRule;
                settings._baseURL   = config._bSandbox == SW_TRUE ? AppleReceiptSettings::kSandboxBaseURL : AppleReceiptSettings::kProductionBaseURL;
                if ( config._baseURL.empty() == false )
                    settings._baseURL = config._baseURL;
                settings._bSandboxServer        = config._bSandbox;
                settings._historyPollIntervalMs = config._pollSeconds * constant::time::kMillisecondsPerSecond;
                if ( httpClient.registerHostTLS( settings._baseURL, trustFile, "receiptTrustFile", outError ) == false )
                    return nullptr;
                return make_unique<AppleReceiptValidator>( std::move( settings ), &game::getSubsystem<INetSecurityProvider>(), &httpClient.get() );
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
                settings._buyerRule   = config._buyerRule;
                if ( config._tokenURL.empty() == false )
                    settings._credentials._tokenURL = config._tokenURL;
                if ( config._baseURL.empty() == false )
                    settings._baseURL = config._baseURL;
                settings._voidedPollIntervalMs = config._pollSeconds * constant::time::kMillisecondsPerSecond;
                if ( httpClient.registerHostTLS( settings._credentials._tokenURL, trustFile, "receiptTrustFile", outError ) == false ||
                     httpClient.registerHostTLS( settings._baseURL, trustFile, "receiptTrustFile", outError ) == false )
                    return nullptr;
                return make_unique<GoogleReceiptValidator>( std::move( settings ), &game::getSubsystem<INetSecurityProvider>(), &httpClient.get() );
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

            /**
             * @brief 항목의 구매자 규칙을 읽습니다. 필수 키입니다 — 묶지 않는 것도 `none` 으로 적어야 한다. 그 스토어가 줄 수 없는 묶기(@p allowedBinding 밖)나 모르는 값은 오류입니다.
             * @details 대응 데이터가 없는 경우를 조용히 넘기지 않으려는 것이다 — 키를 빠뜨린 설정이 "먼저 낸 계정이 받는다" 로 뜨면 그 결함을 다시 만든다.
             */
            [[nodiscard]] static bool readBuyerRule( const JSONValue& entry, ReceiptBuyerBinding allowedBinding, ReceiptProviderConfig& outConfig, string& outError )
            {
                const string binding                = readText( entry, "buyerBinding" );
                outConfig._buyerRule._loginProvider = readText( entry, "buyerLoginProvider" );
                const bool bLinked                  = binding == kBuyerLinkedLogin && allowedBinding == ReceiptBuyerBinding::LinkedLogin;
                const bool bToken                   = binding == kBuyerAccountToken && allowedBinding == ReceiptBuyerBinding::AccountToken;
                if ( bLinked == false && bToken == false && binding != kBuyerNone )
                {
                    StringBuilder<constant::buffer::kMax256> errorBuilder;
                    errorBuilder.appendFormat( "receipt provider '%#' needs buyerBinding '%#' or '%#'", outConfig._kind, allowedBinding == ReceiptBuyerBinding::LinkedLogin ? kBuyerLinkedLogin : kBuyerAccountToken, kBuyerNone );
                    outError = string( errorBuilder.view() );
                    return false;
                }
                if ( bLinked != ( outConfig._buyerRule._loginProvider.empty() == false ) )
                {
                    StringBuilder<constant::buffer::kMax256> errorBuilder;
                    errorBuilder.appendFormat( "receipt provider '%#' needs buyerLoginProvider exactly when buyerBinding is '%#'", outConfig._kind, kBuyerLinkedLogin );
                    outError = string( errorBuilder.view() );
                    return false;
                }
                outConfig._buyerRule._binding = bLinked ? ReceiptBuyerBinding::LinkedLogin : ( bToken ? ReceiptBuyerBinding::AccountToken : ReceiptBuyerBinding::None );
                return true;
            }

            /** @brief 스팀 항목의 설정을 읽고 검사합니다. 틀리면 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool readSteamEntry( const JSONValue& entry, ReceiptProviderConfig& outConfig, string& outError )
            {
                outConfig._appID                = readText( entry, "appId" );
                outConfig._keySecretEnvironment = readText( entry, "keySecretEnvironment" );
                if ( readSandbox( entry, kKindSteam, outConfig, outError ) == false || readBuyerRule( entry, ReceiptBuyerBinding::LinkedLogin, outConfig, outError ) == false )
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
                if ( readSandbox( entry, kKindApple, outConfig, outError ) == false || readBuyerRule( entry, ReceiptBuyerBinding::AccountToken, outConfig, outError ) == false )
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
                if ( readBuyerRule( entry, ReceiptBuyerBinding::AccountToken, outConfig, outError ) == false )
                    return false;
                if ( outConfig._packageName.empty() || outConfig._serviceAccountEnvironment.empty() )
                {
                    outError = "receipt provider 'google' needs packageName and serviceAccountEnvironment";
                    return false;
                }
                return true;
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

    bool ReceiptProviderSet::readEntry( const JSONValue& entry, ReceiptProviderConfig& outConfig, string& outError )
    {
        using Internal = ReceiptProviderSetInternal;
        for ( const string& key : entry.getMemberNames() )
        {
            if ( VectorUtil::contains( Internal::kKnownKeys, key ) == false )
            {
                outError = "unknown receipt provider setting '" + key + "'";
                return false;
            }
        }
        outConfig._kind       = Internal::readText( entry, "kind" );
        outConfig._baseURL    = Internal::readText( entry, "baseUrl" );
        const bool bSteam     = outConfig._kind == Internal::kKindSteam;
        const bool bGoogle    = outConfig._kind == Internal::kKindGoogle;
        const bool bApple     = outConfig._kind == Internal::kKindApple;
        const bool bKnownKind = bSteam || bGoogle || bApple;
        if ( bKnownKind == false )
        {
            outError = "receipt provider kind '" + outConfig._kind + "' is unknown (steam, google, apple)";
            return false;
        }
        const utf8* pPollKey = bSteam ? "reportPollSeconds" : ( bGoogle ? "voidedPollSeconds" : "historyPollSeconds" );
        if ( entry.readOptionalInto( pPollKey, outConfig._pollSeconds ) == false || outConfig._pollSeconds < Internal::kMinPollSeconds )
        {
            StringBuilder<constant::buffer::kMax256> errorBuilder;
            errorBuilder.appendFormat( "receipt provider '%#' needs %# of at least %#", outConfig._kind, pPollKey, Internal::kMinPollSeconds );
            outError = string( errorBuilder.view() );
            return false;
        }
        if ( bSteam )
            return Internal::readSteamEntry( entry, outConfig, outError );
        if ( bApple )
            return Internal::readAppleEntry( entry, outConfig, outError );
        return Internal::readGoogleEntry( entry, outConfig, outError );
    }

    bool ReceiptProviderSet::initialize( const vector<ReceiptProviderConfig>& listSettings, string_view trustFile, const IClock& clock, string& outError )
    {
        using Internal = ReceiptProviderSetInternal;
        shutdown();
        for ( const ReceiptProviderConfig& config : listSettings )
        {
            if ( config._buyerRule._binding == ReceiptBuyerBinding::None )
                SW_LOG_WARNING_ID( "ReceiptProviderSet.ReceiptProviderHasNoBuyerBinding", "Receipt provider '%#' does not bind receipts to buyers - the first account to submit a receipt receives it", config._kind );
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
