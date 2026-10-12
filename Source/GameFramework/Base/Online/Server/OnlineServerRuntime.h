/**
 * @file OnlineServerRuntime.h
 * @brief 온라인 서비스 서버 조립입니다. 저장소, 캐시, 버스, 예약 작업, 원격 설정, 서비스 호스트를 만들고 올라온 키트(`IOnlineServerKit`)를 설정대로 붙입니다.
 * @details - 전용 서버는 서버 서비스(`OnlineServerService`)로 이것을 띄웁니다. 부하 테스트 봇의 `--local-server` 와 끝단 테스트도 같은 조립을 씁니다.
 *          - 시작 순서는 저장소와 캐시 → 버스 → 키트 `initialize`(바인딩을 올린다) → 호스트(전송, TLS) → 버스를 라우터에 묶음 → 키트 `start` → 서버 등록입니다.
 *            종료는 서버 등록 → 키트 `stop` → 호스트 → 예약 작업 → 저장소(남은 완료를 돌린다) → 키트 `shutdown` → 캐시 순입니다. 온라인 기반의 종료 계약과 같습니다.
 *          - 버스는 캐시 위 버스(`EphemeralServerBus`)이고 캐시 앞은 호스트의 라우터로 서비스와 나눠 씁니다. 메모리 캐시이면 이 프로세스 안에서만 돕니다(서버 한 대).
 *          - 서비스 스레드 하나(`tick` 을 부르는 스레드)에서 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/NetTypes.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct OnlineServerKitContext;

    class IStreamTransport;
    class MemoryEphemeralDatabase;
    class MetricRegistry;
    class OnlineServiceHost;
    class ServiceHealthRegistry;

    /** @brief 저장소나 캐시 하나의 드라이버 항목입니다. 비밀은 이미 환경 변수에서 읽은 값입니다. */
    struct OnlineServerStoreSettings
    {
        string _driver{ "memory" };
        string _connection{}; ///< 저장소의 접속 문자열 · 캐시의 엔드포인트
        string _secret{};     ///< 런타임이 드라이버에 넘긴 뒤 비운다
    };
} // namespace sw

namespace sw
{
    /** @brief 런타임 설정입니다. 전용 서버는 서버 설정과 온라인 설정에서 채우고, 테스트와 봇은 직접 채웁니다. */
    struct OnlineServerRuntimeSettings
    {
        OnlineServerConfig        _config{};
        OnlineServerStoreSettings _store{};
        OnlineServerStoreSettings _cache{};
        NetAddress                _listenAddress{ NetAddress::makeLoopback( 0 ) }; ///< 포트 0 = 아무 포트(`getListenPort`)
        string                    _tlsCertificateFile{};                           ///< `_bTLS` 일 때. 둘 다 비면 Dev 만 개발용 인증서
        string                    _tlsPrivateKeyFile{};
        string                    _tlsPrivateKeyPassphrase{}; ///< 런타임이 쓰고 나면 비운다
        IStreamTransport*         _pTransport{ nullptr };     ///< 빌려 쓴다. null 이면 플랫폼 전송을 만든다
        MetricRegistry*           _pMetricRegistry{ nullptr };
        ServiceHealthRegistry*    _pHealthRegistry{ nullptr };
        uint16                    _gamePort{ 0 };                ///< 디렉터리에 알릴 게임(UDP) 포트
        uint8                     _bTLS{ SW_TRUE };              ///< 서비스 스트림을 TLS 1.3 으로 받는다(테스트와 봇은 끈다)
        uint8                     _bManualCacheTime{ SW_FALSE }; ///< 메모리 캐시의 만료를 `tick` 의 시각으로 잰다(가짜 시각 테스트, 봇)
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineServerRuntime
     * @brief 온라인 서비스 서버 하나입니다.
     */
    class SW_GF_API OnlineServerRuntime
    {
    public:
        static constexpr const utf8* kHealthCheckName = "online_services";

        OnlineServerRuntime();
        ~OnlineServerRuntime();

        OnlineServerRuntime( const OnlineServerRuntime& )            = delete;
        OnlineServerRuntime& operator=( const OnlineServerRuntime& ) = delete;

        /** @brief 조립하고 받기 시작합니다. 실패하면 false 와 @p outError 이며, 만든 것은 `shutdown` 이 정리합니다. @p nowMs 는 서비스 시각(UTC 밀리초)입니다. */
        [[nodiscard]] bool initialize( OnlineServerRuntimeSettings settings, int64 nowMs, string& outError );
        /** @brief 만든 역순으로 내립니다. 두 번 불러도 됩니다. */
        void shutdown();
        /** @brief 호스트, 예약 작업, 키트, 서버 등록 하트비트를 돌립니다. */
        void tick( int64 nowMs );

        bool               isInitialized() const { return _bInitialized == SW_TRUE; }
        OnlineServiceHost& getHost();
        uint16             getListenPort() const;
        int32              getConnectionCount() const;
        /** @brief 붙은 키트 이름입니다(시작 순서). */
        void collectKitNames( vector<string>& outListName ) const;
        /** @brief 키트가 내놓은 인터페이스와 기반이 든 문맥입니다(테스트와 진단). */
        const OnlineServerKitContext& getContext() const;
        /** @brief 메모리 캐시이면 그 데이터베이스입니다(가짜 시각을 넣는 테스트). 아니면 nullptr 입니다. */
        MemoryEphemeralDatabase* findMemoryCacheDatabase();

    private:
        struct Parts;

        [[nodiscard]] bool createStores( OnlineServerRuntimeSettings& settings, int64 nowMs, string& outError );
        [[nodiscard]] bool createKits( const OnlineServerRuntimeSettings& settings, string& outError );
        [[nodiscard]] bool startHost( OnlineServerRuntimeSettings& settings, string& outError );
        [[nodiscard]] bool registerServer( const OnlineServerRuntimeSettings& settings, string& outError );

        unique_ptr<Parts> _parts;
        uint8             _bInitialized;
    };
} // namespace sw
