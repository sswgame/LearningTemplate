/**
 * @file OnlineServerKit.h
 * @brief 온라인 서비스 키트가 서버 조립(`OnlineServerRuntime`)에 붙는 진입점입니다. 키트는 `.cpp` 한 줄(`SW_REGISTER_ONLINE_SERVER_KIT`)로 자기 조립을 등록합니다.
 * @details - 키트끼리는 서로 모르므로(include 금지) 서버 조립도 키트 하나가 다른 키트를 부르지 않습니다. 런타임이 공통 기반(호스트, 저장소, 캐시, 버스, 예약 작업)을 만들어
 *            문맥(`OnlineServerKitContext`)에 넣고, 키트는 자기 담당을 만든 뒤 다른 키트가 쓸 기반 인터페이스(신원, 접속 상태, 차단, 원장 정책)를 문맥의 지역 범위
 *            (`_kits`, `SubsystemScope`)에 타입으로 겁니다(`exposeInterface`). 받는 키트는 `get`(필수 — 없으면 멈춤) · `find`(선택)로 찾습니다.
 *          - 순서는 숫자가 아니라 이름 의존입니다. 키트는 인터페이스를 받는 키트의 이름을 `getRequiredKits`(설정에 없으면 기동 오류) · `getOptionalKits`
 *            (설정에 있을 때만 먼저 선다)에 적고, 런타임이 키트를 지역 `SubsystemCollection` 노드로 두어 위상 정렬합니다(준비된 것이 여럿이면 이름 순).
 *          - 단계는 네 개입니다. `initialize`(호스트를 띄우기 전 — 객체를 만들고 바인딩을 등록하고 인터페이스를 내놓는다), `start`(모든 키트의 `initialize` 와
 *            호스트 기동 뒤 — 라우터와 다른 키트의 인터페이스로 로직을 시작한다), `stop`(호스트를 내리기 전 — 캐시 요청 취소, 구독 해제, 저장 일 회수),
 *            `shutdown`(호스트와 저장소를 내린 뒤 — 마지막 정리). `stop` · `shutdown` 은 `initialize` 에 성공한 키트만 역순으로 받습니다.
 *            언리얼 Online Services 의 인터페이스 조립, Nakama 런타임의 `InitModule` 등록에 해당합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Time/TimePoint.h"

#include "Engine/Observability/ServiceFactoryList.h"

#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct OnlineServerConfig;
    struct OnlineServerKitEntry;

    class EphemeralStoreRouter;
    class IClock;
    class IEphemeralStore;
    class IOnlineHostService;
    class IServiceStore;
    class MetricRegistry;
    class OnlineServiceHost;
    class RemoteConfig;
    class ServerBus;
    class ServiceHealthRegistry;
    class ServiceScheduler;

    /**
     * @brief 키트 이름입니다. 설정 파일(`_listKit[]._name`) · 로그 · 의존 칸이 같은 상수를 써서, 의존 칸의 철자가 틀리면 컴파일 오류입니다.
     * @details 키트끼리는 include 하지 않으므로 이름만 기반에 둡니다(언리얼 서브시스템의 클래스 의존과 같은 자리).
     */
    struct OnlineServerKitName
    {
        static constexpr const utf8* kBase        = "onlineBase"; ///< 런타임의 공통 기반(저장소 · 캐시 · 호스트). 모든 키트가 이것 뒤에 선다
        static constexpr const utf8* kAccount     = "account";    ///< 신원, 접속 상태, 세션 끊기, 이름 찾기
        static constexpr const utf8* kGameConnect = "gameConnect";
        static constexpr const utf8* kEconomy     = "economy"; ///< 원장 정책(화폐 상한), 잔액 변경 알림
        static constexpr const utf8* kSocial      = "social";  ///< 차단 목록
        static constexpr const utf8* kDirectory   = "directory";
        static constexpr const utf8* kChat        = "chat";
        static constexpr const utf8* kLeaderboard = "leaderboard";
        static constexpr const utf8* kMatchmaking = "matchmaking";
        static constexpr const utf8* kLiveOps     = "liveops";
        static constexpr const utf8* kMailbox     = "mailbox";
        static constexpr const utf8* kTrade       = "trade";
        static constexpr const utf8* kAdmin       = "admin";
    };
} // namespace sw

namespace sw
{
    /** @brief 키트가 받는 서버 조립의 문맥입니다. 런타임이 채우는 기반 칸과 키트끼리 인터페이스를 거는 지역 범위가 있습니다. 포인터는 모두 빌려 쓰며 키트보다 오래 삽니다. */
    struct OnlineServerKitContext
    {
        SubsystemScope& _kits;  ///< 키트가 `initialize` 에서 인터페이스를 걸고(`exposeInterface`) 다른 키트가 `get` · `find` 로 찾는 지역 범위다
        const IClock&   _clock; ///< 시한 · 백오프를 재는 단조 시계(외부 HTTP 클라이언트가 받는다). 요청 시각(`_nowMs`)과 형식이 달라 섞이지 않는다

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
        ServerDescriptor          _serverDescriptor{}; ///< 이 서버의 설명(id · 종류 · 지역 · 공개 주소 · 게임 포트와 수). 디렉터리 등록과 매칭 백필이 같은 값을 쓴다
        UnixMilliseconds          _nowMs{ 0 };         ///< 시작 시각. 테스트는 가짜 시각을 넣는다
    };
} // namespace sw

