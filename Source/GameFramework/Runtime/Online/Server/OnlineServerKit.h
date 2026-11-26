/**
 * @file OnlineServerKit.h
 * @brief 온라인 서비스 키트가 서버 조립(`OnlineServerRuntime`)에 붙는 진입점입니다. 키트는 `.cpp` 한 줄(`SW_REGISTER_ONLINE_SERVER_KIT`)로 자기 조립을 등록합니다.
 * @details - 키트끼리는 서로 모르므로(include 금지) 서버 조립도 키트 하나가 다른 키트를 부르지 않습니다. 런타임이 공통 기반(호스트, 저장소, 캐시, 버스, 예약 작업)을 만들어
 *            문맥(`OnlineServerKitContext`)에 넣고, 키트는 자기 담당을 만든 뒤 다른 키트가 쓸 기반 인터페이스(신원, 접속 상태, 차단, 원장 정책)를 문맥의 지역 범위에
 *            타입으로 겁니다(`exposeInterface`).
 *          - 받는 쪽은 받을 인터페이스 **형식 목록 하나**만 적습니다(`OnlineServerKit<OnlineKitRequired<…>, OnlineKitOptional<…>, OnlineKitUnordered<…>>`).
 *            의존 키트 이름은 그 형식들의 제공 키트 상수(`kProviderKit`)에서 폴드 식으로 나오고, 꺼내기는 그 목록 묶음(`Dependencies` · `UnorderedDependencies`)으로만
 *            합니다 — 지역 범위는 키트에 보이지 않아(`OnlineServerKitContext::_kits` 가 비공개) 이름 목록과 찾기가 어긋날 자리가 없습니다.
 *          - 순서는 숫자가 아니라 이름 의존입니다. 필수(설정에 없으면 기동 오류) · 선택(설정에 있을 때만 먼저 선다)의 제공 키트가 이 키트보다 먼저 서도록 런타임이 키트를
 *            지역 `SubsystemCollection` 노드로 두어 위상 정렬합니다(준비된 것이 여럿이면 이름 순). 순서 없는 목록(`OnlineKitUnordered`)은 의존에 넣지 않습니다 —
 *            서로 찾는 두 키트(친구 ↔ 채팅)가 순환을 만들지 않게, 모든 키트의 `initialize` 뒤인 `start` 에서만 받습니다.
 *          - 단계는 네 개입니다. `initialize`(호스트를 띄우기 전 — 객체를 만들고 바인딩을 등록하고 인터페이스를 내놓는다), `start`(모든 키트의 `initialize` 와
 *            호스트 기동 뒤 — 라우터와 다른 키트의 인터페이스로 로직을 시작한다), `stop`(호스트를 내리기 전 — 캐시 요청 취소, 구독 해제, 저장 일 회수),
 *            `shutdown`(호스트와 저장소를 내린 뒤 — 마지막 정리). `stop` · `shutdown` 은 `initialize` 에 성공한 키트만 역순으로 받습니다.
 *            언리얼 Online Services 의 인터페이스 조립, Nakama 런타임의 `InitModule` 등록에 해당합니다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Registry/FactoryRegistrationList.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Time/TimePoint.h"

#include "Engine/Server/ServerServiceRegistry.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Directory/ServerRecord.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Service/OnlineServerKitName.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceTTLRule.h"

#include <tuple>
#include <type_traits>

namespace sw
{
    struct OnlineServerConfig;

    class EphemeralStoreRouter;
    class IAfterDeleteAccount;
    class IClock;
    class IEphemeralStore;
    class IOnlineServerKit;
    class IOnlineServerService;
    class ServiceStore;
    class MetricRegistry;
    class OnlineServiceHost;
    class RemoteConfig;
    class ServerBus;
    class ServiceHealthRegistry;
    class ServiceScheduler;

    template <class TRequired, class TOptional, class TUnordered>
    class OnlineServerKit;
} // namespace sw

namespace sw
{
    /** @brief 키트가 받는 서버 조립의 문맥입니다. 런타임이 채우는 기반 칸과 키트끼리 인터페이스를 거는 지역 범위가 있습니다. 포인터는 모두 빌려 쓰며 키트보다 오래 삽니다. */
    struct OnlineServerKitContext
    {
        // 키트는 지역 범위를 직접 보지 못한다 — 거는 것은 `IOnlineServerKit::exposeInterface`, 받는 것은 선언한 형식 목록의 묶음(`OnlineServerKit::Dependencies`)뿐이다.
        // 이름 목록과 `find<T>` 를 따로 적어 어긋나도 컴파일되던 꼴을 접근 제어로 막는다.
        friend class IOnlineServerKit;
        template <class, class, class>
        friend class OnlineServerKit;

        const IClock& _clock; ///< 시한 · 백오프를 재는 단조 시계(외부 HTTP 클라이언트가 받는다). 요청 시각(`_nowMs`)과 형식이 달라 섞이지 않는다

        // 런타임이 채운다.
        OnlineServiceHost*        _pHost{ nullptr };
        ServiceStore*             _pStore{ nullptr };
        IEphemeralStore*          _pCache{ nullptr };
        ServerBus*                _pBus{ nullptr };    ///< 버스의 타입 있는 앞이다. 발행은 주제 정의(`ServerBusTopic`)로만 한다
        EphemeralStoreRouter*     _pRouter{ nullptr }; ///< 호스트를 띄운 뒤(`start`)부터 있다
        ServiceScheduler*         _pScheduler{ nullptr };
        RemoteConfig*             _pRemoteConfig{ nullptr };
        MetricRegistry*           _pMetricRegistry{ nullptr }; ///< 없으면 세지 않는다
        ServiceHealthRegistry*    _pHealthRegistry{ nullptr };
        const OnlineServerConfig* _pConfig{ nullptr };
        uint64                    _serverID{ 1 };
        ServerDescriptor          _serverDescriptor{}; ///< 이 서버의 설명(id · 종류 · 지역 · 공개 주소 · 게임 포트와 수). 디렉터리 등록과 매칭 백필이 같은 값을 쓴다. 매칭 키트가 정원을 게임이 정한 자리 수로 바꾸면 런타임이 다음 갱신에 등록한다
        UnixMilliseconds          _nowMs{ 0 };         ///< 시작 시각. 테스트는 가짜 시각을 넣는다

        // 키트가 `initialize` 에서 채운다(`IOnlineServerKit::addTTLRule`). 런타임이 키트를 모두 띄운 뒤 기반 규칙과 함께 보존 쓸기에 넘긴다.
        vector<ServiceTTLRule> _listTTLRule{};
        // 런타임이 키트의 `initialize` 가 성공할 때마다 `getAfterDeleteAccount` 를 모은다. 계정 키트가 `start` 에서 탈퇴 정리에 넘긴다. 키트가 소유한다.
        vector<const IAfterDeleteAccount*> _listAfterDeleteAccount{};

        OnlineServerKitContext( SubsystemScope& kits, const IClock& clock )
            : _clock{ clock }
            , _kits{ kits }
        {
        }

        OnlineServerKitContext( const OnlineServerKitContext& )            = delete;
        OnlineServerKitContext& operator=( const OnlineServerKitContext& ) = delete;

    private:
        SubsystemScope& _kits; ///< 키트가 `initialize` 에서 인터페이스를 거는 지역 범위다(게임 모듈 창구와 테스트는 `OnlineServerRuntime::getKitScope` 로 본다)
    };
} // namespace sw

