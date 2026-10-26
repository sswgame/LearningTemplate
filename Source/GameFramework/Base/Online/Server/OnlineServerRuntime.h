/**
 * @file OnlineServerRuntime.h
 * @brief 온라인 서비스 서버 조립입니다. 저장소, 캐시, 버스, 예약 작업, 원격 설정, 서비스 호스트를 만들고 로드된 키트(`IOnlineServerKit`)를 설정대로 연결합니다.
 * @details - 전용 서버는 서버 서비스(`OnlineServerService`)로 이것을 띄웁니다. 부하 테스트 봇의 `--local-server` 와 끝단 테스트도 같은 조립을 씁니다.
 *          - 공통 기반(저장소 · 캐시 · 버스 · 예약 작업 · 호스트)과 키트는 지역 `SubsystemScope` 의 노드입니다. 모든 키트가 기반 노드(`OnlineServerKitName::kBase`)에
 *            의존하고 키트끼리는 이름 의존(`IOnlineServerKit::getRequiredKits` · `getOptionalKits`)으로 정렬되어, 순서를 손으로 적는 곳이 없습니다.
 *          - 시작: 기반 `initialize`(저장소와 캐시, 버스) → 키트 `initialize`(바인딩을 등록하고 인터페이스를 건다) → 호스트(전송, TLS) → 버스를 라우터에 연결
 *            → 키트 `start`(초기화 순서) → 서버 등록입니다. 종료: 서버 등록 → 컬렉션 `shutdownAll` 이 키트 `stop`(역순) → 기반 stop(호스트 → 예약 작업 → 저장소와
 *            남은 완료) → 키트 `shutdown`(역순) → 기반 shutdown(캐시)을 돕니다. 온라인 기반의 종료 계약과 같습니다.
 *          - `start` 는 컬렉션의 단계가 아니라 런타임이 초기화 순서(`getInitializedIndices`)로 부릅니다 — 호스트 기동이 모든 키트의 `initialize` 와 `start` 사이에 있어서다.
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
    class MetricRegistry;
    class OnlineServiceHost;
    class ServiceHealthRegistry;

    /** @brief 저장소나 캐시 하나의 드라이버 항목입니다. 비밀은 이미 환경 변수에서 읽은 값입니다. */
    struct OnlineServerStoreConfig
    {
        string _driver{ "memory" };
        string _connection{}; ///< 저장소의 접속 문자열 · 캐시의 엔드포인트
        string _secret{};     ///< 런타임이 드라이버에 넘긴 뒤 비운다
    };
} // namespace sw

namespace sw
{
    /** @brief 런타임 설정입니다. 전용 서버는 서버 설정과 온라인 설정에서 채우고, 테스트와 봇은 직접 채웁니다. */
    struct OnlineServerRuntimeConfig
    {
        OnlineServerConfig      _config{};
        OnlineServerStoreConfig _store{};
        OnlineServerStoreConfig _cache{};
        NetAddress              _listenAddress{ NetAddress::makeLoopback( 0 ) };      ///< 포트 0 = 아무 포트(`getListenPort`)
        NetAddress              _adminListenAddress{ NetAddress::makeLoopback( 0 ) }; ///< `_bAdminListener` 일 때 GM 리슨이 받을 주소
        string                  _tlsCertificateFile{};                                ///< `_bTLS` 일 때. 둘 다 비면 Dev 만 개발용 인증서
        string                  _tlsPrivateKeyFile{};
        string                  _tlsPrivateKeyPassphrase{};  ///< 런타임이 쓰고 나면 비운다
        IStreamTransport*       _pTransport{ nullptr };      ///< 빌려 쓴다. null 이면 플랫폼 전송을 만든다
        IStreamTransport*       _pAdminTransport{ nullptr }; ///< `_bAdminListener` 일 때 GM 리슨의 전송. 빌려 쓴다. null 이면 플랫폼 전송을 만든다
        MetricRegistry*         _pMetricRegistry{ nullptr };
        ServiceHealthRegistry*  _pHealthRegistry{ nullptr };
        uint16                  _gamePort{ 0 };                           ///< 디렉터리에 알릴 게임(UDP) 포트(샤드 여럿이면 첫 포트)
        uint8                   _gamePortCount{ 1 };                      ///< 디렉터리에 알릴 게임 포트 수(`ServerDescriptor::_portCount` — 연이은 포트 샤드 수, 한 포트를 나누면 1)
        uint8                   _bTLS{ SW_TRUE };                         ///< 서비스 스트림을 TLS 1.3 으로 받는다(테스트와 봇은 끈다)
        uint8                   _bManualCacheTime{ SW_FALSE };            ///< 메모리 캐시의 만료를 `tick` 의 시각으로 측정한다(가짜 시각 테스트, 봇)
        uint8                   _bAdminListener{ SW_FALSE };              ///< GM 리슨을 따로 연다(`OnlineServiceExposure`). 끄면 서비스 리슨 하나가 GM 서비스도 받는다
        uint8                   _bReverseIndependentKitOrder{ SW_FALSE }; ///< 의존이 없는 키트끼리를 이름 역순으로 세운다(테스트 — 적지 않은 의존을 이름 순서가 가리는지 잡는다)
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
        [[nodiscard]] bool initialize( OnlineServerRuntimeConfig settings, int64 nowMs, string& outError );
        /** @brief 만든 역순으로 종료합니다. 두 번 불러도 됩니다. */
        void shutdown();
        /** @brief 호스트, 예약 작업, 키트, 서버 등록 하트비트를 돌립니다. */
        void update( int64 nowMs );

        bool               isInitialized() const { return _bInitialized == SW_TRUE; }
        OnlineServiceHost& getHost();
        uint16             getListenPort() const;
        int32              getConnectionCount() const;
        /** @brief 붙은 키트 이름입니다(시작 순서). */
        void collectKitNames( vector<string>& outListName ) const;
        /** @brief 키트가 내놓은 인터페이스와 기반을 가진 문맥입니다(테스트와 진단). */
        const OnlineServerKitContext& getContext() const;

    private:
        struct Parts;

        class BaseNode;

        [[nodiscard]] bool createStores( OnlineServerRuntimeConfig& settings, int64 nowMs, string& outError );
        /** @brief 설정의 키트를 만들어 노드로 더합니다(모르는 키트 · 두 번 적은 키트 · 설정에 없는 필수 키트는 오류). */
        [[nodiscard]] bool addKits( bool bReverseIndependentOrder, string& outError );
        [[nodiscard]] bool startHost( OnlineServerRuntimeConfig& settings, string& outError );
        [[nodiscard]] bool registerServer( string& outError );

        unique_ptr<Parts> _parts;
        uint8             _bInitialized;
    };
} // namespace sw
