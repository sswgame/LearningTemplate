/**
 * @file OnlineClientRegistry.h
 * @brief 키트 클라이언트의 정적 등록입니다. 모듈이 올라오면 클라이언트 팩토리가 오르고, 게임 세션(`OnlineSession`)이 올라온 키트의 클라이언트를 만들어 묶습니다.
 * @details 키트는 클라이언트 `.cpp` 에 `SW_REGISTER_ONLINE_CLIENT( XClient );` 한 줄을 둡니다. 게임은 키트를 매니페스트 의존으로 싣고 `session.get<XClient>()` 만 부르면 되며,
 *          등록을 빠뜨려 응답이 안 오는 길이 없습니다. 서버 키트의 `SW_REGISTER_ONLINE_SERVER_KIT` 와 같은 모양이고, 언리얼 Online Services 의 인터페이스 등록 자리입니다(기억에 의존).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Registry/ServiceFactoryList.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IOnlineClientService;
    class OnlineServiceClient;

    /** @brief 세션이 클라이언트를 만들 때 넘기는 값입니다. */
    struct OnlineSessionInfo
    {
        string _gameBuild{}; ///< 게임 빌드 글(`GameSettings::_onlineGameBuild`)
        string _platform{};  ///< 플랫폼 글(`[a-z0-9_]`)
    };

    /** @brief 키트 클라이언트 팩토리입니다. 등록기가 가진 정적 객체이며 이름은 노드에 있습니다. */
    struct OnlineClientFactory : ServiceFactoryNode
    {
        using CreateFunction = unique_ptr<IOnlineClientService> ( * )( OnlineServiceClient& client, const OnlineSessionInfo& info );

        CreateFunction _pCreate{ nullptr };
        uint64         _typeHash{ 0 }; ///< `OnlineSession::find<T>` 가 찾는 형식 값
    };
} // namespace sw

namespace sw
{
    /** @struct OnlineClientRegistry @brief 키트 클라이언트 팩토리의 레지스트리입니다. GameFramework 에 하나 있습니다. */
    struct SW_GF_API OnlineClientRegistry
    {
        static void registerFactory( OnlineClientFactory* pFactory );
        static void unregisterFactory( OnlineClientFactory* pFactory );
        /** @brief 이름 순으로 @p outListFactory 에 담습니다. 비운 뒤 채웁니다. */
        static void collectFactories( vector<OnlineClientFactory*>& outListFactory );
    };
} // namespace sw

namespace sw
{
    /**
     * @class OnlineClientRegistrar
     * @brief 키트 클라이언트 타입 하나를 정적 초기화 때 등록하고 모듈 언로드 때 해제합니다. `SW_REGISTER_ONLINE_CLIENT` 로 씁니다.
     */
    template <typename TClient>
    class OnlineClientRegistrar
    {
    public:
        explicit OnlineClientRegistrar( const utf8* pName )
            : OnlineClientRegistrar( pName, &OnlineClientRegistrar::create )
        {
        }
        /** @param pCreate `initialize( &client )` 하나로 묶이지 않는 클라이언트(계정 — 빌드 글을 받는다)의 만듦 함수입니다. */
        OnlineClientRegistrar( const utf8* pName, OnlineClientFactory::CreateFunction pCreate )
            : _factory{}
        {
            _factory._pName    = pName;
            _factory._pCreate  = pCreate;
            _factory._typeHash = getSubsystemTypeHash<TClient>();
            OnlineClientRegistry::registerFactory( &_factory );
        }
        ~OnlineClientRegistrar() { OnlineClientRegistry::unregisterFactory( &_factory ); }

        OnlineClientRegistrar( const OnlineClientRegistrar& )            = delete;
        OnlineClientRegistrar& operator=( const OnlineClientRegistrar& ) = delete;

    private:
        static unique_ptr<IOnlineClientService> create( OnlineServiceClient& client, const OnlineSessionInfo& info )
        {
            (void)info;
            unique_ptr<TClient> pClient = make_unique<TClient>();
            pClient->initialize( &client );
            return pClient;
        }

        OnlineClientFactory _factory;
    };
} // namespace sw

/** @brief 키트 클라이언트 타입을 등록합니다. 그 타입을 정의하는 `.cpp` 의 `namespace sw` 안에서 한 번 씁니다. 등록 전에 타입이 완전해야 하므로 클라이언트 헤더를 include 한 뒤에 씁니다. */
#define SW_REGISTER_ONLINE_CLIENT( Type )                                 \
    const ::sw::OnlineClientRegistrar<Type> s_onlineClientRegistrar##Type \
    {                                                                     \
        #Type                                                             \
    }

/** @brief `SW_REGISTER_ONLINE_CLIENT` 와 같고 만듦 함수 @p pCreate 를 받습니다. */
#define SW_REGISTER_ONLINE_CLIENT_WITH( Type, pCreate )                   \
    const ::sw::OnlineClientRegistrar<Type> s_onlineClientRegistrar##Type \
    {                                                                     \
        #Type, pCreate                                                    \
    }
