/**
 * @file PushProviderSet.h
 * @brief 설정 목록(`PushProviderConfig`)에서 실제 푸시 제공자를 만들고, 제공자마다 쓸 HTTP 클라이언트와 TLS 컨텍스트를 소유합니다(OnlineKits G4).
 * @details - 설정 파일은 `{"providers":[{"kind":"fcm","serviceAccountEnvironment":"<변수>","tokenUrl":"…","sendUrl":"…"}]}` 이다. 자격(서비스 계정 키)은 파일에 쓰지 않고
 *            환경 변수에서 읽는다. 변수가 없거나 서비스 계정이 틀리면 **서버 기동이 실패한다**(가짜로 대신 뜨지 않는다). 설정 파일이 없으면 실제 제공자를 만들지 않는다.
 *          - `tokenUrl` · `sendUrl` 은 비우면 제공자의 공식 주소이고, 가짜 서버(테스트)를 가리킬 때만 쓴다. `https://` 주소가 있는데 신뢰 파일(CA 번들 PEM)이 없으면 오류다.
 *          - 제공자는 응답을 자기만 회수하므로 제공자마다 HTTP 클라이언트 하나를 둔다. 발송기를 내린 뒤에 `shutdown` 해야 한다(발송기가 제공자를 빌려 쓴다).
 *          제품 이름(`fcm`)은 이 설정의 `kind` 값과 `Provider/Fcm/` 안에만 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationProvider.h"

namespace sw
{
    class OwnedHTTPClient;

    /** @brief 푸시 제공자 하나의 설정입니다. */
    struct PushProviderConfig
    {
        string _kind{};                      ///< 제공자 종류(`fcm`)
        string _serviceAccountEnvironment{}; ///< 서비스 계정 JSON 을 담은 환경 변수 이름(키는 설정 파일에 쓰지 않는다)
        string _tokenURL{};                  ///< 비우면 서비스 계정의 `token_uri`
        string _sendBaseURL{};               ///< 비우면 제공자의 공식 주소
    };
} // namespace sw

namespace sw
{
    /**
     * @class PushProviderSet
     * @brief 서버 하나의 실제 푸시 제공자 그룹입니다. 서비스 스레드에서만 씁니다.
     */
    class SW_KIT_SERVER_API PushProviderSet
    {
    public:
        PushProviderSet();
        ~PushProviderSet();

        PushProviderSet( const PushProviderSet& )            = delete;
        PushProviderSet& operator=( const PushProviderSet& ) = delete;

        /**
         * @brief `{"providers":[…]}` 를 읽습니다. 모르는 키, 모르는 종류, 빠진 필수 키는 오류입니다 — 철자가 틀린 설정이 조용히 기본값이 되지 않게 한다.
         */
        [[nodiscard]] static bool readSettings( string_view jsonText, vector<PushProviderConfig>& outListSettings, string& outError );

        /** @brief 설정마다 비밀 읽기, HTTP 클라이언트, TLS 컨텍스트, 제공자를 만듭니다. 하나라도 실패하면 만든 것을 모두 버리고 false 와 @p outError 입니다. */
        [[nodiscard]] bool initialize( const vector<PushProviderConfig>& listSettings, string_view trustFile, string& outError );
        /** @brief HTTP 클라이언트를 먼저 종료하고 제공자와 컨텍스트를 버립니다. */
        void shutdown();

        int32                      getProviderCount() const { return static_cast<int32>( _listEntry.size() ); }
        IPushNotificationProvider* getProvider( int32 index ) const { return _listEntry[static_cast<size_t>( index )]._provider.get(); }

    private:
        struct Entry
        {
            unique_ptr<OwnedHTTPClient>           _httpClient{}; ///< TLS 컨텍스트와 함께 소유한다
            unique_ptr<IPushNotificationProvider> _provider{};   ///< HTTP 클라이언트를 빌려 쓰므로 먼저 소멸한다(나중 선언)
        };

        vector<Entry> _listEntry;
    };
} // namespace sw
