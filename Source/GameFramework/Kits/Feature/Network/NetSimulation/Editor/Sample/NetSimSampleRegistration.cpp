#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/Sample/NetSimSampleGame.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 이 확장 모듈이 로드된 동안 본보기 게임을 한 프로세스 재생의 게임 목록에 등록합니다. 실제 게임은 자기 모듈에서 같은 줄로 등록한다. */
        const NetSimGameRegistrar s_netSimSampleRegistrar{ NetSimSampleGame::kName, &NetSimSampleGame::create };
    } // namespace
} // namespace sw::editor
