/**
 * @file PlatformLoginProviderSet.h
 * @brief 설정 목록(`PlatformLoginProviderConfig`)에서 외부 로그인 제공자를 만들고, 제공자마다 쓸 HTTP 클라이언트와 TLS 컨텍스트를 소유합니다.
 * @details - 제공자는 응답을 자기만 회수하므로(`PlatformLoginProviderFactory::create`) 제공자마다 HTTP 클라이언트 하나를 둡니다.
 *            애플 로그인(`AppleSignIn`)은 JWKS 용과 교환 · 철회 용 둘을 둡니다.
 *          - `https://` 주소(JWKS, 프로필 API)의 호스트마다 클라이언트 TLS 컨텍스트를 만듭니다. 서버 체인은 @p trustFile(CA 번들 PEM)로 검증하고 이름도 봅니다.
 *            `https://` 주소가 있는데 신뢰 파일이 없으면 오류입니다. 개발용 인증서로 조용히 바꾸면 실제 제공자에 닿지 못한 채 기동이 끝나기 때문입니다.
 *          - 계정 키트 서버 조립(`AccountOnlineServerKit`)이 옵션 `platformProvidersFile` 과 `platformTrustFile` 로 이 객체를 채우고 로그인 서비스에 등록합니다.
 *            로그인 서비스를 내린 뒤에 `shutdown` 해야 합니다(서비스가 제공자를 빌려 씁니다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/Clock.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProvider.h"
#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/PlatformLoginProviderConfig.h"

namespace sw
{
    class OwnedHTTPClient;

    /**
     * @class PlatformLoginProviderSet
     * @brief 서버 하나의 외부 로그인 제공자 그룹입니다. 서비스 스레드에서만 씁니다.
     */
    class SW_KIT_SERVER_API PlatformLoginProviderSet
    {
    public:
        PlatformLoginProviderSet();
        ~PlatformLoginProviderSet();

        PlatformLoginProviderSet( const PlatformLoginProviderSet& )            = delete;
        PlatformLoginProviderSet& operator=( const PlatformLoginProviderSet& ) = delete;

        /** @brief 설정마다 HTTP 클라이언트, TLS 컨텍스트, 제공자를 만듭니다. 하나라도 실패하면 만든 것을 모두 버리고 false 와 @p outError 입니다. */
        [[nodiscard]] bool initialize( const vector<PlatformLoginProviderConfig>& listSettings, string_view trustFile, const IClock& clock, string& outError );
        /** @brief HTTP 클라이언트를 먼저 종료하고 제공자와 컨텍스트를 버립니다. */
        void shutdown();

        int32                   getProviderCount() const { return static_cast<int32>( _listEntry.size() ); }
        IPlatformLoginProvider* getProvider( int32 index ) const { return _listEntry[static_cast<size_t>( index )]._provider.get(); }

    private:
        struct Entry
        {
            unique_ptr<OwnedHTTPClient>        _httpClient{};       ///< TLS 컨텍스트와 함께 소유한다
            unique_ptr<OwnedHTTPClient>        _secondHTTPClient{}; ///< 애플 로그인의 교환 · 철회용. 다른 방식은 비어 있다
            unique_ptr<IPlatformLoginProvider> _provider{};         ///< HTTP 클라이언트를 빌려 쓰므로 먼저 소멸한다(나중 선언)
        };

        vector<Entry> _listEntry;
    };
} // namespace sw
