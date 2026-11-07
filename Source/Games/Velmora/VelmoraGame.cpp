/**
 * @file VelmoraGame.cpp
 * @brief 벨모라 골짜기 — 오픈월드 싱글 액션 어드벤처의 작은 수직 슬라이스입니다(`docs/DesignBacklog.md` "오픈월드 액션 RPG 키트" 싱글플레이 줄).
 * @details 지역 진입 → 탐험(봉화) · 전투(홀 · 보스) → 보상 · 퀘스트 → 저장 · 다시 불러오기, 그리고 확장 팩(DLC, 모드 `velmora_tidecaves`)을 켜 기존 세이브를 잇는다.
 *          새 키트를 만들지 않고 기존 키트를 조합한다 — 몸(`AdventureVitals`, GF_ActionAdventure), 지역 전투(`ActionRoom` · `MonsterCatalog`, GF_ActionCombat),
 *          확장 팩(`ModSession` 의 덮어쓰기 층, GF_ModSupport), 판 상태(퀘스트 일지 · 깃발 · 지갑)와 자동 저장 · 세이브 슬롯(GameFramework).
 *          실행: `App.exe -game=Velmora`, 확장 팩과 함께: `App.exe -game=Velmora -gv_modList=velmora_tidecaves`.
 */
#include "pch.h"

#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/GameConfig.h"

#include "GameFramework/Kits/Feature/Content/ModSupport/ModSession.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Catalog/MonsterCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

#include "Games/Velmora/VelmoraGameModeComponent.h"
#include "Games/Velmora/VelmoraRegionCatalog.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 벨모라 골짜기 게임 인스턴스입니다. */
    class VelmoraGame : public GameInstance
    {
    public:
        VelmoraGame();
        ~VelmoraGame() override;

    protected:
        bool onInitialize() override;

    private:
        /**
         * @brief 이 세션의 확장 팩 · 모드(`-gv_modList`)입니다. 카탈로그를 읽기 전에 적용한다 — 확장 팩은 지역 · 퀘스트 · 몬스터 표에 층으로 항목을 더하므로(`<Layer>`)
         *        먼저 읽은 표가 층 없는 내용을 쥐지 않게. 요청한 팩을 올리지 못하면 게임을 띄우지 않는다(요청과 다른 구성으로 조용히 돌지 않는다).
         */
        unique_ptr<ModSession>                 _mods;
        shared_ptr<const VelmoraRegionCatalog> _pRegionCatalog;
        shared_ptr<const QuestCatalog>         _pQuestCatalog;
        shared_ptr<const MonsterCatalog>       _pMonsterCatalog;
    };
} // namespace sw

namespace sw
{
    VelmoraGame::VelmoraGame()
        : _mods{ nullptr }
        , _pRegionCatalog{ nullptr }
        , _pQuestCatalog{ nullptr }
        , _pMonsterCatalog{ nullptr }
    {
        // 판 상태(퀘스트 일지 · 깃발 · 지갑)는 판 상태 컴포넌트가, 몸 · 자리 · 일어설 지역은 디렉터가 싣는다.
        getStateSnapshot().registerGameMode<VelmoraGameModeComponent>();
        getStateSnapshot().registerStatefulComponent<SessionStateComponent>();
    }

    VelmoraGame::~VelmoraGame() = default;

    bool VelmoraGame::onInitialize()
    {
        _mods = make_unique<ModSession>( game::getSubsystem<AssetManager>() );
        if ( _mods->applyForGame( game::getSubsystem<const GameConfig>()._packRoot.get() ) == false || getGameScope().bind( _mods.get() ) == false )
            return false;
        // 필수 데이터 — 디렉터 · 액션 룸이 게임 서비스로 찾는다(코드 폴백 없음). 못 읽으면 게임을 띄우지 않는다.
        if ( loadRequiredCatalog( _pRegionCatalog ) == false || loadRequiredCatalog( _pQuestCatalog ) == false || loadRequiredCatalog( _pMonsterCatalog ) == false )
            return false;
        (void)getSubsystem<GameSceneFlow>().requestFirstScene(); // 실패는 씬 흐름이 알린다
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::VelmoraGame );