namespace sw
{
    /**
     * @class IOnlineServerKit
     * @brief 키트 하나의 서버 조립입니다. 모든 함수는 서비스 스레드(서버 루프)에서 불립니다. 키트는 이것을 직접 상속하지 못하고 `OnlineServerKit<…>` 을 상속합니다.
     * @details - `initialize` 가 실패하면 그 안에서 만든 것을 스스로 되돌립니다 — `stop` · `shutdown` 을 받지 않습니다(`Subsystem` 과 같은 계약).
     *          - `initialize` 에 성공한 키트는 다른 키트나 호스트가 실패해도 `stop` · `shutdown` 을 받습니다. `start` 를 받지 못했거나 실패했으면 `isStarted()` 가
     *            false 이므로 `start` 에서 세운 것을 되돌리는 줄만 그것을 봅니다. `start` 가 실패하면 그 안에서 반쯤 세운 것은 `start` 가 되돌립니다.
     */
    class SW_GF_API IOnlineServerKit
    {
    public:
        virtual ~IOnlineServerKit() = default;

        IOnlineServerKit( const IOnlineServerKit& )            = delete;
        IOnlineServerKit& operator=( const IOnlineServerKit& ) = delete;

        /** @brief 서버 루프마다 호스트 틱 뒤에 불립니다. 바인딩의 `onServiceTick` 이 하지 않는 일(서버 등록 하트비트 등)만 둡니다. */
        virtual void update( UnixMilliseconds nowMs ) { (void)nowMs; }
        /** @brief 호스트를 내리기 전입니다. 맡긴 캐시 요청을 취소하고, 구독을 풀고, 저장 일을 회수합니다. */
        virtual void stop() = 0;
        /** @brief 호스트와 저장소를 종료하고 남은 완료를 돌린 뒤입니다. 저장소 완료가 부르는 로직(로그인 서비스 등)의 마지막 정리를 합니다. */
        virtual void shutdown() {}

