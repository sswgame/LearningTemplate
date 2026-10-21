/**
 * @file OnlineServerKit.h
 * @brief 온라인 서비스 키트가 서버 조립(`OnlineServerRuntime`)에 붙는 진입점입니다. 키트는 `.cpp` 한 줄(`SW_REGISTER_ONLINE_SERVER_KIT`)로 자기 조립을 등록합니다.
 * @details - 키트끼리는 서로 모르므로(include 금지) 서버 조립도 키트 하나가 다른 키트를 부르지 않습니다. 런타임이 공통 기반(호스트, 저장소, 캐시, 버스, 예약 작업)을 만들어
 *            문맥(`OnlineServerKitContext`)에 넣고, 키트는 자기 담당을 만든 뒤 다른 키트가 쓸 기반 인터페이스(신원, 접속 상태, 차단, 원장 정책)를 문맥에 내놓습니다.
 *          - 단계는 네 개입니다. `initialize`(호스트를 띄우기 전 — 객체를 만들고 바인딩을 만들고 인터페이스를 내놓는다), `start`(호스트를 띄운 뒤 — 라우터와 다른 키트의
 *            인터페이스를 받아 로직을 시작한다), `stop`(호스트를 내리기 전 — 캐시 요청 취소, 구독 해제, 저장 일 회수), `shutdown`(호스트와 저장소를 내린 뒤 — 마지막 정리).
 *            언리얼 Online Services 의 인터페이스 조립, Nakama 런타임의 `InitModule` 등록에 해당합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Observability/ServiceFactoryList.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct OnlineServerConfig;
    struct OnlineServerKitEntry;

    class EphemeralStoreRouter;
    class IAccountBlockList;
    class IAccountDirectory;
    class IAccountNameIndex;
    class IAccountPresence;
    class IAccountSessionControl;
    class IAccountStatSource;
    class IBalanceNotice;
    class IEphemeralStore;
    class IGroupChannelMembership;
    class ILedgerPolicy;
    class ILiveEventControl;
    class IOfflinePushSender;
    class IOnlineHostService;
    class IServerDirectoryControl;
    class IServiceStore;
    class MetricRegistry;
    class OnlineServiceHost;
    class RemoteConfig;
    class ServerBus;
    class ServiceHealthRegistry;
    class ServiceScheduler;

    /** @brief 키트 시작 순서입니다. 작은 것부터 `initialize` · `start` 하고 반대로 `stop` · `shutdown` 합니다. 인터페이스를 내놓는 키트가 앞에 옵니다. */
    struct OnlineServerKitOrder
    {
        static constexpr int32 kIdentity    = 100; ///< 계정 — 신원, 접속 상태, 세션 끊기, 이름 찾기
        static constexpr int32 kEconomy     = 200; ///< 경제 — 원장 정책(화폐 상한)
        static constexpr int32 kSocial      = 300; ///< 친구 — 차단 목록
        static constexpr int32 kDirectory   = 400;
        static constexpr int32 kChat        = 500;
        static constexpr int32 kLeaderboard = 600;
        static constexpr int32 kMatchmaking = 700;
        static constexpr int32 kLiveOps     = 800;
        static constexpr int32 kMailbox     = 900;
        static constexpr int32 kTrade       = 1000;
        static constexpr int32 kAdmin       = 1100;
    };
} // namespace sw

namespace sw
{
    /** @brief 키트가 받는 서버 조립의 문맥입니다. 런타임이 채우는 칸과 키트가 내놓는 칸이 있습니다. 포인터는 모두 빌려 쓰며 키트보다 오래 삽니다. */
    struct OnlineServerKitContext
    {
        // 런타임이 채운다.
        OnlineServiceHost*        _pHost{ nullptr };
        IServiceStore*            _pStore{ nullptr };
        IEphemeralStore*          _pCache{ nullptr };
        ServerBus*                _pBus{ nullptr };    ///< 버스의 타입 있는 앞이다. 발행은 주제 정의(`ServerBusTopic`)로만 한다
        EphemeralStoreRouter*     _pRouter{ nullptr }; ///< 호스트를 띄운 뒤(`start`)부터 있다
        ServiceScheduler*         _pScheduler{ nullptr };
        RemoteConfig*             _pRemoteConfig{ nullptr };
        MetricRegistry*           _pMetricRegistry{ nullptr }; ///< 없으면 세지 않는다
        ServiceHealthRegistry*    _pHealthRegistry{ nullptr };
        const OnlineServerConfig* _pConfig{ nullptr };
        uint64                    _serverID{ 1 };
        int64                     _nowMs{ 0 }; ///< 시작 시각(UTC 밀리초). 테스트는 가짜 시각을 넣는다

        // 키트가 `initialize` 에서 내놓고 다른 키트가 `start` 에서 읽는다. 내놓는 키트가 없으면 null 이다.
        IAccountDirectory*        _pAccountDirectory{ nullptr }; ///< 계정 키트
        IAccountPresence*         _pPresence{ nullptr };         ///< 계정 키트
        const IAccountNameIndex*  _pNameIndex{ nullptr };        ///< 계정 키트
        IAccountSessionControl*   _pSessionControl{ nullptr };   ///< 계정 키트
        const IAccountBlockList*  _pBlockList{ nullptr };        ///< 친구 키트
        const ILedgerPolicy*      _pLedgerPolicy{ nullptr };     ///< 경제 키트(화폐 카탈로그)
        IBalanceNotice*           _pBalanceNotice{ nullptr };    ///< 경제 키트(요청 없이 바뀐 잔액을 계정에 알림)
        IServerDirectoryControl*  _pDirectoryControl{ nullptr }; ///< 서버 디렉터리 키트(점검과 공지 바꾸기)
        IGroupChannelMembership*  _pGroupChannels{ nullptr };    ///< 채팅 키트(길드와 파티 채널 멤버)
        const IAccountStatSource* _pStatSource{ nullptr };       ///< 순위표 키트(접속한 계정의 통계, 매칭 실력)
        IOfflinePushSender*       _pOfflinePush{ nullptr };      ///< 라이브 운영 키트(오프라인 계정의 기기 푸시)
        ILiveEventControl*        _pLiveEventControl{ nullptr }; ///< 라이브 운영 키트(GM 의 이벤트 운영)
    };
} // namespace sw

