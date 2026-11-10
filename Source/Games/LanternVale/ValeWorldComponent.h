/**
 * @file ValeWorldComponent.h
 * @brief 씬의 골짜기 오브젝트입니다 — 게임 네트워크 세션의 역할을 보고 서버면 `ValeServerLink`, 클라이언트면 `ValeClientLink` 를 세웁니다.
 * @details 역할은 실행 인자 · 서버 설정이 정한다(`GameNetSession`). 전용 서버(`Server -game=LanternVale -server-config=…`)가 골짜기를 돌리고,
 *          클라이언트는 로그인 뒤 게임 접속 티켓으로 붙는다(`net.ticketconnect` — 서버는 티켓의 계정으로 진행을 읽는다). 세션이 없으면(혼자 띄운 App) 규칙만 확인하고 쉰다.
 *          진행 저장소는 서버 사용자 데이터 폴더 아래 `_profileFolder` 의 파일이다. 두 프로세스 시나리오는 서버의 사용자 데이터 폴더(`SW_USER_DATA_ROOT`)를 실행마다 빈 폴더로
 *          옮긴다(한 번의 서버 실행 안에서 저장 → 재접속 읽기는 그대로 지나고, 실행마다 새로 시작한다 — `py -3 -m Scripts net-pair mmo`).
 *          개발 명령 `vale.*` 과 감시 값 `LanternVale.*` 가 시나리오의 손이다(`game/lanternvale/automation/`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/StreamingCellGrid.h"

#include "Games/LanternVale/ValeNetLink.h"
#include "Games/LanternVale/ValeWorld.h"

namespace sw
{
    /**
     * @class ValeWorldComponent
     * @brief 골짜기 하나의 규칙 데이터와 망 잇기를 듭니다.
     */
    REFLECT( Category = "LanternVale", DisplayName = "Vale World", Tooltip = "Runs the vale on the dedicated server and shows the server's progress on a client" )
    class ValeWorldComponent : public Component
    {
    public:
        REFLECT_BODY();

        ValeWorldComponent();
        ~ValeWorldComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 규칙 데이터가 골짜기를 세울 수 있으면 1 입니다(세션과 상관없이 시작할 때 본다). */
        int32 getRulesValid() const { return _bRulesValid != SW_FALSE ? 1 : 0; }
        /** @brief 0 혼자 · 1 서버 · 2 클라이언트입니다. */
        int32 getNetRoleValue() const;

        const ValeRules&      getRules() const { return _rules; }
        ValeServerLink&       getServer() { return _server; }
        const ValeServerLink& getServer() const { return _server; }
        ValeClientLink&       getClient() { return _client; }
        const ValeClientLink& getClient() const { return _client; }

    private:
        /** @brief 씬의 넷 링크가 걸려 있으면 역할에 맞는 링크를 한 번 세웁니다. */
        void               tryStartLink();
        [[nodiscard]] bool startServer( INetPeers& peers, NetMessageRouter& router );

        PROPERTY( Category = "Vale", DisplayName = "Rules", Tooltip = "Combat, gathering, growth and party numbers of the vale" )
        ValeRules _rules;
        PROPERTY( Category = "Vale", DisplayName = "Interest Grid", Tooltip = "Cells the MMO replication uses to find what each player sees" )
        StreamingCellGridDef _interestGrid;
        PROPERTY( Required, Category = "Vale", DisplayName = "Profile Folder", Tooltip = "Server profile slots under the server's user data folder" )
        string _profileFolder;

        ValeServerLink _server;
        ValeClientLink _client;
        uint8          _bRulesValid;
        uint8          _bLinkTried; ///< 걸린 넷 링크로 링크를 세워 봤다(실패해도 다시 하지 않는다 — 오류는 한 번 알린다)
    };
} // namespace sw
