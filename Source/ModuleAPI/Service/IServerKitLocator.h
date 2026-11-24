/**
 * @file ModuleAPI/Service/IServerKitLocator.h
 * @brief 전용 서버의 게임 모듈이 서버 키트 인터페이스(백필 · 저장소 …)를 받는 호스트 칸과, 받을 인터페이스를 적는 형식 목록 묶음입니다.
 * @details - 키트 인터페이스는 온라인 서버 조립(`OnlineServerRuntime`)의 지역 범위에 걸린다(`IOnlineServerKit::exposeInterface`). 조립은 서버 서비스라 모듈 리로드 · 종료마다
 *            멈췄다 다시 선다 — 게임 범위에 그 객체를 직접 걸면 매달린다. 그래서 호스트(Server)가 이 칸을 소유하고, 조립이 설 때 범위를 공개 · 내릴 때 거둔다.
 *          - 게임 모듈은 받을 인터페이스 **형식 목록 하나**를 적는다(`ServerKitDependencies<ServerKitRequired<…>, ServerKitOptional<…>>`). 의존 키트 이름은 그 형식들의
 *            제공 키트 상수(`kProviderKit`)에서 나오고, 꺼내기는 그 묶음(`get` 필수 · `find` 선택)으로만 한다 — 범위는 게임 모듈에 보이지 않아(`findKitScope` 비공개)
 *            선언하지 않은 형식을 찾을 자리가 없다. 온라인 서버 키트의 `OnlineServerKit<OnlineKitRequired<…>, …>` 와 같은 꼴이다.
 *          - 기동(게임 서버를 세울 때)에 `verify` 로 필수 인터페이스를 확인해 없으면 오류로 멈춘다. 부를 때마다 `resolve` 로 묶음을 다시 받는다(들고 있지 않는다).
 *          `IModuleStateStore` 와 같은 자리다. 언리얼의 `IOnlineSubsystem::Get()` 을 서버 모듈에서 부르는 것과 같은 쓰임이다. App(클라이언트)에는 칸이 없다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/SubsystemScope.h"

#include <tuple>
#include <type_traits>

namespace sw
{
    template <class TRequired, class TOptional>
    class ServerKitDependencies;

    /** @brief 서버 키트 범위의 창구입니다. 메인 스레드(서버 루프)에서만 씁니다. 범위는 형식 목록 묶음(`ServerKitDependencies`)만 봅니다. */
    class IServerKitLocator
    {
    public:
        virtual ~IServerKitLocator() = default;

        /**
         * @brief 서버 키트 조립이 내려가기(리로드 · 종료) 직전의 알림입니다. 그 안에서는 묶음을 아직 받을 수 있습니다 — 저장할 것을 여기서 맡기고 끝까지 기다립니다.
         * @details 조립이 내려간 뒤의 저장은 갈 곳이 없다(게임 서버의 종료는 조립보다 늦다). 구독이 사라지면 풀린다.
         */
        [[nodiscard]] virtual Subscription subscribeBeforeWithdraw( const Delegate<void()>& onBeforeWithdraw ) = 0;

    private:
        /** @brief 지금 서 있는 서버 키트 조립의 인터페이스 범위입니다. 조립이 서 있지 않으면(리로드 · 종료 중, 온라인 설정 없음) nullptr 입니다. */
        virtual const ISubsystemLocator* findKitScope() const = 0;

    private:
        // 게임 모듈이 범위에서 아무 형식이나 찾던 길(`findServerKit<T>`)을 접근 제어로 막는다 — 선언한 형식 목록의 묶음만 범위를 본다.
        template <class, class>
        friend class ServerKitDependencies;
    };

    /** @brief 게임 모듈이 필수로 받는 서버 키트 인터페이스 형식 목록입니다. 조립이 섰는데 제공 키트(`T::kProviderKit`)가 걸지 않았으면 기동 오류입니다. */
    template <class... TInterface>
    struct ServerKitRequired
    {
    };
    /** @brief 게임 모듈이 선택으로 받는 서버 키트 인터페이스 형식 목록입니다. 제공 키트가 없으면 null 로 받습니다. */
    template <class... TInterface>
    struct ServerKitOptional
    {
    };

    namespace ServerKitDependenciesInternal
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
    } // namespace ServerKitDependenciesInternal

    /**
     * @class ServerKitDependencies
     * @brief 게임 모듈이 받을 서버 키트 인터페이스 형식 목록 하나와, 그 목록에서 찾은 묶음입니다. 선언하지 않은 형식은 꺼낼 수 없습니다(`static_assert`).
     * @details 묶음의 포인터는 받은 호출 안에서만 쓴다 — 조립은 리로드 · 종료마다 다시 선다.
     */
    template <class... TRequired, class... TOptional>
    class ServerKitDependencies<ServerKitRequired<TRequired...>, ServerKitOptional<TOptional...>>
    {
    public:
        /** @brief 필수 형식 목록의 제공 키트 이름입니다. 같은 이름이 거듭될 수 있습니다. */
        static span<const utf8* const> getRequiredKits() { return ServerKitDependenciesInternal::ProviderKitList<TRequired...>::get(); }
        /** @brief 선택 형식 목록의 제공 키트 이름입니다. */
        static span<const utf8* const> getOptionalKits() { return ServerKitDependenciesInternal::ProviderKitList<TOptional...>::get(); }

        /**
         * @brief 기동 확인입니다. 조립이 섰는데 필수 인터페이스가 없으면 대상이 그 제공 키트 이름인 오류입니다(게임 서버를 세우지 않는다).
         * @details 조립이 서 있지 않으면(온라인 설정 없음 · 리로드 중) 확인할 것이 없어 성공이다 — 그동안 `resolve` 가 비어 있음을 돌려준다.
         */
        [[nodiscard]] static expected<void> verify( const IServerKitLocator* pLocator )
        {
            const ISubsystemLocator* const pScope = findScope( pLocator );
            if ( pScope == nullptr )
                return {};
            const utf8* const pMissingKit = findMissingKit( *pScope );
            if ( pMissingKit != nullptr )
                return unexpected( Error{ ErrorKind::NotFound, "the game module needs a server kit interface its provider kit did not expose", pMissingKit } );
            return {};
        }

        /** @brief 지금 서 있는 조립에서 묶음을 받습니다. 조립이 서 있지 않거나 필수 인터페이스가 없으면(기동 확인을 거친 뒤에는 생기지 않는다) 비어 있습니다. */
        [[nodiscard]] static optional<ServerKitDependencies> resolve( const IServerKitLocator* pLocator )
        {
            const ISubsystemLocator* const pScope = findScope( pLocator );
            if ( pScope == nullptr || findMissingKit( *pScope ) != nullptr )
                return nullopt;
            ServerKitDependencies dependencies;
            dependencies._required = std::tuple<TRequired*...>{ findInLocator<TRequired>( *pScope )... };
            dependencies._optional = std::tuple<TOptional*...>{ findInLocator<TOptional>( *pScope )... };
            return dependencies;
        }

        /** @brief 필수 인터페이스 @p T 입니다. 묶음이 있으면 늘 있습니다. */
        template <class T>
        T& get() const
        {
            static_assert( ServerKitDependenciesInternal::kIsListed<T, TRequired...>, "ServerKitDependencies::get needs T in the ServerKitRequired list" );
            return *std::get<T*>( _required );
        }

        /** @brief 선택 인터페이스 @p T 입니다. 제공 키트가 없으면 null 입니다. */
        template <class T>
        T* find() const
        {
            static_assert( ServerKitDependenciesInternal::kIsListed<T, TOptional...>, "ServerKitDependencies::find needs T in the ServerKitOptional list" );
            return std::get<T*>( _optional );
        }

    private:
        ServerKitDependencies() = default;

        static const ISubsystemLocator* findScope( const IServerKitLocator* pLocator ) { return pLocator != nullptr ? pLocator->findKitScope() : nullptr; }

        /** @brief 범위에 걸리지 않은 첫 필수 인터페이스의 제공 키트 이름입니다. 모두 있으면 nullptr 입니다. */
        static const utf8* findMissingKit( const ISubsystemLocator& scope )
        {
            (void)scope; // 필수 목록이 비면 폴드 식이 범위를 쓰지 않는다
            const utf8* pMissingKit = nullptr;
            ( ( pMissingKit = ( pMissingKit == nullptr && findInLocator<TRequired>( scope ) == nullptr ) ? std::remove_const_t<TRequired>::kProviderKit : pMissingKit ), ... );
            return pMissingKit;
        }

    private:
        std::tuple<TRequired*...> _required{};
        std::tuple<TOptional*...> _optional{};
    };
} // namespace sw
