#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderSet.h"

#include "GameFramework/Base/Online/HTTP/OwnedHTTPClient.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/Provider/Apple/AppleLoginProvider.h"

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

    bool PlatformLoginProviderSet::initialize( const vector<PlatformLoginProviderConfig>& listSettings, string_view trustFile, string& outError )
    {
        shutdown();
        for ( const PlatformLoginProviderConfig& settings : listSettings )
        {
            Entry& entry      = _listEntry.emplace_back();
            entry._httpClient = make_unique<OwnedHTTPClient>();
            if ( entry._httpClient->initialize() == false )
            {
                outError = "cannot start the HTTP client of platform login provider '" + settings._name + "'";
                shutdown();
                return false;
            }
            const bool bTLSReady = entry._httpClient->registerHostTLS( settings._jwksURL, trustFile, outError ) &&
                                   entry._httpClient->registerHostTLS( settings._profileURL, trustFile, outError );
            if ( bTLSReady == false )
            {
                outError = "platform login provider '" + settings._name + "': " + outError;
                shutdown();
                return false;
            }
            if ( settings._kind == PlatformLoginProviderKind::AppleSignIn )
            {
                // 교환 · 철회는 JWKS 와 다른 클라이언트로 한다(클라이언트의 응답은 소유한 쪽이 모두 회수한다).
                entry._secondHTTPClient   = make_unique<OwnedHTTPClient>();
                const string tokenURL     = settings._tokenURL.empty() ? string( AppleLoginProvider::kDefaultTokenURL ) : settings._tokenURL;
                const string revokeURL    = settings._revokeURL.empty() ? string( AppleLoginProvider::kDefaultRevokeURL ) : settings._revokeURL;
                const bool   bSecondReady = entry._secondHTTPClient->initialize() && entry._secondHTTPClient->registerHostTLS( tokenURL, trustFile, outError ) &&
                                          entry._secondHTTPClient->registerHostTLS( revokeURL, trustFile, outError );
                if ( bSecondReady == false )
                {
                    outError = "platform login provider '" + settings._name + "': " + ( outError.empty() ? string( "cannot start the HTTP client" ) : outError );
                    shutdown();
                    return false;
                }
                entry._provider = AppleLoginProvider::create( settings, &NetSecurity::getProvider(), &entry._httpClient->get(), &entry._secondHTTPClient->get() );
            }
            else
                entry._provider = PlatformLoginProviderFactory::create( settings, &NetSecurity::getProvider(), &entry._httpClient->get() );
            if ( entry._provider == nullptr )
            {
                outError = "platform login provider '" + settings._name + "' has invalid settings";
                shutdown();
                return false;
            }
        }
        return true;
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
