/**
 * @file AbilityArenaGame.cpp
 * @brief 어빌리티 시스템 테스트 게임의 빈 게임 인스턴스입니다. 판은 Playground 미니게임 abilityarena 가 돌린다 — 이 게임은 다음 커밋이 지운다.
 */
#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 빈 게임 팩 */
    class AbilityArenaGame : public GameInstance
    {
    public:
        AbilityArenaGame()           = default;
        ~AbilityArenaGame() override = default;
    };
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::AbilityArenaGame );
