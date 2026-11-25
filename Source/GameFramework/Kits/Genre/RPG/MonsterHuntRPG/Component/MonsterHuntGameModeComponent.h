/**
 * @file MonsterHuntGameModeComponent.h
 * @brief 몬스터 사냥 RPG 키트(`GF_MonsterHuntRPG`)의 디렉터 — 의뢰 하나(보상 흥정 → 단서 조사 → 괴물 사냥)를 진행합니다.
 *        키트의 흥정(`MonsterHuntHaggle`) · 조사(`MonsterHuntInvestigation`) · 도감(`MonsterHuntBestiary`) · 전투 자원(`MonsterHuntCombat`) · 연금술(`MonsterHuntAlchemy`)을 씬 데이터로 열고 명령으로 돌립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 입력 · 모습 · HUD 는 클라이언트 모듈
 *          (`GF_Client_MonsterHuntRPG` 의 `MonsterHuntAvatarComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          의뢰를 받은 뒤 스킬(`submitLearnSkill`)을 배워 슬롯에 끼우고 변이원(`submitMutagen`)을 끼운다 — 색이 맞는 스킬마다 변이원이 세지고, 배운 스킬이 대체 표식을 연다.
 *          단서를 모두 찾으면 도감 지식이 오르고 소굴의 괴물이 나온다. 괴물의 체력 · 공격은 씬 데이터, 오일 · 표식의 배율은 상성표다.
 *          괴물을 쓰러뜨리면 승리, 플레이어가 쓰러지면 패배다. 판이 끝나면 `_resultHoldSeconds` 뒤 미니게임을 끝내고(디렉터 계약 ③) 흥정한 보상을 결과 값으로 낸다.
 *          장르 완결 루프 시나리오: `game/playground/automation/monsterhuntrpg_slice.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Time/Countdown.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntAlchemy.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntBestiary.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntCatalog.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntCombat.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntContract.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntMutagens.h"
#include "GameFramework/Runtime/Character/Combat/Damage/ElementChart.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/MatchGameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Crafting.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Progression/SkillTree.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

namespace sw
{
    /** @brief 몬스터 사냥 판의 단계입니다(감시 값 `MonsterHunt.Phase` 의 수). */
    ENUM()
    enum class MonsterHuntPhase : uint8
    {
        Haggle = 0,  ///< 의뢰인과 보상을 흥정한다
        Investigate, ///< 단서를 찾는다
        Hunt,        ///< 괴물과 싸운다
        Victory,     ///< 괴물을 쓰러뜨렸다
        Defeat       ///< 플레이어가 쓰러졌다
    };
} // namespace sw

namespace sw
{
    /**
     * @class MonsterHuntGameModeComponent
     * @brief 흥정(올리기 · 받기) → 조사(걷기 · 단서) → 사냥(공격 · 회피 · 표식 · 오일 · 물약) → 승리 · 패배를 명령으로 진행합니다.
     */
    REFLECT( Version = 2, Category = "MonsterHuntRPG", DisplayName = "Monster Hunt Game Mode", Tooltip = "Runs one MonsterHuntRPG contract: haggling, clue investigation and the hunt from commands" )
    class SW_KIT_API MonsterHuntGameModeComponent : public MatchGameModeComponent
    {
    public:
        REFLECT_BODY();