        /**
         * @brief 종료 드레인이 끝나기를 기다려야 하는 이 키트의 진행 중인 일 수입니다(진행 중인 매치 · 거래 · 저장). 키트별 종료 API 의 한 칸 — 접속은 호스트가 센다.
         * @details 기본 0(기다릴 일이 없다). 서버 루프가 접속 수와 함께 0 이 될 때까지(기한 안에서) 틱을 돌리므로, 키트는 일이 끝나는 대로 줄이기만 하면 된다.
         */
        virtual int32 getOpenJobCount() const { return 0; }
        /** @brief 종료 드레인이 시작될 때 한 번 불립니다. 새 일을 받지 않고 진행 중인 일을 마무리하며 접속한 클라이언트에 사유를 알립니다(`ServerShutdownNotice`). 기본은 아무 일도 하지 않습니다. */
        virtual void beginShutdown( const ServerShutdownNotice& notification ) { (void)notification; }

        /**
         * @brief 탈퇴한 계정의 이 키트 레코드를 지우는 훅입니다. 계정 id 를 키나 값으로 두는 표가 없으면 nullptr 를 반환합니다.
         * @details 순수 가상이다 — 키트를 새로 만들면 탈퇴 정리를 정하지 않고는 컴파일되지 않는다. 훅은 키트보다 짧게 쓰인다(저장소 일이 키트 `shutdown` 전에 회수된다).
         */
        virtual const IAfterDeleteAccount* getAfterDeleteAccount() const = 0;

        /** @brief 필수 형식 목록의 제공 키트 이름입니다(`OnlineServerKit` 이 형식 목록에서 만든다). 설정에 없으면 기동 오류이고, 있으면 이 키트보다 먼저 섭니다. 같은 이름이 거듭될 수 있습니다. */
        virtual span<const utf8* const> getRequiredKits() const = 0;
        /** @brief 선택 형식 목록의 제공 키트 이름입니다. 설정에 있을 때만 이 키트보다 먼저 섭니다. */
        virtual span<const utf8* const> getOptionalKits() const = 0;
        /** @brief 선택 형식 목록 가운데 서버 여러 대(`OnlineServerTopology::MultiServer`)에서는 필수인 인터페이스의 제공 키트 이름입니다. 그 구성의 설정에 없으면 기동 오류입니다. */
        virtual span<const utf8* const> getMultiServerKits() const = 0;

        /** @brief 런타임이 `initialize` 단계에 부릅니다. 형식 목록의 필수 · 선택 인터페이스를 찾아 묶은 뒤 키트의 `initialize` 를 부릅니다. */
        [[nodiscard]] expected<void> initializeKit( OnlineServerKitContext& context, const OnlineServerKitEntry& entry ) { return resolveAndInitialize( context, entry ); }

