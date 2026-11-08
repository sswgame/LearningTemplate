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

            using ValidatorResult = expected<unique_ptr<IReceiptValidator>>;

            [[nodiscard]] static ValidatorResult createSteam( const ReceiptProviderConfig& config, string_view trustFile, OwnedHTTPClient& httpClient )
            {
                SteamReceiptSettings settings;
                if ( ServerSecret::read( config._keySecretEnvironment, settings._key ) == false || settings._key.empty() )
                    return unexpected( Error{ ErrorKind::NotFound, "the steam publisher key environment variable is not set", config._keySecretEnvironment } );
                if ( settings._key.find_first_of( "&=?# %+/" ) != string::npos )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the steam publisher key has characters that cannot go into a query" } );
                settings._appID     = config._appID;
                settings._buyerRule = config._buyerRule;
                if ( config._baseURL.empty() == false )
                    settings._baseURL = config._baseURL;
                settings._bSandbox             = config._bSandbox;
                settings._reportPollIntervalMs = config._pollSeconds * constant::time::kMillisecondsPerSecond;
                if ( expected<void> tls = httpClient.registerHostTLS( settings._baseURL, trustFile, "receiptTrustFile" ); tls.has_value() == false )
                    return unexpected( tls.error() );
                return ValidatorResult( make_unique<SteamReceiptValidator>( std::move( settings ), &httpClient.get() ) );
            }

            [[nodiscard]] static ValidatorResult createApple( const ReceiptProviderConfig& config, string_view trustFile, OwnedHTTPClient& httpClient )
            {
                AppleReceiptSettings settings;
                const bool           bNamed      = config._signingKeySecretEnvironment.empty() == false;
                const bool           bSecretRead = bNamed && ServerSecret::read( config._signingKeySecretEnvironment, settings._privateKeyPem ) && settings._privateKeyPem.empty() == false;
                if ( bSecretRead == false )
                    return unexpected( Error{ ErrorKind::NotFound, "the apple signing key environment variable is not set", config._signingKeySecretEnvironment } );
                const expected<string> rootPath = NetSecurity::resolveProjectPath( config._rootCertificateFile );
                if ( rootPath.has_value() == false )
                    return unexpected( rootPath.error() );
                if ( FileUtil::readTextFile( *rootPath, settings._trustedRootsPem ) == false || settings._trustedRootsPem.empty() )
                    return unexpected( Error{ ErrorKind::NotFound, "cannot read the apple root certificate file", *rootPath } );
                settings._issuerID  = config._issuerID;
                settings._keyID     = config._keyID;
                settings._bundleID  = config._bundleID;
                settings._buyerRule = config._buyerRule;
                settings._baseURL   = config._bSandbox == SW_TRUE ? AppleReceiptSettings::kSandboxBaseURL : AppleReceiptSettings::kProductionBaseURL;
                if ( config._baseURL.empty() == false )
                    settings._baseURL = config._baseURL;
                settings._bSandboxServer        = config._bSandbox;
                settings._historyPollIntervalMs = config._pollSeconds * constant::time::kMillisecondsPerSecond;
                if ( expected<void> tls = httpClient.registerHostTLS( settings._baseURL, trustFile, "receiptTrustFile" ); tls.has_value() == false )
                    return unexpected( tls.error() );
                return ValidatorResult( make_unique<AppleReceiptValidator>( std::move( settings ), &game::getSubsystem<INetSecurityProvider>(), &httpClient.get() ) );
            }

            [[nodiscard]] static ValidatorResult createGoogle( const ReceiptProviderConfig& config, string_view trustFile, OwnedHTTPClient& httpClient )
            {
                string serviceAccountJSON;
                if ( ServerSecret::read( config._serviceAccountEnvironment, serviceAccountJSON ) == false || serviceAccountJSON.empty() )
                    return unexpected( Error{ ErrorKind::NotFound, "the google service account environment variable is not set", config._serviceAccountEnvironment } );
                GoogleReceiptSettings settings;
                const expected<void>  read = ServiceAccountCredentials::read( serviceAccountJSON, settings._credentials );
                Memory::set( serviceAccountJSON.data(), 0, serviceAccountJSON.size() ); // 키가 든 글을 남기지 않는다
                if ( read.has_value() == false )
                    return unexpected( read.error() );
                settings._packageName = config._packageName;
                settings._buyerRule   = config._buyerRule;
                if ( config._tokenURL.empty() == false )
                    settings._credentials._tokenURL = config._tokenURL;
                if ( config._baseURL.empty() == false )
                    settings._baseURL = config._baseURL;
                settings._voidedPollIntervalMs = config._pollSeconds * constant::time::kMillisecondsPerSecond;
                if ( expected<void> tls = httpClient.registerHostTLS( settings._credentials._tokenURL, trustFile, "receiptTrustFile" ).and_then( [&]
                {
                    return httpClient.registerHostTLS( settings._baseURL, trustFile, "receiptTrustFile" );
                } );
                     tls.has_value() == false )
                    return unexpected( tls.error() );
                return ValidatorResult( make_unique<GoogleReceiptValidator>( std::move( settings ), &game::getSubsystem<INetSecurityProvider>(), &httpClient.get() ) );
            }

            /** @brief 항목의 `sandbox`(참 · 거짓, 없으면 거짓)를 읽습니다. 참 · 거짓이 아니면 대상이 제공자 종류인 오류입니다. */
            [[nodiscard]] static expected<void> readSandbox( const JSONValue& entry, const utf8* pKind, ReceiptProviderConfig& outConfig )
            {
                bool bSandbox = false;
                if ( entry.readOptionalInto( "sandbox", bSandbox ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the receipt provider has a sandbox that is not true or false", pKind } );
                outConfig._bSandbox = bSandbox ? SW_TRUE : SW_FALSE;
                return {};
            }

            /**
             * @brief 항목의 구매자 규칙을 읽습니다. 필수 키입니다 — 묶지 않는 것도 `none` 으로 적어야 한다. 그 스토어가 줄 수 없는 묶기(@p allowedBinding 밖)나 모르는 값은 오류입니다.
             * @details 대응 데이터가 없는 경우를 조용히 넘기지 않으려는 것이다 — 키를 빠뜨린 설정이 "먼저 낸 계정이 받는다" 로 뜨면 그 결함을 다시 만든다.
             */
            [[nodiscard]] static expected<void> readBuyerRule( const JSONValue& entry, ReceiptBuyerBinding allowedBinding, ReceiptProviderConfig& outConfig )
            {
                const string binding                = readText( entry, "buyerBinding" );
                outConfig._buyerRule._loginProvider = readText( entry, "buyerLoginProvider" );
                const bool bLinked                  = binding == kBuyerLinkedLogin && allowedBinding == ReceiptBuyerBinding::LinkedLogin;
                const bool bToken                   = binding == kBuyerAccountToken && allowedBinding == ReceiptBuyerBinding::AccountToken;
                if ( bLinked == false && bToken == false && binding != kBuyerNone )
                {
                    StringBuilder<constant::buffer::kMax128> detailBuilder;
                    detailBuilder.appendFormat( "%# needs '%#' or '%#'", outConfig._kind, allowedBinding == ReceiptBuyerBinding::LinkedLogin ? kBuyerLinkedLogin : kBuyerAccountToken, kBuyerNone );
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the receipt provider has a buyerBinding its store cannot give", detailBuilder.view() } );
                }
                if ( bLinked != ( outConfig._buyerRule._loginProvider.empty() == false ) )
                {
                    Error error{ ErrorKind::InvalidArgument, "the receipt provider needs buyerLoginProvider exactly when buyerBinding is linkedLogin", outConfig._kind };
                    return unexpected( error );
                }
                outConfig._buyerRule._binding = bLinked ? ReceiptBuyerBinding::LinkedLogin : ( bToken ? ReceiptBuyerBinding::AccountToken : ReceiptBuyerBinding::None );
                return {};
            }

            /** @brief 스팀 항목의 설정을 읽고 검사합니다. 틀리면 오류를 남기고 false 입니다. */
            [[nodiscard]] static expected<void> readSteamEntry( const JSONValue& entry, ReceiptProviderConfig& outConfig )
            {
                outConfig._appID                = readText( entry, "appId" );
                outConfig._keySecretEnvironment = readText( entry, "keySecretEnvironment" );
                if ( expected<void> read = readSandbox( entry, kKindSteam, outConfig ).and_then( [&]
                { return readBuyerRule( entry, ReceiptBuyerBinding::LinkedLogin, outConfig ); } );
                     read.has_value() == false )
                    return read;
                if ( SteamReceiptValidator::isOrderID( outConfig._appID ) == false || outConfig._keySecretEnvironment.empty() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "receipt provider 'steam' needs a numeric appId and keySecretEnvironment" } );
                return {};
            }

            /** @brief 애플 항목의 설정을 읽고 검사합니다. 틀리면 오류를 남기고 false 입니다. */
            [[nodiscard]] static expected<void> readAppleEntry( const JSONValue& entry, ReceiptProviderConfig& outConfig )
            {
                outConfig._issuerID                    = readText( entry, "issuerId" );
                outConfig._keyID                       = readText( entry, "keyId" );
                outConfig._bundleID                    = readText( entry, "bundleId" );
                outConfig._signingKeySecretEnvironment = readText( entry, "signingKeySecretEnvironment" );
                outConfig._rootCertificateFile         = readText( entry, "rootCertificateFile" );
                if ( expected<void> read = readSandbox( entry, kKindApple, outConfig ).and_then( [&]
                { return readBuyerRule( entry, ReceiptBuyerBinding::AccountToken, outConfig ); } );
                     read.has_value() == false )
                    return read;
                const bool bIdentityGiven = outConfig._issuerID.empty() == false && outConfig._keyID.empty() == false && outConfig._bundleID.empty() == false;
                const bool bFilesGiven    = outConfig._signingKeySecretEnvironment.empty() == false && outConfig._rootCertificateFile.empty() == false;
                if ( bIdentityGiven == false || bFilesGiven == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "receipt provider 'apple' needs issuerId, keyId, bundleId, signingKeySecretEnvironment and rootCertificateFile" } );
                return {};
            }

            /** @brief 구글 항목의 설정을 읽고 검사합니다. 틀리면 오류를 남기고 false 입니다. */
            [[nodiscard]] static expected<void> readGoogleEntry( const JSONValue& entry, ReceiptProviderConfig& outConfig )
            {
                outConfig._packageName               = readText( entry, "packageName" );
                outConfig._serviceAccountEnvironment = readText( entry, "serviceAccountEnvironment" );
                outConfig._tokenURL                  = readText( entry, "tokenUrl" );
                if ( expected<void> result = readBuyerRule( entry, ReceiptBuyerBinding::AccountToken, outConfig ); result.has_value() == false )
                    return result;
                if ( outConfig._packageName.empty() || outConfig._serviceAccountEnvironment.empty() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "receipt provider 'google' needs packageName and serviceAccountEnvironment" } );
                return {};
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

    expected<void> ReceiptProviderSet::readEntry( const JSONValue& entry, ReceiptProviderConfig& outConfig )
    {
        using Internal = ReceiptProviderSetInternal;
        for ( const string& key : entry.getMemberNames() )
        {
            if ( VectorUtil::contains( Internal::kKnownKeys, key ) == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "unknown receipt provider setting", key } );
        }
        outConfig._kind       = Internal::readText( entry, "kind" );
        outConfig._baseURL    = Internal::readText( entry, "baseUrl" );
        const bool bSteam     = outConfig._kind == Internal::kKindSteam;
        const bool bGoogle    = outConfig._kind == Internal::kKindGoogle;
        const bool bApple     = outConfig._kind == Internal::kKindApple;
        const bool bKnownKind = bSteam || bGoogle || bApple;
        if ( bKnownKind == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the receipt provider kind is unknown (steam, google, apple)", outConfig._kind } );
        const utf8* pPollKey = bSteam ? "reportPollSeconds" : ( bGoogle ? "voidedPollSeconds" : "historyPollSeconds" );
        if ( entry.readOptionalInto( pPollKey, outConfig._pollSeconds ) == false || outConfig._pollSeconds < Internal::kMinPollSeconds )
        {
            StringBuilder<constant::buffer::kMax128> detailBuilder;
            detailBuilder.appendFormat( "%# needs %# of at least %#", outConfig._kind, pPollKey, Internal::kMinPollSeconds );
            return unexpected( Error{ ErrorKind::InvalidArgument, "the receipt provider poll interval is too short or not a number", detailBuilder.view() } );
        }
        if ( bSteam )
            return Internal::readSteamEntry( entry, outConfig );
        if ( bApple )
            return Internal::readAppleEntry( entry, outConfig );
        return Internal::readGoogleEntry( entry, outConfig );
    }

    expected<void> ReceiptProviderSet::initialize( const vector<ReceiptProviderConfig>& listSettings, string_view trustFile, const IClock& clock )
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
                shutdown();
                return unexpected( Error{ ErrorKind::ExternalFailure, "cannot start the HTTP client of the receipt provider", config._kind } );
            }
            Internal::ValidatorResult provider = config._kind == Internal::kKindSteam ? Internal::createSteam( config, trustFile, *entry._httpClient )
                                               : config._kind == Internal::kKindApple ? Internal::createApple( config, trustFile, *entry._httpClient )
                                                                                      : Internal::createGoogle( config, trustFile, *entry._httpClient );
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
