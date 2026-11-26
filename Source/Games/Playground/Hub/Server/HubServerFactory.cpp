/**
 * @file HubServerFactory.cpp
 * @brief 서버 링크를 만드는 모듈 창구입니다(`IHubServerFactory`). 씬 오브젝트는 이 창구로만 닿고 이름으로 부르는 곳이 없어 헤더를 두지 않습니다.
 * @details 진행 저장소는 전용 서버의 온라인 조립 저장소다 — 받을 서버 키트 인터페이스를 형식 목록 하나(`HubServerKits`)로 적고, 서버를 세울 때 확인한 뒤(없으면
 *          세우지 않는다) 맡길 때마다 그 묶음에서 받는다(조립은 리로드 · 종료마다 다시 서므로 들고 있지 않는다). 이 폴더(`Server/`)는 클라이언트 구성이 짓지 않는다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"

#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

#include "Games/Playground/Hub/Server/HubServerLink.h"

#include "ModuleAPI/Service/IServerKitLocator.h"

namespace sw
{
    /** @brief 허브 서버가 받는 서버 키트 인터페이스 형식 목록입니다. 진행 저장소는 필수다(조립이 섰는데 없으면 서버를 세우지 않는다). */
    using HubServerKits = ServerKitDependencies<ServerKitRequired<ServiceStore>, ServerKitOptional<>>;

    /** @class HubServerFactory @brief 서버 링크를 만드는 모듈 창구입니다(`SW_MODULE_PART`). 이름 해시가 함수 서명 글이라 `sw` 에 둔다. */
    class HubServerFactory final : public IHubServerFactory
    {
    public:
        static constexpr const utf8* kModulePartName = "HubServerFactory";

        unique_ptr<IHubServer> createServer( INetPeers& peers, NetMessageRouter& router, const HubRules& rules, const StreamingCellGridDef& gridDef ) override
        {
            const expected<void> verified = HubServerKits::verify( game::findSubsystem<IServerKitLocator>() );
            if ( verified.has_value() == false )
            {
                SW_LOG_ERROR_ID( "Hub.ServerKitMissing", "[Hub] the server was not started: %# (%#)", verified.error().message(), verified.error().detail() );
                return {};
            }
            unique_ptr<HubServerLink>   pServer = make_unique<HubServerLink>();
            const HubServiceStoreSource source  = SW_DELEGATE_LAMBDA( HubServiceStoreSource, []() -> ServiceStore*
             {
                const optional<HubServerKits> kits = HubServerKits::resolve( game::findSubsystem<IServerKitLocator>() );
                return kits.has_value() ? &kits->get<ServiceStore>() : nullptr;
            } );
            if ( pServer->initialize( peers, router, rules, gridDef, source ) == false )
                return {};
            if ( IServerKitLocator* const pLocator = game::findSubsystem<IServerKitLocator>() )
                pServer->holdBeforeWithdraw( pLocator->subscribeBeforeWithdraw( SW_DELEGATE_METHOD( Delegate<void()>, &HubServerLink::flushProfiles, pServer.get() ) ) );
            return pServer;
        }
    };

    SW_MODULE_PART( Engine, HubServerFactory, IHubServerFactory );
} // namespace sw