        /** @brief 런타임이 `start` 단계에 부릅니다. 순서 없는 목록을 찾아 묶은 뒤 키트의 `start` 를 부르고, 성공하면 `isStarted()` 가 true 가 됩니다. */
        [[nodiscard]] expected<void> startKit( OnlineServerKitContext& context )
        {
            expected<void> result = resolveAndStart( context );
            _bStarted             = result.has_value();
            return result;
        }

    protected:
        /** @brief `start` 에 성공했는지입니다. `stop` · `shutdown` 에서 `start` 가 세운 것을 되돌릴 때 봅니다. */
        bool isStarted() const { return _bStarted; }

        /** @brief @p pService 를 문맥의 호스트에 등록합니다. 메서드 영역이 겹치면 대상이 키트 이름인 오류입니다(겹친 번호는 호스트가 로그에 남긴다). */
        [[nodiscard]] static expected<void> registerServerService( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, IOnlineServerService* pService );

        /**
         * @brief @p object 를 인터페이스 @p TInterface 로 문맥의 지역 범위에 겁니다. 다른 키트가 이미 같은 인터페이스를 걸었으면 대상이 키트 이름인 오류입니다.
         * @details 건 칸은 범위와 함께 사라집니다 — 범위는 키트를 소유한 런타임 부품이라 키트보다 먼저 사라지지 않는다. 받는 쪽은 형식 목록 묶음으로만 받는다.
         */
        template <class TInterface>
        [[nodiscard]] static expected<void> exposeInterface( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, TInterface& object )
        {
            if ( context._kits.bind<TInterface>( &object ) )
                return {};
            return unexpected( Error{ ErrorKind::AlreadyExists, "the kit exposes an interface another kit already exposed", entry._name } );
        }

        /**
         * @brief 이 키트 테이블의 TTL 규칙 @p rule 에 설정 @p config 의 쓸기 상한을 넣어 문맥에 올립니다. 런타임이 모든 키트의 `initialize` 뒤에 기반 규칙(감사 · 원장 · 신고 · 보낸 우편)과 함께 매일 쓸기에 넘깁니다.
         * @details 기간은 키트 설정(Config JSON)에서 읽어 넣습니다. 규칙이 틀렸거나(기간 · 시각 읽기 · 색인이 없다) 같은 테이블 · 접두어 규칙이 이미 있으면 대상이 키트 이름인 오류입니다.
         *          테이블 주인이 보존을 정하는 자리다 — PlayFab 의 데이터 보존 정책, Nakama 의 저장소 TTL 과 같다.
         */
        [[nodiscard]] static expected<void> addTTLRule( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, ServiceTTLRule rule, const ServiceTTLConfig& config );

    private:
        IOnlineServerKit()
            : _bStarted{ SW_FALSE }
        {
        }

        [[nodiscard]] virtual expected<void> resolveAndInitialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry ) = 0;
        [[nodiscard]] virtual expected<void> resolveAndStart( OnlineServerKitContext& context )                                         = 0;

    private:
        uint8 _bStarted : 1;

        // 생성자가 비공개라 키트는 `OnlineServerKit<…>` 을 거쳐서만 선다 — 형식 목록을 적지 않은 키트는 컴파일되지 않는다.
        template <class, class, class>
        friend class OnlineServerKit;
    };
} // namespace sw

namespace sw
{
    /** @brief 필수로 받는 인터페이스 형식 목록입니다. 제공 키트(`T::kProviderKit`)가 설정에 없으면 기동 오류이고, 걸지 않았으면 이 키트의 `initialize` 가 오류로 거절됩니다. */
    template <class... TInterface>
    struct OnlineKitRequired
    {
    };
    /**
     * @brief 선택으로 받는 인터페이스 형식 목록입니다. 제공 키트가 설정에 있으면 먼저 서고, 없으면 null 로 받습니다.
     * @details 인터페이스가 `static constexpr bool kRequiredInMultiServer = true` 를 적으면 서버 여러 대 구성에서는 필수다 — 서버끼리 나누는 인터페이스라 없으면 서버마다 따로 돈다.
     *          받는 키트가 아니라 인터페이스가 적으므로 새로 받는 키트도 빠뜨릴 수 없다.
     */
    template <class... TInterface>
    struct OnlineKitOptional
    {
    };
    /** @brief 순서 없이 받는 인터페이스 형식 목록입니다. 의존에 넣지 않으며(서로 찾는 키트의 순환을 막는다) `start` 에서만 받습니다. 없으면 null 입니다. */
    template <class... TInterface>
    struct OnlineKitUnordered
    {
    };