        MonsterHuntGameModeComponent();
        virtual ~MonsterHuntGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 명령(게임 스레드) ----
        /** @brief 지금 제안보다 `_askStep` 높은 보상을 요구합니다. 받아들이거나 결렬되면 조사로 넘어갑니다. 흥정 중이 아니면 false 입니다. */
        [[nodiscard]] bool submitRaise();
        /** @brief 지금 제안을 받고 조사로 넘어갑니다. 흥정 중이 아니면 false 입니다. */
        [[nodiscard]] bool submitAccept();
        /** @brief 걷는 축을 넘깁니다. 다음 걸음이 쓰고 마지막 값으로 덮습니다. 걸을 수 없는 단계면 받지 않고 false 입니다. */
        [[nodiscard]] bool submitMove( float2 move );
        /** @brief 감각 반경 안의 열린 단서 하나를 조사합니다. 마지막 단서를 찾으면 괴물이 나옵니다. 찾은 것이 없으면 false 입니다. */
        [[nodiscard]] bool submitInvestigate();
        /**
         * @brief 칼을 휘두릅니다(@p bStrong 이면 강한 공격). 쿨다운과 스태미나를 쓰고, 닿으면 오일 · 아드레날린을 반영한 피해를 줍니다.
         * @return 휘둘렀으면(빗나가도) true 입니다. 사냥 중이 아니거나 쿨다운 중이거나 스태미나가 모자라면 false 입니다.
         */
        [[nodiscard]] bool submitAttack( bool bStrong );
        /** @brief 회피합니다. `_dodgeSeconds` 동안 맞지 않습니다. 스태미나가 모자라면 false 입니다. */
        [[nodiscard]] bool submitDodge();
        /** @brief 표식을 겁니다(@p bAlternate 면 대체 시전). 닿으면 위력(변이원 · 스킬 능력치와 상성 반영)만큼 피해를 줍니다. 스태미나 · 스킬이 모자라거나 닿지 않으면 false 입니다. */
        [[nodiscard]] bool submitSign( bool bAlternate );
        /** @brief 스킬 트리에서 지금 배울 수 있는 첫 스킬을 배우고 첫 빈 슬롯에 끼웁니다. 배울 것이 없거나 의뢰 중이 아니면 false 입니다. */
        [[nodiscard]] bool submitLearnSkill();
        /** @brief 가진 변이원 하나를 변이원이 없는 첫 열린 슬롯 그룹에 끼웁니다. 남은 것이 없거나 열린 그룹이 없으면 false 입니다. */
        [[nodiscard]] bool submitMutagen();
        /** @brief 칼에 오일을 바릅니다. 남은 사용 횟수가 없으면 false 입니다. */
        [[nodiscard]] bool submitOil();
        /** @brief 재료로 연금술 물건을 만듭니다(`_brewRecipe`). 만든 오일 · 물약은 사용 횟수가 가득 찬다. 레시피를 적지 않은 판이거나 재료가 모자라면 false 입니다. */
        [[nodiscard]] bool submitBrew();
        /** @brief 물약을 마십니다. 효과 동안 `_potionHealPerSecond` 로 회복합니다. 횟수가 없거나 독성이 넘치면 false 입니다. */
        [[nodiscard]] bool submitPotion();

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        const MonsterHuntInvestigation& getInvestigation() const { return _investigation; }
        const MonsterHuntHaggle&        getHaggle() const { return _haggle; }
        const MonsterHuntBestiary&      getBestiary() const { return _bestiary; }
        const MonsterHuntCombat&        getCombat() const { return _combat; }
        const MonsterHuntAlchemy&       getAlchemy() const { return _alchemy; }
        MonsterHuntPhase                getGamePhase() const { return _phase; }
        int32                           getPhase() const { return static_cast<int32>( _phase ); }
        /** @brief 지금 제안(흥정이 끝났으면 받을 보상)입니다. */
        int32 getReward() const { return _haggle.isClosed() ? _haggle.getFinalReward() : _haggle.getOffer(); }
        /** @brief 사냥하는 괴물의 도감 지식 단계입니다. */
        int32 getKnowledge() const { return _bestiary.getKnowledge( _monster.key() ); }
        /** @brief 조사한 단서 수입니다. */
        int32                      getFoundClueCount() const { return _foundClueCount; }
        const MonsterHuntMutagens& getMutagens() const { return _mutagens; }
        const SkillTreeState&      getSkills() const { return _skills; }
        /** @brief 계약 퀘스트를 마쳤으면 1 입니다(감시 값). */
        int32 getQuestDone() const;
        /** @brief 배운 스킬 랭크 수입니다(감시 값). */
        int32 getLearnedSkillCount() const { return static_cast<int32>( _listLearnedSkill.size() ); }
        /** @brief 변이원과 스킬이 더한 표식 위력(%)입니다. */
        int32   getSignIntensity() const;
        int32   getPlayerHP() const { return _playerHP; }
        int32   getPlayerMaxHP() const { return _playerMaxHP; }
        int32   getMonsterHP() const { return _monsterHP; }
        int32   getOilHits() const { return _alchemy.getOilHits(); }
        float32 getToxicity() const { return _alchemy.getToxicity(); }
        /** @brief 공격 쿨다운이 끝났으면 1 입니다(감시 값). */
        int32 getAttackReady() const { return _attackCooldown.isActive() ? 0 : 1; }
        /** @brief 괴물이 칼에 닿는 거리면 1 입니다(감시 값). */
        int32            getMonsterInReach() const { return _phase == MonsterHuntPhase::Hunt && computeMonsterDistance() <= _attackReach ? 1 : 0; }
        AbsolutePosition getPlayerPosition() const { return toAbsolute( _playerPosition ); }
        AbsolutePosition getMonsterPosition() const { return toAbsolute( _monsterPosition ); }
        float32          getPlayerX() const { return _playerPosition._x; }
        /** @brief 감각 반경 안에서 지금 찾을 수 있는 단서입니다. */
        void collectNearbyClues( vector<const MonsterHuntClueDef*>& outListClue ) const;
        /** @brief 지금 지식으로 약점이 하나라도 보이면 true 입니다. */
        bool isWeaknessKnown() const;

        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateMatch( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        /** @brief 승리면 흥정한 보상을 돈으로 냅니다. */
        void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;

    private:
        /** @brief 땅 평면 자리를 절대 자리로 바꿉니다(시뮬레이션은 높이를 모른다). */
        static AbsolutePosition toAbsolute( const float2& position ) { return AbsolutePosition{
            float3{ position._x, 0.0f, position._y }
 }; }
        /** @brief 씬 데이터의 id 가 모두 카탈로그에 있는지 봅니다. 모르는 id 가 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validateSetup() const;
        /** @brief 사냥꾼의 짐(오일 · 물약 · 명상할 술)으로 가방을 엽니다. */
        [[nodiscard]] bool openBag();
        /** @brief 계약이 적은 퀘스트를 일지에서 시작합니다. 적지 않은 계약이면 빈 일지다. 적었는데 퀘스트 카탈로그 · 퀘스트가 없으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool openQuest();
        /** @brief 씬의 스킬 트리를 열고 점수를 줍니다. 트리를 적지 않은 판은 스킬이 없다. */
        [[nodiscard]] bool openSkills();
        /** @brief 배운 스킬과 열린 그룹의 변이원이 더한 능력치입니다. */
        void    computeStats( StatBlock& outStats ) const;
        bool    isAlive() const { return _phase == MonsterHuntPhase::Investigate || _phase == MonsterHuntPhase::Hunt; }
        float32 computeMonsterDistance() const;
        /** @brief 흥정이 끝났으면 명상으로 횟수를 채우고 조사로 넘어갑니다. */
        void beginInvestigation();
        void beginHunt();
        void updateHunt( float32 deltaTime );
        /** @brief 괴물에게 속성 @p element 의 피해 @p baseDamage 를 줍니다(상성 반영). 쓰러지면 승리입니다. */
        void damageMonster( float32 baseDamage, const hashed_string& element );
        /** @brief 쌓인 도감 · 조사 알림을 로그로 냅니다. */
        void reportEvents();
        /** @brief 승리 · 패배로 판을 끝내고 결과를 보여 주는 시간을 셉니다. */
        void finishGame( MonsterHuntPhase phase );

    private:
        PROPERTY( Category = "Contract", DisplayName = "Contract", Tooltip = "Contract id (catalog)" )
        CatalogKey<MonsterHuntContractDef> _contract;
        PROPERTY( Category = "Contract", DisplayName = "Monster", Tooltip = "Monster id (catalog) waiting at the lair" )
        CatalogKey<MonsterHuntMonsterDef> _monster;
        PROPERTY( Category = "Contract", DisplayName = "Quests", Tooltip = "Quest catalog with the contract's quest; needed when the contract names one" )
        AssetRef<QuestCatalog> _quests;
        PROPERTY( Category = "Contract", DisplayName = "Ask Step", Tooltip = "Money asked above the current offer", Min = 1 )
        int32 _askStep;
        PROPERTY( Category = "Gear", DisplayName = "Element Chart", Tooltip = "Oil and sign multipliers against the monster (required)" )
        AssetRef<ElementChart> _elementChart;
        PROPERTY( Category = "Gear", DisplayName = "Oil", Tooltip = "Alchemy oil id (catalog) in the bag" )
        CatalogKey<MonsterHuntAlchemyDef> _oil;
        PROPERTY( Category = "Gear", DisplayName = "Potion", Tooltip = "Alchemy potion id (catalog) in the bag" )
        CatalogKey<MonsterHuntAlchemyDef> _potion;
        PROPERTY( Category = "Gear", DisplayName = "Brew Recipe", Tooltip = "Recipe (recipe catalog) the hunter can brew; empty = no brewing" )
        CatalogKey<RecipeDef> _brewRecipe;
        PROPERTY( Category = "Gear", DisplayName = "Brew Items", Tooltip = "Item ids in the bag at the start for brewing (list an id twice for two)" )
        vector<CatalogKey<ItemDef>> _listBrewItem;
        PROPERTY( Category = "Gear", DisplayName = "Sign", Tooltip = "Sign id (catalog) the hunter casts" )
        CatalogKey<MonsterHuntSignDef> _sign;
        PROPERTY( Category = "Skills", DisplayName = "Skill Trees", Tooltip = "Skill tree catalog; empty = a hunter without skills" )
        AssetRef<SkillTreeCatalog> _skillTreeCatalog;
        PROPERTY( Category = "Skills", DisplayName = "Skill Tree", Tooltip = "Tree id in the skill tree catalog" )
        CatalogKey<SkillTreeDef> _skillTree;
        PROPERTY( Category = "Skills", DisplayName = "Skill Points", Min = 0 )
        int32 _skillPoints;
        PROPERTY( Category = "Skills", DisplayName = "Hunter Level", Tooltip = "Level that opens skills and slot groups", Min = 1 )
        int32 _hunterLevel;
        PROPERTY( Category = "Skills", DisplayName = "Mutagens", Tooltip = "Mutagen ids (catalog) the hunter carries" )
        vector<CatalogKey<MonsterHuntMutagenDef>> _listMutagen;
        PROPERTY( Category = "Skills", DisplayName = "Attack Stat", Tooltip = "Stat (percent) from skills and mutagens that scales weapon damage" )
        hashed_string _attackStat;
        PROPERTY( Category = "Gear", DisplayName = "Bag Slots", Min = 3 )
        int32 _bagSlotCount;
        PROPERTY( Category = "Field", DisplayName = "Start", Tooltip = "Absolute ground-plane position (X, Z) the hunter starts at" )
        float2 _startPosition;
        PROPERTY( Category = "Field", DisplayName = "Monster Spawn", Tooltip = "Absolute ground-plane position (X, Z) the monster comes from" )
        float2 _spawnPosition;
        PROPERTY( Category = "Field", DisplayName = "Field Min", Tooltip = "Lowest ground-plane corner the hunter can walk to" )
        float2 _fieldMin;
        PROPERTY( Category = "Field", DisplayName = "Field Max", Tooltip = "Highest ground-plane corner the hunter can walk to" )
        float2 _fieldMax;
        PROPERTY( Category = "Field", DisplayName = "Sense Radius", Tooltip = "Distance at which clues are sensed", Min = 0.1, Units = m )
        float32 _senseRadius;
        PROPERTY( Category = "Field", DisplayName = "Move Speed", Min = 0.0, Units = "m/s" )
        float32 _moveSpeed;
        PROPERTY( Category = "Field", DisplayName = "Body Radius", Tooltip = "Radius of the walking body against the static bodies of the scene", Min = 0.05, Units = m )
        float32 _bodyRadius;
        PROPERTY( Category = "Hunter", DisplayName = "Max HP", Min = 1 )
        int32 _playerMaxHP;
        PROPERTY( Category = "Hunter", DisplayName = "Fast Damage", Min = 0.0 )
        float32 _fastDamage;
        PROPERTY( Category = "Hunter", DisplayName = "Strong Damage", Min = 0.0 )
        float32 _strongDamage;
        PROPERTY( Category = "Hunter", DisplayName = "Fast Sound", Tooltip = "Sound event of the fast swing; empty = silent" )
        hashed_string _fastSound;
        PROPERTY( Category = "Hunter", DisplayName = "Strong Sound", Tooltip = "Sound event of the strong swing; empty = silent" )
        hashed_string _strongSound;
        PROPERTY( Category = "Hunter", DisplayName = "Attack Reach", Min = 0.1, Units = m )
        float32 _attackReach;
        PROPERTY( Category = "Hunter", DisplayName = "Sign Reach", Min = 0.1, Units = m )
        float32 _signReach;
        PROPERTY( Category = "Hunter", DisplayName = "Attack Cooldown", Min = 0.0, Units = s )
        float32 _attackCooldownSeconds;
        PROPERTY( Category = "Hunter", DisplayName = "Dodge Time", Tooltip = "Seconds a dodge keeps the hunter untouchable", Min = 0.0, Units = s )
        float32 _dodgeSeconds;
        PROPERTY( Category = "Hunter", DisplayName = "Potion Heal", Tooltip = "HP per second while the potion lasts", Min = 0.0 )
        float32 _potionHealPerSecond;
        PROPERTY( Category = "Monster", DisplayName = "Max HP", Min = 1 )
        int32 _monsterMaxHP;
        PROPERTY( Category = "Monster", DisplayName = "Damage", Min = 0 )
        int32 _monsterDamage;
        PROPERTY( Category = "Monster", DisplayName = "Reach", Min = 0.1, Units = m )
        float32 _monsterReach;
        PROPERTY( Category = "Monster", DisplayName = "Speed", Min = 0.0, Units = "m/s" )
        float32 _monsterSpeed;
        PROPERTY( Category = "Monster", DisplayName = "Attack Interval", Min = 0.1, Units = s )
        float32 _monsterAttackSeconds;
        PROPERTY( Category = "Contract", DisplayName = "Seed", Min = 1 )
        uint32 _seed;

        BoundAssetRef<MonsterHuntCatalog>     _pCatalog;
        BoundAssetRef<ElementChart>           _pChart;
        BoundAssetRef<RecipeCatalog>          _pRecipes;
        BoundAssetRef<ItemCatalog>            _pItemCatalog;
        BoundAssetRef<QuestCatalog>           _pQuests;  ///< 퀘스트를 적지 않은 계약이면 비었다
        QuestLog                              _questLog; ///< 계약 퀘스트(`MonsterHuntContractDef::_questID`)만 진행한다 — 조사가 빌린다
        vector<QuestEvent>                    _listQuestEvent;
        BoundAssetRef<SkillTreeCatalog>       _pSkillTreeCatalog; ///< 트리를 적지 않은 판이면 비었다
        MonsterHuntMutagens                   _mutagens;
        SkillTreeState                        _skills; ///< 배운 차례(`_listLearnedSkill`)가 상태다 — 복원은 차례대로 다시 배운다
        MonsterHuntHaggle                     _haggle;
        MonsterHuntInvestigation              _investigation;
        MonsterHuntBestiary                   _bestiary;
        MonsterHuntCombat                     _combat;
        MonsterHuntAlchemy                    _alchemy;
        Inventory                             _bag;
        vector<MonsterHuntInvestigationEvent> _listInvestigationEvent;
        vector<MonsterHuntBestiaryEvent>      _listBestiaryEvent;
        Countdown                             _attackCooldown;
        Countdown                             _dodge;
        Countdown                             _monsterAttack;
        PROPERTY( RuntimeState )
        float2 _playerPosition;
        PROPERTY( RuntimeState )
        float2 _monsterPosition;
        float2 _commandMove; ///< 마지막 입력 명령의 이동 축 — 클라이언트가 프레임마다 덮는 입력이라 싣지 않는다
        PROPERTY( RuntimeState )
        vector<hashed_string> _listLearnedSkill; ///< 랭크를 올린 스킬(차례대로)
        PROPERTY( RuntimeState, Min = 0 )
        float32 _healProgress; ///< 물약이 채운 1 HP 에 못 미친 몫
        PROPERTY( RuntimeState, Min = 0 )
        int32 _foundClueCount;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _playerHP;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _monsterHP;
        PROPERTY( RuntimeState )
        MonsterHuntPhase _phase;
    };
} // namespace sw
