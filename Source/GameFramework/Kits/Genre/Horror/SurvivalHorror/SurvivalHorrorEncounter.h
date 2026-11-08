/**
 * @file SurvivalHorrorEncounter.h
 * @brief 초자연 전투(애니그마 오브 피어, 엘드리치 호러 류)를 구현합니다. 조사자 여럿과 괴물 하나가 턴 순서(기반 `TurnOrder` 라운드제)대로 주사위를 굴립니다.
 * @details 조사자 차례에는 공포 판정(의지 주사위, 성공이 하나도 없으면 괴물의 공포만큼 정신력을 잃는다)을 하고 이어서 전투 판정(힘 주사위, 성공마다 괴물 강인함 1)을 한다.
 *          괴물 차례에는 체력이 가장 낮은 조사자(같으면 번호가 작은 쪽)를 피해만큼 때린다. 체력이나 정신력이 0 이 되면 그 조사자는 빠진다.
 *          주사위는 시드 난수라서 같은 시드이면 같은 싸움이 된다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorCatalog.h"
#include "GameFramework/Runtime/Character/Combat/TurnOrder.h"
#include "GameFramework/Runtime/Foundation/Utility/Random/GameRandom.h"

namespace sw
{
    class Archive;

    /** @brief 전투에 나선 조사자 한 명입니다. */
    struct SurvivalHorrorInvestigator
    {
        float32 _speed{ 1.0f };
        int32   _actorID{ 0 }; ///< 게임이 정한 번호이다(0 이상)
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
    enum class SurvivalHorrorEncounterState : uint8
    {
        Ongoing = 0,
        Victory, ///< 괴물이 쓰러졌다
        Defeat   ///< 조사자가 모두 빠졌다
    };

    /** @brief 한 차례의 결과입니다. */
    struct SurvivalHorrorTurnResult
    {
        int32 _actorID{ constant::invalid_index::kInt32 };
        int32 _targetID{ constant::invalid_index::kInt32 }; ///< 괴물 차례에서 맞은 조사자
        int32 _horrorSuccesses{ 0 };                        ///< 조사자 차례의 공포 판정 성공 수
        int32 _combatSuccesses{ 0 };                        ///< 조사자 차례의 전투 판정 성공 수이며 괴물에게 준 피해와 같다
        int32 _sanityLoss{ 0 };
        int32 _healthLoss{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class SurvivalHorrorEncounter
     * @brief 한 번의 초자연 전투를 진행합니다. 끝날 때까지 `playNextTurn` 을 호출합니다.
     */
    class SW_KIT_API SurvivalHorrorEncounter
    {
    public:
        static constexpr int32  kMonsterActorID = 1000000; ///< 괴물의 차례 번호이다(조사자 번호와 겹치지 않게 큰 값을 쓴다)
        static constexpr uint32 kStateTag       = FourCCUtil::make( "HENC" );
        static constexpr uint32 kStateVersion   = 1;

        SurvivalHorrorEncounter();

        /** @brief @p successFace 이상이 나오면 성공으로 봅니다(기본 5이며 여섯 면 중 둘입니다). 1 ~ 6 밖이거나, 시드가 0 이거나, 조사자 번호가 음수 · 괴물 번호 · 겹침이면 오류를 남기고 false 입니다(맞추지 않습니다). */
        [[nodiscard]] bool initialize( const SurvivalHorrorMonsterDef& monster, const vector<SurvivalHorrorInvestigator>& listInvestigator, uint32 seed, int32 successFace = 5 );
        /** @brief 다음 차례를 진행합니다. 이미 끝났으면 `_actorID` 가 -1 인 빈 결과를 반환합니다. */
        SurvivalHorrorTurnResult playNextTurn();

        /**
         * @brief 괴물 id, 조사자(속도, 번호, 체력, 정신력, 의지, 힘, 빠짐), 차례(`TurnOrder`), 난수, 남은 강인함, 턴 수, 결과를 씁니다.
         *        괴물 정의와 성공 눈은 `initialize` 에서 받으므로 저장하지 않습니다(괴물 id 는 같은 괴물인지 확인하려고 저장합니다).
         *        `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 같은 괴물로 `initialize` 한 뒤에 호출해야 합니다. 데이터가 깨졌거나, 괴물이 다르거나, 조사자 · 차례 · 강인함 · 결과가 서로 맞지 않으면 false 를 반환하고 상태는 그대로입니다.
         */
        SW_DECLARE_STATE_IO();

        SurvivalHorrorEncounterState              getState() const { return _state; }
        int32                                     getMonsterToughness() const { return _monsterToughness; }
        const vector<SurvivalHorrorInvestigator>& getInvestigators() const { return _listInvestigator; }
        int32                                     getTurnCount() const { return _turnCount; }

    private:
        int32                       rollSuccesses( int32 diceCount );
        SurvivalHorrorInvestigator* findInvestigator( int32 actorID );
        void                        defeatIfBroken( SurvivalHorrorInvestigator& investigator );
        void                        refreshState();
        /** @brief 차례를 받을 수 있는 번호(0 이상, 괴물 번호와 다름)이고 겹치지 않으면 true 입니다. 겹친 둘은 차례 하나를 나눠 쓰고 한쪽이 빠지면 함께 빠진다. */
        [[nodiscard]] static bool hasValidActorIDs( const vector<SurvivalHorrorInvestigator>& listInvestigator );
        /** @brief 읽은 상태가 스스로 맞는지 봅니다. 빠짐은 체력 · 정신력이 정하고, 차례에는 남은 조사자와 괴물만 있으며, 결과는 강인함과 남은 수가 정합니다. */
        [[nodiscard]] bool isStateConsistent() const;

    private:
        vector<SurvivalHorrorInvestigator> _listInvestigator;
        SurvivalHorrorMonsterDef           _monster;
        TurnOrder                          _turnOrder;
        GameRandom                         _random;
        int32                              _monsterToughness;
        int32                              _successFace;
        int32                              _turnCount;
        SurvivalHorrorEncounterState       _state;
    };
} // namespace sw
