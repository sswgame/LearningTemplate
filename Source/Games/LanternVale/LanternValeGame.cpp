/**
 * @file LanternValeGame.cpp
 * @brief 작은 액션 MMO 수직 슬라이스 LanternVale 의 게임 인스턴스입니다 — 등불 골짜기의 그림자 짐승을 치고 빛버섯을 캐며 자라는 서버 권위 골짜기입니다.
 * @details 실행: 전용 서버 `Server -game=LanternVale -server-config=<설정>`(게임 접속 티켓 서버 — `_bGameConnectTicket`), 클라이언트 `App -game=LanternVale` 에서
 *          `net.ticketconnect <로그인 주소> <계정> <비밀번호> <서버 이름> <게임 주소>` 로 붙는다. 두 프로세스 확인은 `py -3 -m Scripts net-pair mmo` 가 한다.
 *          씬(`game/lanternvale/maps/vale.scene.xml`)의 `Vale` 오브젝트(`ValeWorldComponent`)가 세션 역할을 보고 서버 · 클라이언트 링크를 세운다.
 *          진행은 서버가 계정마다 저장하므로 이 인스턴스는 상태 스냅숏에 올릴 것이 없다(클라이언트 세이브가 진행의 원본이 아니다).
 */
#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief LanternVale 게임 인스턴스입니다. */
    class LanternValeGame : public GameInstance
    {
    public:
        LanternValeGame()           = default;
        ~LanternValeGame() override = default;

    protected:
        bool onInitialize() override
        {
            (void)getSubsystem<GameSceneFlow>().requestFirstScene(); // 실패는 함수가 알리고, 핫 리로드로 다시 선 인스턴스는 살아 있는 씬을 쓴다
            return true;
        }
    };
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::LanternValeGame );
