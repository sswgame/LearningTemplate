/**
 * @file ValeServerFactory.cpp
 * @brief 서버 링크를 만드는 모듈 창구입니다(`IValeServerFactory`). 씬 오브젝트는 이 창구로만 닿고 이름으로 부르는 곳이 없어 헤더를 두지 않습니다.
 * @details 진행 저장소는 전용 서버의 온라인 조립 저장소다 — 게임 모듈이 서버 키트를 찾는 호스트 칸(`IServerKitLocator`)에서 맡길 때마다 찾는다(조립은 리로드 · 종료마다
 *          다시 서므로 들고 있지 않는다). 이 폴더(`Server/`)는 클라이언트 구성이 짓지 않으므로 클라이언트 구성에는 창구가 없다.
 */
#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Engine/Module/ModuleSubsystemRegistrar.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include "Games/LanternVale/Server/ValeServerLink.h"

#include "ModuleAPI/Service/IServerKitLocator.h"

namespace sw
{
    /** @class ValeServerFactory @brief 서버 링크를 만드는 모듈 창구입니다(`SW_MODULE_PART`). 이름 해시가 함수 서명 글이라 `sw` 에 둔다. */
    class ValeServerFactory final : public IValeServerFactory
    {
    public:
        static constexpr const utf8* kModulePartName = "ValeServerFactory";

        unique_ptr<IValeServer> createServer( INetPeers& peers, NetMessageRouter& router, const ValeRules& rules, const StreamingCellGridDef& gridDef ) override
        {
            unique_ptr<ValeServerLink>   pServer = make_unique<ValeServerLink>();
            const ValeServiceStoreSource source  = SW_DELEGATE_LAMBDA( ValeServiceStoreSource, []() -> IServiceStore*
             { return findServerKit<IServiceStore>( game::findSubsystem<IServerKitLocator>() ); } );
            if ( pServer->initialize( peers, router, rules, gridDef, source ) == false )
                return {};
            return pServer;
        }
    };

    SW_MODULE_PART( Engine, ValeServerFactory, IValeServerFactory );
} // namespace sw