    namespace OnlineServerKitInternal
    {
        template <class T, class... TList>
        inline constexpr bool kIsListed = ( std::is_same_v<T, TList> || ... );

        /** @brief 형식 목록의 제공 키트 이름 배열입니다. 빈 목록도 배열이 되도록 끝에 null 하나를 더 둔다(길이는 형식 수만 센다). */
        template <class... TInterface>
        struct ProviderKitList
        {
            static constexpr const utf8* kArrName[sizeof...( TInterface ) + 1] = { std::remove_const_t<TInterface>::kProviderKit..., nullptr };

            static span<const utf8* const> get() { return { kArrName, sizeof...( TInterface ) }; }
        };

        /** @brief 인터페이스 @p T 가 서버 여러 대에서 필수인지입니다(`T::kRequiredInMultiServer`). 적지 않은 인터페이스는 false 입니다. */
        template <class T, class = void>
        inline constexpr bool kIsRequiredInMultiServer = false;
        template <class T>
        inline constexpr bool kIsRequiredInMultiServer<T, std::void_t<decltype( T::kRequiredInMultiServer )>> = T::kRequiredInMultiServer;

        /** @brief 형식 목록에서 서버 여러 대에 필수인 것의 제공 키트 이름만 앞에서부터 담은 배열입니다. */
        template <size_t TCapacity>
        struct MultiServerKitNames
        {
            const utf8* _arrName[TCapacity]{};
            size_t      _count{ 0 };
        };

        template <class... TInterface>
        constexpr MultiServerKitNames<sizeof...( TInterface ) + 1> makeMultiServerKitNames()
        {
            MultiServerKitNames<sizeof...( TInterface ) + 1> names{};
            ( ( kIsRequiredInMultiServer<TInterface> ? (void)( names._arrName[names._count++] = std::remove_const_t<TInterface>::kProviderKit ) : (void)0 ), ... );
            return names;
        }

        template <class... TInterface>
        inline constexpr MultiServerKitNames<sizeof...( TInterface ) + 1> kMultiServerKitNames = makeMultiServerKitNames<TInterface...>();
    } // namespace OnlineServerKitInternal

    template <class TRequired, class TOptional>
    class OnlineKitDependencies;

    /**
     * @class OnlineKitDependencies
     * @brief 필수 · 선택 형식 목록에서 찾은 인터페이스 묶음입니다. 선언하지 않은 형식은 꺼낼 수 없습니다(`static_assert`).
     * @details 필수와 선택의 제공 키트는 이 키트보다 먼저 서므로 `initialize` 부터 쓸 수 있다. 묶음은 키트가 소유한 값이고 인터페이스는 지역 범위와 함께 산다.
     */
    template <class... TRequired, class... TOptional>
    class OnlineKitDependencies<OnlineKitRequired<TRequired...>, OnlineKitOptional<TOptional...>>
    {
    public:
        OnlineKitDependencies() = default;

        /** @brief 필수 인터페이스 @p T 입니다. 묶음이 선 뒤에는 늘 있습니다. */
        template <class T>
        T& get() const
        {
            static_assert( OnlineServerKitInternal::kIsListed<T, TRequired...>, "OnlineKitDependencies::get needs T in the kit's OnlineKitRequired list" );
            return *std::get<T*>( _required );
        }

        /** @brief 선택 인터페이스 @p T 입니다. 제공 키트가 설정에 없으면 null 입니다. */
        template <class T>
        T* find() const
        {
            static_assert( OnlineServerKitInternal::kIsListed<T, TOptional...>, "OnlineKitDependencies::find needs T in the kit's OnlineKitOptional list" );
            return std::get<T*>( _optional );
        }

