/**
 * @file MeadowVillageGame.cpp
 * @brief 키트 조립 테스트 게임 — 농장(GF_Farming) · 생물 마을(GF_CreatureLife) · 집(GF_Housing)을 한 씬 · 한 공유 상태에 섞습니다.
 * @details 카탈로그는 씬의 디렉터가 참조로 받는다. 이 클래스는 디렉터들을 상태 스냅숏에 올리기만 한다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/GameStateSnapshot.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 조립 테스트 게임 인스턴스입니다. */
    class MeadowVillageGame : public GameInstance
    {
    public:
        MeadowVillageGame();
        ~MeadowVillageGame() override;
    };
} // namespace sw

namespace sw
{
    MeadowVillageGame::MeadowVillageGame()
    {
        getStateSnapshot().registerGameMode<GameModeComponent>(); // 바탕 형식 하나로 디렉터가 모두 실린다
    }

    MeadowVillageGame::~MeadowVillageGame() = default;
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::MeadowVillageGame );
