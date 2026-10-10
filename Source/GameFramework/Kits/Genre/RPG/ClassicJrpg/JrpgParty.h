/**
 * @file JrpgParty.h
 * @brief JRPG 파티 — 멤버(직업 · 레벨 · 능력치 · HP/MP · 내공 · 배운 주문 · 비급 숙련 · 장비), 전직(레벨 1 · 능력치 절반 · 주문 유지 — DQ3),
 *        경험치 · 골드 분배, 여관(살아 있는 멤버 회복) · 교회(부활), 지갑 · 인벤토리(기반 ShopState 와 그대로 쓴다)입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Gameplay/Inventory/Equipment.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/Gameplay/Progression/LevelProgress.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/RPG/ClassicJrpg/JrpgCatalog.h"

namespace sw
{
    struct GameStateRefs;

    class Archive;
    class ItemCatalog;

    /** @brief 비급 하나의 숙련입니다. */
    struct JrpgManualProgress
    {
        hashed_string _manualId{};
        int32         _proficiency{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 파티 멤버 하나입니다. */
    struct SW_GF_API JrpgMember
    {
        hashed_string              _id{};
        hashed_string              _classId{};
        string                     _name{};
        vector<hashed_string>      _listSpell{}; ///< 배운 주문(전직해도 남는다)
        vector<JrpgManualProgress> _listManual{};
        Equipment                  _equipment{};
        LevelProgress              _level{};
        int32                      _arrStat[kJrpgStatCount]{ 1, 0, 1, 1, 1, 1, 1 };
        int32                      _hp{ 1 };
        int32                      _mp{ 0 };
        int32                      _inner{ 0 }; ///< 내공 게이지(무협 옵션)

        bool  isAlive() const { return _hp > 0; }
        int32 getStat( JrpgStat stat ) const { return _arrStat[static_cast<size_t>( stat )]; }
        bool  knowsSpell( const hashed_string& spellId ) const;
        /** @brief 비급 숙련입니다. 배우지 않았으면 −1 입니다. */
        int32 findProficiency( const hashed_string& manualId ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 전직 결과입니다. */
    enum class JrpgClassChangeResult : uint8
    {
        Ok = 0,
        UnknownMember,
        UnknownClass,
        SameClass,
        LevelTooLow,
        MissingItem,
        Dead
    };

    /** @brief 파티에서 일어난 일입니다. */
    struct JrpgPartyEvent
    {
        enum class Kind : uint8
        {
            LevelUp = 0,       ///< `_value` = 새 레벨
            LearnedSpell,      ///< `_id` = 주문
            TechniqueUnlocked, ///< `_id` = 초식
            ClassChanged,      ///< `_id` = 새 직업
            ExpGained,         ///< `_value` = 받은 경험치
            GoldGained,        ///< `_value` = 골드(멤버 −1)
            Rested,            ///< 여관 — `_value` = 낸 돈
            Revived            ///< 교회 — `_value` = 낸 돈
        };
        hashed_string _id{};
        int32         _memberIndex{ -1 };
        int32         _value{ 0 };
        Kind          _kind{ Kind::LevelUp };
    };
} // namespace sw

namespace sw
{
    /**
     * @class JrpgParty
     * @brief 멤버 최대 4 명과 지갑 · 인벤토리입니다. 카탈로그 둘은 빌려 씁니다.
     * @details 공격력 = 힘 + 장비 "attack", 방어력 = 체력 / 2 + 장비 "defense" 입니다(장비는 기반 `Equipment` — 아이템 `<Stats attack="12"/>`).
     *          경험치 분배는 DQ 식: 살아 있는 멤버 수로 나눈 몫(내림)을 각자 받고, 골드는 지갑에 그대로 들어갑니다.
     */
    class SW_GF_API JrpgParty
    {
    public:
        static constexpr uint32 kStateTag            = FourCcUtil::make( "JPTY" );
        static constexpr uint32 kStateVersion        = 1;
        static constexpr int32  kMaxMembers          = 4;
        static constexpr int32  kInnerMax            = 100;
        static constexpr int32  kClassChangeMinLevel = 20; ///< DQ3 다마 신전

        JrpgParty();

        /** @brief 새 파티를 엽니다. 파티 가방 · 지갑은 @p refs 에서 빌립니다 — 가방이 없으면 아이템이 드는 전직이, 지갑이 없으면 골드 보상 · 여관 · 교회가 막힙니다. */
        void initialize( const JrpgCatalog* pCatalog, const ItemCatalog* pItemCatalog, const GameStateRefs& refs,
                         string_view equipLayout = "Weapon,Armor,Shield,Helmet,Accessory" );
        /** @brief 멤버를 더합니다(직업의 레벨 1 능력치에서 @p level 까지 성장). 자리 번호, 못 더하면 −1 입니다. */
        int32 addMember( const hashed_string& memberId, string_view name, const hashed_string& classId, int32 level = 1 );
        /**
         * @brief 전직합니다(DQ3) — 레벨 1 · 경험치 0 으로 돌아가고 능력치는 지금의 절반(내림, 최대 HP 는 1 이상), 배운 주문은 남습니다.
         * @param minLevel 전직할 수 있는 최소 레벨입니다.
         */
        JrpgClassChangeResult changeClass( int32 memberIndex, const hashed_string& classId, int32 minLevel = kClassChangeMinLevel );
        /** @brief 경험치를 더하고 오른 레벨 수입니다. 레벨마다 직업 성장치를 더하고 그 레벨의 주문을 배웁니다. */
        int32 addExp( int32 memberIndex, int64 amount );
        /** @brief 전투 보상 — 살아 있는 멤버가 경험치를 똑같이 나눠 받고(나머지 버림), 골드는 지갑으로 갑니다. 한 멤버의 몫입니다. */
        int64 distributeRewards( int64 exp, int64 gold );
        /** @brief 여관 — 살아 있는 멤버 수 × @p pricePerMember 를 내고 HP · MP 를 채웁니다. 쓰러진 멤버는 그대로입니다. 돈이 모자라면 false 입니다. */
        [[nodiscard]] bool restAtInn( int64 pricePerMember );
        /** @brief 교회 — 레벨 × @p pricePerLevel 을 내고 쓰러진 멤버를 HP 가득 살립니다. */
        [[nodiscard]] bool reviveAtChurch( int32 memberIndex, int64 pricePerLevel );
        /** @brief 비급을 익힙니다(숙련 0 — 첫 단계가 0 이면 그 초식이 열린다). */
        void learnManual( int32 memberIndex, const hashed_string& manualId );
        /** @brief 비급 숙련을 더하고 새로 열린 초식을 알립니다. 익히지 않은 비급이면 아무것도 하지 않습니다. */
        void addProficiency( int32 memberIndex, const hashed_string& manualId, int32 amount );
        void drainEvents( vector<JrpgPartyEvent>& outListEvent );
        /** @brief 멤버(직업 · 이름 · 주문 · 비급 숙련 · 장비 아이템 id · 레벨 · 능력치 · HP/MP · 내공)를 씁니다. 가방 · 지갑은 빌린 것이라 싣지 않는다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 모르는 직업 · 아이템, 장비 칸 수가 다르거나 깨졌으면 false 이고 그대로입니다(카탈로그 · 칸 구성은 `initialize` 의 것). */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief 이 주문 · 초식을 쓸 수 있는가입니다(배웠거나, 비급 숙련이 그 단계에 닿았다). */
        bool  canUseSpell( int32 memberIndex, const hashed_string& spellId ) const;
        bool  isTechniqueUnlocked( int32 memberIndex, const hashed_string& techniqueId ) const;
        int32 computeAttack( int32 memberIndex ) const;
        int32 computeDefense( int32 memberIndex ) const;
        int32 countAlive() const;
        int32 findMemberIndex( const hashed_string& memberId ) const;

        int32              getMemberCount() const { return static_cast<int32>( _listMember.size() ); }
        const JrpgMember&  getMember( int32 memberIndex ) const { return _listMember[static_cast<size_t>( memberIndex )]; }
        JrpgMember&        getMember( int32 memberIndex ) { return _listMember[static_cast<size_t>( memberIndex )]; }
        const JrpgCatalog* getCatalog() const { return _pCatalog; }

    private:
        bool isValidIndex( int32 memberIndex ) const { return memberIndex >= 0 && memberIndex < static_cast<int32>( _listMember.size() ); }
        void growOneLevel( int32 memberIndex, const JrpgClassDef& classDef );
        void learnSpellsAtLevel( int32 memberIndex, const JrpgClassDef& classDef, int32 level );
        void pushEvent( JrpgPartyEvent::Kind kind, int32 memberIndex, int32 value, const hashed_string& id = hashed_string{} );

        vector<JrpgMember>          _listMember;
        EventBuffer<JrpgPartyEvent> _eventBuffer;
        string                      _equipLayout;
        const JrpgCatalog*          _pCatalog;
        const ItemCatalog*          _pItemCatalog;
        Inventory*                  _pInventory; ///< 플레이어 가방(빌림)
        Wallet*                     _pWallet;    ///< 빌린 지갑(골드)
    };
} // namespace sw
