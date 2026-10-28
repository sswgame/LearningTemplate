#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"
#include "GameFramework/Plugin/Editor/NetSimulation/Sample/NetSimSampleGame.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 이 확장 모듈이 로드된 동안 본보기 게임(`NetSimSampleGame`)을 한 프로세스 재생의 게임 목록에 등록합니다.
         * @details 목록의 항목은 모듈이 언로드되기 전에 해제해야 한다(`NetSimGameRegistry` 머리말). 등록과 해제를 한 객체의 생성자 · 소멸자에 두어 한쪽만 하는 경로가 없다.
         *          실제 게임은 자기 모듈에서 같은 구조로 등록한다.
         */
        struct NetSimSampleRegistrar
        {
            NetSimSampleRegistrar() { (void)NetSimGameRegistry::registerGame( _entry ); } // 실패(이름 없음 · 중복)는 등록이 오류로 남긴다
            ~NetSimSampleRegistrar() { (void)NetSimGameRegistry::unregisterGame( _entry ); }

            NetSimGameEntry _entry{ NetSimSampleGame::kName, &NetSimSampleGame::create };
        };

        const NetSimSampleRegistrar s_netSimSampleRegistrar;
    } // namespace
} // namespace sw::editor
