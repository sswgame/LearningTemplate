#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderSet.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/Provider/Apple/AppleLoginProvider.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Online/HTTP/OwnedHTTPClient.h"
#include "GameFramework/Runtime/Online/Security/NetSecurity.h"

namespace sw
{
    PlatformLoginProviderSet::PlatformLoginProviderSet()
        : _listEntry{}
    {
    }

    PlatformLoginProviderSet::~PlatformLoginProviderSet()
    {
        shutdown();
    }

    expected<void> PlatformLoginProviderSet::initialize( const vector<PlatformLoginProviderConfig>& listSettings, string_view trustFile, const IClock& clock )
    {
        shutdown();
        for ( const PlatformLoginProviderConfig& settings : listSettings )
        {
            Entry& entry      = _listEntry.emplace_back();
            entry._httpClient = make_unique<OwnedHTTPClient>( clock );
            if ( entry._httpClient->initialize() == false )
            {
                shutdown();
                return unexpected( Error{ ErrorKind::ExternalFailure, "cannot start the HTTP client of the platform login provider", settings._name } );
            }
            const expected<void> tlsReady = entry._httpClient->registerHostTLS( settings._jwksURL, trustFile, "platformTrustFile" ).and_then( [&]
            {
                return entry._httpClient->registerHostTLS( settings._profileURL, trustFile, "platformTrustFile" );
            } );
            if ( tlsReady.has_value() == false )
            {
                Error error{ tlsReady.error().kind(), tlsReady.error().message().data(), settings._name };
                error.appendDetail( ": " ).appendDetail( tlsReady.error().detail() );
                shutdown();
                return unexpected( error );
            }
            if ( settings._kind == PlatformLoginProviderKind::AppleSignIn )
            {
                // 교환 · 철회는 JWKS 와 다른 클라이언트로 한다(클라이언트의 응답은 소유한 쪽이 모두 회수한다).
                entry._secondHTTPClient        = make_unique<OwnedHTTPClient>( clock );
                const string         tokenURL  = settings._tokenURL.empty() ? string( AppleLoginProvider::kDefaultTokenURL ) : settings._tokenURL;
                const string         revokeURL = settings._revokeURL.empty() ? string( AppleLoginProvider::kDefaultRevokeURL ) : settings._revokeURL;
                const expected<void> secondReady =
                    entry._secondHTTPClient->initialize()
                        ? entry._secondHTTPClient->registerHostTLS( tokenURL, trustFile, "platformTrustFile" ).and_then( [&]
                {
                    return entry._secondHTTPClient->registerHostTLS( revokeURL, trustFile, "platformTrustFile" );
                } )
                        : expected<void>{ unexpected( Error{ ErrorKind::ExternalFailure, "cannot start the HTTP client" } ) };
                if ( secondReady.has_value() == false )
                {
                    Error error{ secondReady.error().kind(), secondReady.error().message().data(), settings._name };
                    error.appendDetail( ": " ).appendDetail( secondReady.error().detail() );
                    shutdown();
                    return unexpected( error );
                }
                entry._provider = AppleLoginProvider::create( settings, &game::getSubsystem<INetSecurityProvider>(), &entry._httpClient->get(), &entry._secondHTTPClient->get() );
            }
            else
                entry._provider = PlatformLoginProviderConfig::create( settings, &game::getSubsystem<INetSecurityProvider>(), &entry._httpClient->get() );
            if ( entry._provider == nullptr )
            {
                shutdown();
                return unexpected( Error{ ErrorKind::InvalidArgument, "the platform login provider has invalid settings", settings._name } );
            }
        }
        return {};
    }

    void PlatformLoginProviderSet::shutdown()
    {
        for ( Entry& entry : _listEntry )
        {
            if ( entry._httpClient != nullptr )
                entry._httpClient->shutdown(); // 날아가던 요청을 끝내고 TLS 컨텍스트를 놓는다
            if ( entry._secondHTTPClient != nullptr )
                entry._secondHTTPClient->shutdown();
        }
        _listEntry.clear();
    }
} // namespace sw