    private:
        std::tuple<TRequired*...> _required{};
        std::tuple<TOptional*...> _optional{};

        template <class, class, class>
        friend class OnlineServerKit;
    };

    /**
     * @class OnlineKitUnorderedDependencies
     * @brief 순서 없는 형식 목록에서 찾은 인터페이스 묶음입니다. `start` 에만 넘어옵니다. 선언하지 않은 형식은 꺼낼 수 없습니다(`static_assert`).
     */
    template <class... TUnordered>
    class OnlineKitUnorderedDependencies
    {
    public:
        OnlineKitUnorderedDependencies() = default;

        /** @brief 순서 없는 인터페이스 @p T 입니다. 그 키트가 없으면 null 입니다. */
        template <class T>
        T* find() const
        {
            static_assert( OnlineServerKitInternal::kIsListed<T, TUnordered...>, "OnlineKitUnorderedDependencies::find needs T in the kit's OnlineKitUnordered list" );
            return std::get<T*>( _unordered );
        }

    private:
        std::tuple<TUnordered*...> _unordered{};

        template <class, class, class>
        friend class OnlineServerKit;
    };

    /**
     * @class OnlineServerKit
     * @brief 키트 조립의 기반입니다. 받을 인터페이스 형식 목록 하나에서 의존 키트 이름 · 찾기 · 꺼내기를 모두 만듭니다.
     * @details - 키트는 `initialize( context, entry, interfaces )` · `start( context, interfaces, unordered )` 를 구현합니다. 의존 이름을 손으로 적는 칸은 없다.
     *          - 필수 인터페이스를 제공 키트가 걸지 않았으면(이름만 같은 다른 조립) 키트의 `initialize` 를 부르지 않고 대상이 제공 키트 이름인 오류다.
     *          - 언리얼 `USubsystem::Initialize` 의 `Collection.InitializeDependency<T>()`(형식으로 의존을 적는다)와 같은 자리다. 위상 정렬은 이름 의존 그대로다.
     */
    template <class TRequired = OnlineKitRequired<>, class TOptional = OnlineKitOptional<>, class TUnordered = OnlineKitUnordered<>>
    class OnlineServerKit;

    template <class... TRequired, class... TOptional, class... TUnordered>
    class OnlineServerKit<OnlineKitRequired<TRequired...>, OnlineKitOptional<TOptional...>, OnlineKitUnordered<TUnordered...>> : public IOnlineServerKit
    {
    public:
        using Dependencies          = OnlineKitDependencies<OnlineKitRequired<TRequired...>, OnlineKitOptional<TOptional...>>;
        using UnorderedDependencies = OnlineKitUnorderedDependencies<TUnordered...>;

        span<const utf8* const> getRequiredKits() const final { return OnlineServerKitInternal::ProviderKitList<TRequired...>::get(); }
        span<const utf8* const> getOptionalKits() const final { return OnlineServerKitInternal::ProviderKitList<TOptional...>::get(); }
        span<const utf8* const> getMultiServerKits() const final
        {
            return { OnlineServerKitInternal::kMultiServerKitNames<TOptional...>._arrName, OnlineServerKitInternal::kMultiServerKitNames<TOptional...>._count };
        }

    protected:
        OnlineServerKit()
            : IOnlineServerKit{}
            , _dependencies{}
        {
        }

        /** @brief 호스트를 띄우기 전입니다. 로직과 바인딩을 만들고 `registerServerService` 로 올리며, 다른 키트가 쓸 인터페이스를 `exposeInterface` 로 겁니다. */
        [[nodiscard]] virtual expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, const Dependencies& interfaces ) = 0;
        /** @brief 모든 키트의 `initialize` 와 호스트 기동 뒤입니다. 라우터와 다른 키트의 인터페이스로 로직을 시작합니다. */
        [[nodiscard]] virtual expected<void> start( OnlineServerKitContext& context, const Dependencies& interfaces, const UnorderedDependencies& unordered ) = 0;