namespace sw
{
    /**
     * @class IOnlineServerKit
     * @brief 키트 하나의 서버 조립입니다. 모든 함수는 서비스 스레드(서버 루프)에서 불립니다.
     */
    class SW_GF_API IOnlineServerKit
    {
    public:
        IOnlineServerKit()          = default;
        virtual ~IOnlineServerKit() = default;

        IOnlineServerKit( const IOnlineServerKit& )            = delete;
        IOnlineServerKit& operator=( const IOnlineServerKit& ) = delete;

        /** @brief 호스트를 띄우기 전입니다. 로직과 바인딩을 만들고 `registerHostService` 로 올리며, 다른 키트가 쓸 인터페이스를 @p context 에 내놓습니다. */
        [[nodiscard]] virtual bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) = 0;
        /** @brief 호스트를 띄운 뒤입니다. 라우터와 다른 키트의 인터페이스로 로직을 시작합니다. 필요한 인터페이스가 없으면 false 와 까닭입니다. */
        [[nodiscard]] virtual bool start( OnlineServerKitContext& context, string& outError ) = 0;
        /** @brief 서버 루프마다 호스트 틱 뒤에 불립니다. 바인딩의 `onServiceTick` 이 하지 않는 일(서버 등록 하트비트 등)만 둡니다. */
        virtual void update( int64 nowMs ) { (void)nowMs; }
        /** @brief 호스트를 내리기 전입니다. 맡긴 캐시 요청을 취소하고, 구독을 풀고, 저장 일을 회수합니다. `start` 에 실패했어도 불립니다. */
        virtual void stop() = 0;
        /** @brief 호스트와 저장소를 종료하고 남은 완료를 돌린 뒤입니다. 저장소 완료가 부르는 로직(로그인 서비스 등)의 마지막 정리를 합니다. */
        virtual void shutdown() {}

    protected:
        /** @brief @p pService 를 문맥의 호스트에 등록합니다. 메서드 영역이 겹치면 false 와 키트 이름이 포함된 @p outError 입니다(겹친 번호는 호스트가 로그에 남긴다). */
        [[nodiscard]] static bool registerHostService( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, IOnlineHostService* pService, string& outError );
    };
} // namespace sw

namespace sw
{
    /** @brief 키트 조립 팩토리입니다. 등록기가 가진 정적 객체이며 이름과 순서는 노드에 있습니다. */
    struct OnlineServerKitFactory : ServiceFactoryNode
    {
        using CreateFunction = unique_ptr<IOnlineServerKit> ( * )();

        CreateFunction _pCreate{ nullptr };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct OnlineServerKitRegistry
     * @brief 키트 조립 팩토리의 레지스트리입니다. GameFramework 에 하나 있고 키트 모듈이 로드될 때 오릅니다.
     */
    struct SW_GF_API OnlineServerKitRegistry
    {
        static void registerFactory( OnlineServerKitFactory* pFactory );
        static void unregisterFactory( OnlineServerKitFactory* pFactory );
        /** @brief 순서대로(같으면 이름 순) @p outListFactory 에 담습니다. 비운 뒤 채웁니다. */
        static void collectFactories( vector<OnlineServerKitFactory*>& outListFactory );
        /** @brief 이름으로 찾습니다. 없으면 nullptr 입니다. */
        static OnlineServerKitFactory* findFactory( string_view name );
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineServerKitRegistrar
     * @brief 키트 조립 타입 하나를 정적 초기화 때 등록하고 모듈 언로드 때 해제합니다. `SW_REGISTER_ONLINE_SERVER_KIT` 로 씁니다.
     */
    template <typename TKit>
    class OnlineServerKitRegistrar
    {
    public:
        OnlineServerKitRegistrar( const utf8* pName, int32 order )
            : _factory{}
        {
            _factory._pName   = pName;
            _factory._order   = order;
            _factory._pCreate = &OnlineServerKitRegistrar::create;
            OnlineServerKitRegistry::registerFactory( &_factory );
        }
        ~OnlineServerKitRegistrar() { OnlineServerKitRegistry::unregisterFactory( &_factory ); }

        OnlineServerKitRegistrar( const OnlineServerKitRegistrar& )            = delete;
        OnlineServerKitRegistrar& operator=( const OnlineServerKitRegistrar& ) = delete;

    private:
        static unique_ptr<IOnlineServerKit> create() { return make_unique<TKit>(); }

        OnlineServerKitFactory _factory;
    };
} // namespace sw

/**
 * @brief 키트 조립 타입을 레지스트리에 등록합니다. 그 타입을 정의하는 `.cpp` 의 `namespace sw` 안, 익명 네임스페이스 밖에서 한 번 씁니다.
 * @param Type `IOnlineServerKit` 파생 타입
 * @param Name 설정 파일(`_listKit[]._name`)과 로그에 쓰는 이름(문자열 리터럴)
 * @param Order 시작 순서(`OnlineServerKitOrder::k*`)
 */
#define SW_REGISTER_ONLINE_SERVER_KIT( Type, Name, Order )                      \
    const ::sw::OnlineServerKitRegistrar<Type> s_onlineServerKitRegistrar##Type \
    {                                                                           \
        Name, Order                                                             \
    }
