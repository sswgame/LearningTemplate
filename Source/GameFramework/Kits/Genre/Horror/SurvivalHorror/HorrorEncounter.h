/**
 * @file HorrorEncounter.h
 * @brief 초자연 전투(애니그마 오브 피어 · 엘드리치 호러 류) — 조사자 여럿과 괴물 하나가 턴 순서(기반 `TurnOrder` 라운드제)대로 주사위를 굴립니다.
 * @details 조사자 차례: 공포 판정(의지 주사위 — 성공이 하나도 없으면 괴물의 공포만큼 정신력을 잃는다) → 전투 판정(힘 주사위 — 성공마다 괴물 강인함 1).
 *          괴물 차례: 체력이 가장 낮은 조사자(같으면 번호가 작은 쪽)를 피해만큼 때립니다. 체력이나 정신력이 0 이 되면 그 조사자는 빠집니다.
 *          주사위는 씨앗 난수라 같은 씨앗이면 같은 싸움입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Actor/Combat/TurnOrder.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/HorrorCatalog.h"

namespace sw
{
    class Archive;

    /** @brief 전투에 나선 조사자 한 명입니다. */
    struct HorrorInvestigator
    {
        float32 _speed{ 1.0f };
        int32   _actorID{ 0 }; ///< 게임이 정한 번호(0 이상)
        int32   _health{ 5 };
        int32   _sanity{ 5 };
        int32   _will{ 2 };     ///< 공포 판정 주사위 수
        int32   _strength{ 2 }; ///< 전투 판정 주사위 수
        uint8   _bDefeated{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 전투 상태입니다. */
    enum class HorrorEncounterState : uint8
    {
        Ongoing = 0,
        Victory, ///< 괴물이 쓰러졌다
        Defeat   ///< 조사자가 모두 빠졌다
    };

    /** @brief 한 차례의 결과입니다. */
    struct HorrorTurnResult
    {
        int32 _actorID{ -1 };
        int32 _targetID{ -1 };       ///< 괴물 차례 — 맞은 조사자
        int32 _horrorSuccesses{ 0 }; ///< 조사자 차례 — 공포 판정 성공 수
        int32 _combatSuccesses{ 0 }; ///< 조사자 차례 — 전투 판정 성공 수(= 괴물에게 준 피해)
        int32 _sanityLoss{ 0 };
        int32 _healthLoss{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class HorrorEncounter
     * @brief 한 번의 초자연 전투입니다. `playNextTurn` 을 끝날 때까지 부릅니다.
     */
    class SW_GF_API HorrorEncounter
    {
    public:
        static constexpr int32  kMonsterActorID = 1000000; ///< 괴물의 차례 번호(조사자 번호와 겹치지 않게)
        static constexpr uint32 kStateTag       = FourCcUtil::make( "HENC" );
        static constexpr uint32 kStateVersion   = 1;

        HorrorEncounter();

        /** @brief @p successFace 이상이 나오면 성공(기본 5 — 여섯 면 중 둘)입니다. */
        void initialize( const HorrorMonsterDef& monster, const vector<HorrorInvestigator>& listInvestigator, uint32 seed, int32 successFace = 5 );
        /** @brief 다음 차례를 치릅니다. 이미 끝났으면 `_actorID` 가 −1 인 빈 결과입니다. */
        HorrorTurnResult playNextTurn();

        /**
         * @brief 괴물 id · 조사자(속도 · 번호 · 체력 · 정신력 · 의지 · 힘 · 빠짐) · 차례(`TurnOrder`) · 난수 · 남은 강인함 · 턴 수 · 결과를 씁니다.
         *        괴물 정의와 성공 눈은 `initialize` 의 것이라 싣지 않습니다(괴물 id 는 맞는지 보려고 싣는다).
         */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 같은 괴물로 `initialize` 한 뒤에 부릅니다. 깨졌거나 괴물이 다르면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        HorrorEncounterState              getState() const { return _state; }
        int32                             getMonsterToughness() const { return _monsterToughness; }
        const vector<HorrorInvestigator>& getInvestigators() const { return _listInvestigator; }
        int32                             getTurnCount() const { return _turnCount; }

    private:
        int32               rollSuccesses( int32 diceCount );
        HorrorInvestigator* findInvestigator( int32 actorID );
        void                defeatIfBroken( HorrorInvestigator& investigator );
        void                refreshState();

        vector<HorrorInvestigator> _listInvestigator;
        HorrorMonsterDef           _monster;
        TurnOrder                  _turnOrder;
        GameRandom                 _random;
        int32                      _monsterToughness;
        int32                      _successFace;
        int32                      _turnCount;
        HorrorEncounterState       _state;
    };
} // namespace sw