    private:
        [[nodiscard]] expected<void> resolveAndInitialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry ) final
        {
            // 필수 · 선택의 제공 키트는 이름 의존으로 먼저 섰다(그 `initialize` 에서 걸었다) — 지금 찾아도 `start` 에서 찾는 것과 같다.
            _dependencies._required = std::tuple<TRequired*...>{ context._kits.find<TRequired>()... };
            _dependencies._optional = std::tuple<TOptional*...>{ context._kits.find<TOptional>()... };
            const utf8* pMissingKit = nullptr;
            ( ( pMissingKit = ( pMissingKit == nullptr && std::get<TRequired*>( _dependencies._required ) == nullptr ) ? std::remove_const_t<TRequired>::kProviderKit : pMissingKit ), ... );
            if ( pMissingKit != nullptr )
            {
                Error error{ ErrorKind::NotFound, "the online kit needs an interface its provider kit did not expose", entry._name };
                error.appendDetail( " needs an interface of " ).appendDetail( pMissingKit );
                return unexpected( error );
            }
            if ( context._pConfig->_topology == OnlineServerTopology::MultiServer )
            {
                ( ( pMissingKit = ( pMissingKit == nullptr && OnlineServerKitInternal::kIsRequiredInMultiServer<TOptional> && std::get<TOptional*>( _dependencies._optional ) == nullptr )
                                    ? std::remove_const_t<TOptional>::kProviderKit
                                    : pMissingKit ),
                  ... );
                if ( pMissingKit != nullptr )
                {
                    Error error{ ErrorKind::NotFound, "the multi-server topology needs an interface its provider kit did not expose", entry._name };
                    error.appendDetail( " needs an interface of " ).appendDetail( pMissingKit );
                    return unexpected( error );
                }
            }
            return initialize( context, entry, _dependencies );
        }

        [[nodiscard]] expected<void> resolveAndStart( OnlineServerKitContext& context ) final
        {
            UnorderedDependencies unordered;
            unordered._unordered = std::tuple<TUnordered*...>{ context._kits.find<TUnordered>()... };
            return start( context, _dependencies, unordered );
        }

    private:
        Dependencies _dependencies; ///< `initialize` 앞에서 채우고 `start` 에도 같은 것을 넘긴다
    };
} // namespace sw

namespace sw
{
    /** @brief 키트 조립 팩토리입니다. 등록기가 가진 정적 객체이며 이름은 노드에 있습니다(순서는 쓰지 않는다 — 이름 의존이 정한다). */
    struct OnlineServerKitFactory : FactoryRegistration
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
        /** @brief 팩토리를 올립니다. 만들기 함수가 없거나 같은 이름이 있으면(오류 로그) 올리지 않고 false 입니다. */
        [[nodiscard]] static bool registerFactory( OnlineServerKitFactory* pFactory );
        static void               unregisterFactory( OnlineServerKitFactory* pFactory );
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
        explicit OnlineServerKitRegistrar( const utf8* pName, SourceLocation site = SourceLocation::current() )
            : _factory{ makeFactory( pName ) }
            , _registrar{ &_factory, &OnlineServerKitRegistry::registerFactory, &OnlineServerKitRegistry::unregisterFactory, site }
        {
        }

        OnlineServerKitRegistrar( const OnlineServerKitRegistrar& )            = delete;
        OnlineServerKitRegistrar& operator=( const OnlineServerKitRegistrar& ) = delete;

    private:
        static unique_ptr<IOnlineServerKit> create() { return make_unique<TKit>(); }
        static OnlineServerKitFactory       makeFactory( const utf8* pName )
        {
            OnlineServerKitFactory factory;
            factory._pName   = pName;
            factory._pCreate = &OnlineServerKitRegistrar::create;
            return factory;
        }

        OnlineServerKitFactory                   _factory;   ///< 목록에 오르는 줄(이 등록자와 수명이 같다)
        StaticRegistrar<OnlineServerKitFactory*> _registrar; ///< 줄 다음에 만들어 줄보다 먼저 사라진다
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
