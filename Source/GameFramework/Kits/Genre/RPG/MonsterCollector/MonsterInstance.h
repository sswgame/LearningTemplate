/**
 * @file MonsterInstance.h
 * @brief 몬스터 개체 — 개체값(IV 0~31) · 노력치(EV 한 능력치 252 · 합 510) · 성격, 3세대 이후 능력치 공식, 경험치 · 레벨업 · 기술 배우기(4 칸 · PP) ·
 *        진화(레벨 · 아이템 · 친밀도), 파티 6 · 박스 보관입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterCollectorCatalog.h"

namespace sw
{
    class Archive;
    class GameRandom;

    /** @brief 기술 칸 하나입니다. */
    struct MonsterMoveSlot
    {
        hashed_string _moveId{};
        int32         _pp{ 0 };
        int32         _ppMax{ 0 };

        bool isEmpty() const { return _moveId.empty(); }
    };
} // namespace sw

namespace sw
{
    /** @brief 개체 하나(잡은 몬스터 · 야생 · 트레이너의 몬스터)입니다. 능력치 `_arrStat` 는 `MonsterRules::recomputeStats` 가 채웁니다. */
    struct SW_GF_API MonsterInstance
    {
        static constexpr int32  kMoveSlotCount = 4;
        static constexpr uint32 kStateMinBytes = 157; ///< `writeState` 한 개체의 최소 바이트(이름 셋 12 + 기술 칸 48 + 개체값 · 노력치 · 능력치 72 + 경험치 8 + 정수 넷 16 + 상태 1)

        hashed_string   _speciesId{};
        hashed_string   _natureId{};
        string          _nickname{};
        MonsterMoveSlot _arrMove[kMoveSlotCount]{};
        int32           _arrIv[kMonsterStatCount]{ 0, 0, 0, 0, 0, 0 };
        int32           _arrEv[kMonsterStatCount]{ 0, 0, 0, 0, 0, 0 };
        int32           _arrStat[kMonsterStatCount]{ 1, 1, 1, 1, 1, 1 }; ///< 지금 레벨의 능력치(HP 는 최대 HP)
        int64           _exp{ 0 };                                       ///< 총 경험치
        int32           _level{ 1 };
        int32           _hp{ 1 };
        int32           _friendship{ 70 };
        int32           _statusTurns{ 0 }; ///< 수면 남은 턴 · 맹독 n
        MonsterStatus   _status{ MonsterStatus::None };

        int32 getMaxHp() const { return _arrStat[static_cast<size_t>( MonsterStat::Hp )]; }
        int32 getStat( MonsterStat stat ) const { return _arrStat[static_cast<size_t>( stat )]; }
        bool  isFainted() const { return _hp <= 0; }
        int32 countMoves() const;
        /** @brief 이 기술을 가진 칸입니다. 없으면 −1 입니다. */
        int32 findMoveSlot( const hashed_string& moveId ) const;
        /** @brief 개체 하나(종 · 성격 · 별명 · 기술 칸 · 개체값 · 노력치 · 능력치 · 경험치 · 레벨 · HP · 친밀도 · 상태이상)를 씁니다 — 보관함 · 전투가 함께 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다(종 · 기술이 카탈로그에 있는지는 부르는 쪽이 본다). */
        [[nodiscard]] bool readState( Archive& archive );
    };
} // namespace sw

namespace sw
{
    /** @brief 성장에서 일어난 일입니다. */
    struct MonsterGrowthEvent
    {
        enum class Kind : uint8
        {
            LevelUp = 0,
            LearnedMove,
            MoveLearnBlocked, ///< 칸이 넷 다 찼다 — 게임이 "무엇을 잊을까" 를 묻고 `learnMove( …, slot )` 을 부른다
            CanEvolve         ///< 레벨 · 친밀도 진화 조건이 맞았다(`_id` = 진화할 종) — 게임이 B 취소를 받고 `evolve` 를 부른다
        };
        hashed_string _id{}; ///< 기술 · 종
        int32         _level{ 0 };
        Kind          _kind{ Kind::LevelUp };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MonsterRules
     * @brief 개체에 대한 규칙(상태 없음)입니다.
     * @details 능력치 공식(3세대 이후):
     *          HP = ⌊(2B + IV + ⌊EV/4⌋) × L / 100⌋ + L + 10,
     *          그 밖 = ⌊(⌊(2B + IV + ⌊EV/4⌋) × L / 100⌋ + 5) × 성격⌋(성격 110 · 100 · 90 %).
     */
    struct SW_GF_API MonsterRules
    {
        static constexpr int32 kMaxIv              = 31;
        static constexpr int32 kMaxEvPerStat       = 252;
        static constexpr int32 kMaxEvTotal         = 510;
        static constexpr int32 kMaxFriendship      = 255;
        static constexpr int32 kFriendshipPerLevel = 5; ///< 레벨업마다 친밀도