namespace sw
{
    /**
     * @class IOnlineServerKit
     * @brief 키트 하나의 서버 조립입니다. 모든 함수는 서비스 스레드(서버 루프)에서 불립니다.
     * @details - `initialize` 가 실패하면 그 안에서 만든 것을 스스로 되돌립니다 — `stop` · `shutdown` 을 받지 않습니다(`ISubsystem` 과 같은 계약).
     *          - `initialize` 에 성공한 키트는 다른 키트나 호스트가 실패해도 `stop` · `shutdown` 을 받습니다. `start` 를 받지 못했거나 실패했으면 `isStarted()` 가
     *            false 이므로 `start` 에서 세운 것을 되돌리는 줄만 그것을 봅니다. `start` 가 실패하면 그 안에서 반쯤 세운 것은 `start` 가 되돌립니다.
     */
    class SW_GF_API IOnlineServerKit
    {
    public:
        IOnlineServerKit()
            : _bStarted{ false }
        {
        }
        virtual ~IOnlineServerKit() = default;

        IOnlineServerKit( const IOnlineServerKit& )            = delete;
        IOnlineServerKit& operator=( const IOnlineServerKit& ) = delete;

        /** @brief 호스트를 띄우기 전입니다. 로직과 바인딩을 만들고 `registerHostService` 로 올리며, 다른 키트가 쓸 인터페이스를 `exposeInterface` 로 겁니다. */
        [[nodiscard]] virtual bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) = 0;
        /** @brief 모든 키트의 `initialize` 와 호스트 기동 뒤입니다. 라우터와 다른 키트의 인터페이스로 로직을 시작합니다. */
        [[nodiscard]] virtual bool start( OnlineServerKitContext& context, string& outError ) = 0;
        /** @brief 서버 루프마다 호스트 틱 뒤에 불립니다. 바인딩의 `onServiceTick` 이 하지 않는 일(서버 등록 하트비트 등)만 둡니다. */
        virtual void update( UnixMilliseconds nowMs ) { (void)nowMs; }
        /** @brief 호스트를 내리기 전입니다. 맡긴 캐시 요청을 취소하고, 구독을 풀고, 저장 일을 회수합니다. */
        virtual void stop() = 0;
        /** @brief 호스트와 저장소를 종료하고 남은 완료를 돌린 뒤입니다. 저장소 완료가 부르는 로직(로그인 서비스 등)의 마지막 정리를 합니다. */
        virtual void shutdown() {}

        /** @brief 인터페이스를 `get` 으로 받는 키트 이름(`OnlineServerKitName`)입니다. 설정에 없으면 기동 오류이고, 있으면 이 키트보다 먼저 섭니다. */
        virtual span<const utf8* const> getRequiredKits() const { return {}; }
        /** @brief 인터페이스를 `find` 로 받는 키트 이름입니다. 설정에 있을 때만 이 키트보다 먼저 섭니다. */
        virtual span<const utf8* const> getOptionalKits() const { return {}; }

        /** @brief 런타임이 `start` 단계에 부릅니다. 성공하면 `isStarted()` 가 true 가 됩니다. */
        [[nodiscard]] bool startKit( OnlineServerKitContext& context, string& outError )
        {
            _bStarted = start( context, outError );
            return _bStarted;
        }

    protected:
        /** @brief `start` 에 성공했는지입니다. `stop` · `shutdown` 에서 `start` 가 세운 것을 되돌릴 때 봅니다. */
        bool isStarted() const { return _bStarted; }

        /** @brief @p pService 를 문맥의 호스트에 등록합니다. 메서드 영역이 겹치면 false 와 키트 이름이 포함된 @p outError 입니다(겹친 번호는 호스트가 로그에 남긴다). */
        [[nodiscard]] static bool registerHostService( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, IOnlineHostService* pService, string& outError );

        /**
         * @brief @p object 를 인터페이스 @p TInterface 로 문맥의 지역 범위에 겁니다. 다른 키트가 이미 같은 인터페이스를 걸었으면 false 와 까닭입니다.
         * @details 건 칸은 범위와 함께 사라집니다 — 범위는 키트를 소유한 런타임 부품이라 키트보다 먼저 사라지지 않고, 찾는 쪽은 `start` 에서만 찾습니다.
         */
        template <class TInterface>
        [[nodiscard]] static bool exposeInterface( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, TInterface& object, string& outError )
        {
            if ( context._kits.bind<TInterface>( &object ) )
                return true;
            reportInterfaceTaken( entry, outError );
            return false;
        }

    private:
        static void reportInterfaceTaken( const OnlineServerKitEntry& entry, string& outError );

        bool _bStarted;
    };
} // namespace sw

namespace sw
{
    /** @brief 키트 조립 팩토리입니다. 등록기가 가진 정적 객체이며 이름은 노드에 있습니다(순서는 쓰지 않는다 — 이름 의존이 정한다). */
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
        /** @brief 이름 순으로 @p outListFactory 에 담습니다. 비운 뒤 채웁니다. */
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
        explicit OnlineServerKitRegistrar( const utf8* pName )
            : _factory{}
        {
            _factory._pName   = pName;
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
 * @param Name 설정 파일(`_listKit[]._name`)과 로그 · 의존 칸에 쓰는 이름(`OnlineServerKitName::k*`)
 */
#define SW_REGISTER_ONLINE_SERVER_KIT( Type, Name )                             \
    const ::sw::OnlineServerKitRegistrar<Type> s_onlineServerKitRegistrar##Type \
    {                                                                           \
        Name                                                                    \
    }