        /** @brief 능력치 하나입니다(위 공식). */
        static int32 computeStat( MonsterStat stat, int32 baseStat, int32 iv, int32 ev, int32 level, int32 naturePercent );
        /** @brief 개체의 능력치를 다시 셉니다. 최대 HP 가 바뀐 만큼 지금 HP 도 옮깁니다(기절한 개체는 그대로 0). */
        static void recomputeStats( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster );
        /**
         * @brief 종 · 레벨로 새 개체를 만듭니다. 개체값은 0..31 을 굴리고 성격은 카탈로그의 성격 중 하나를 굴립니다(씨앗이 같으면 같은 개체).
         * @details 기술은 그 레벨까지 배우는 것 중 **마지막 넷**입니다. 종이 없으면 빈 종 id 의 개체입니다.
         */
        static MonsterInstance createMonster( const MonsterCollectorCatalog& catalog, const hashed_string& speciesId, int32 level, GameRandom& random );
        /** @brief 쓰러뜨린 종의 노력치를 더합니다(한 능력치 252 · 합 510 에서 멈춘다). 능력치는 다시 셉니다. */
        static void addEffortValues( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster, const MonsterSpeciesDef& defeated );
        /** @brief 쓰러뜨려서 받는 경험치 ⌊b × L / 7⌋ 이고 트레이너전이면 ×1.5 입니다. */
        static int64 computeExpYield( const MonsterSpeciesDef& defeated, int32 defeatedLevel, bool bTrainer );
        /**
         * @brief 경험치를 더하고 오른 레벨 수를 돌려줍니다. 레벨마다 능력치 · 친밀도를 올리고 그 레벨의 기술을 배우며(칸이 차면 막힘 알림),
         *        끝에 레벨 · 친밀도 진화 조건을 봅니다.
         */
        static int32 gainExp( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster, int64 amount, vector<MonsterGrowthEvent>& outListEvent );
        /** @brief 기술을 배웁니다. 빈 칸이 있으면 거기, 없으면 @p replaceSlot 을 바꿉니다(−1 이면 실패). 이미 아는 기술도 실패입니다. */
        [[nodiscard]] static bool learnMove( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster, const hashed_string& moveId,
                                             int32 replaceSlot = -1 );
        /**
         * @brief 맞는 진화 갈래의 종입니다. 없으면 빈 이름입니다.
         * @param itemId 비면 레벨업 진화(레벨 · 친밀도 조건만 있는 갈래)를, 비지 않으면 그 아이템 갈래를 봅니다.
         */
        static hashed_string findEvolution( const MonsterCollectorCatalog& catalog, const MonsterInstance& monster, const hashed_string& itemId );
        /** @brief 종을 바꾸고 능력치를 다시 셉니다(개체값 · 노력치 · 성격 · 기술 · 경험치는 그대로). */
        [[nodiscard]] static bool evolve( const MonsterCollectorCatalog& catalog, MonsterInstance& inoutMonster, const hashed_string& targetSpeciesId );
        /** @brief HP · PP · 상태이상을 모두 회복합니다(포켓몬 센터). */
        static void restore( MonsterInstance& inoutMonster );
    };
} // namespace sw

namespace sw
{
    /** @brief 보관한 곳입니다. */
    enum class MonsterStoragePlace : uint8
    {
        Party = 0,
        Box,
        Full ///< 파티도 박스도 찼다
    };

    /**
     * @class MonsterStorage
     * @brief 파티(최대 6)와 박스입니다. 잡은 개체는 파티에 자리가 있으면 파티로, 없으면 박스로 갑니다.
     * @details 파티의 마지막 "싸울 수 있는" 개체는 박스에 맡길 수 없습니다(전투 불능만 남는 파티를 막는다).
     */
    class SW_GF_API MonsterStorage
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "MCST" );
        static constexpr uint32 kStateVersion = 1;
        static constexpr int32  kPartySize    = 6;

        MonsterStorage();

        void                initialize( int32 boxCapacity );
        MonsterStoragePlace add( const MonsterInstance& monster );
        [[nodiscard]] bool  depositToBox( int32 partyIndex );
        [[nodiscard]] bool  withdrawFromBox( int32 boxIndex );
        [[nodiscard]] bool  swapPartyOrder( int32 firstIndex, int32 secondIndex );
        /** @brief 파티 모두를 회복합니다. */
        void restoreParty();
        /** @brief 파티 · 박스의 개체를 씁니다. 박스 정원은 `initialize` 의 설정이라 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 파티가 6 을, 박스가 정원을 넘거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        bool                           hasUsableMonster() const;
        const vector<MonsterInstance>& getParty() const { return _listParty; }
        vector<MonsterInstance>&       getParty() { return _listParty; }
        const vector<MonsterInstance>& getBox() const { return _listBox; }
        int32                          getBoxCapacity() const { return _boxCapacity; }

    private:
        vector<MonsterInstance> _listParty;
        vector<MonsterInstance> _listBox;
        int32                   _boxCapacity;
    };
} // namespace sw
