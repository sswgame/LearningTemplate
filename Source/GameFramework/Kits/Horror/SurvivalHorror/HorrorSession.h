/**
 * @file HorrorSession.h
 * @brief 생존 공포 한 판 — 격자 가방 · 아이템 상자 · 조합, 탄약 · 회복 · 세이브 제한, 정신력(어둠 · 괴물 목격 → 환각 · 조준 흔들림)과 손전등,
 *        열쇠 문(`AreaGraph` 조건) · 다이얼 자물쇠 · 순서 퍼즐, 문서와 단서 보드 추리입니다.
 * @details 시간은 `update` 로만 흐르고 난수를 쓰지 않습니다(결정적). 지도는 게임이 가진 `AreaGraph` 를 빌려 씁니다(방문 상태는 그쪽에 남는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Combat/ResourceGauge.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Inventory/GridInventory.h"
#include "GameFramework/Inventory/ItemBag.h"
#include "GameFramework/Kits/Horror/SurvivalHorror/HorrorCatalog.h"
#include "GameFramework/World/GameFlags.h"

namespace sw
{
    class AreaGraph;

    /** @brief 퍼즐 · 자물쇠 · 추리의 결과입니다. */
    enum class HorrorPuzzleResult : uint8
    {
        Solved = 0,
        Progress,      ///< 순서 퍼즐 — 맞는 한 걸음
        Wrong,         ///< 틀렸다(벌칙이 있으면 받았다)
        AlreadySolved, ///< 이미 풀었다(아무 일도 없다)
        LockedOut,     ///< 시도 횟수를 다 써서 더는 안 된다
        MissingItem,   ///< 열쇠가 없다 · 추리에 쓸 단서가 없다
        Unknown        ///< 그런 정의가 없다
    };

    SW_GF_API const utf8* toString( HorrorPuzzleResult result );

    /** @brief 세이브 결과입니다. */
    enum class HorrorSaveResult : uint8
    {
        Ok = 0,
        NoSaveItem, ///< 잉크 리본이 없다
        NoSavesLeft ///< 정해진 횟수를 다 썼다
    };

    /** @brief 한 판의 알림입니다. */
    struct SurvivalHorrorEvent
    {
        enum class Kind : uint8
        {
            SanityLost = 0, ///< `_value` = 깎인 양, `_id` = 원인(괴물 · 퍼즐 · 추리 — 어둠의 감소는 알리지 않는다)
            HallucinationStarted,
            HallucinationEnded,
            FlashlightDied, ///< 배터리가 다 되어 꺼졌다
            DoorUnlocked,   ///< `_id` = 자물쇠
            PuzzleSolved,   ///< `_id` = 다이얼 · 순서 퍼즐
            DocumentRead,   ///< `_id` = 문서
            ClueGained,     ///< `_id` = 단서
            DeductionSolved,
            DeductionFailed,
            Combined, ///< `_id` = 만든 아이템
            Saved,    ///< `_value` = 이번까지 한 세이브 수
            AreaEntered
        };

        Kind          _kind{ Kind::SanityLost };
        hashed_string _id{};
        float32       _value{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class HorrorSession
     * @brief 플레이어 한 명의 생존 공포 상태입니다. 카탈로그 · 지도는 빌려 씁니다(이 객체보다 오래 살아야 한다).
     * @details 정신력은 빛 없이 어둠에 있으면 초당 깎이고(`update( dt, bInDarkness )`), 괴물을 처음 보면 그 괴물의 양만큼, 다시 보면 배율만큼 깎입니다.
     *          밝은 곳에서는 지연 뒤 천천히 찹니다. 비율이 문턱 아래면 환각(게임은 가짜 괴물 · 소리를 낸다)이고, 조준 흔들림 배율은 1 + (1 − 비율) × 최대입니다.
     *          손전등은 켜 있는 동안 배터리를 쓰고, 다 되면 꺼집니다 — 그 뒤 어둠은 다시 정신력을 깎습니다.
     */
    class SW_GF_API HorrorSession
    {
    public:
        HorrorSession();

        /** @brief 새 판을 시작합니다. @p pAreaGraph 가 있으면 @p startArea 에 들어갑니다. */
        void initialize( const HorrorCatalog* pCatalog, AreaGraph* pAreaGraph, const hashed_string& startArea );

        /** @brief 시간을 흘립니다 — 손전등 배터리, 어둠의 정신력 감소, 밝은 곳의 회복. */
        void update( float32 deltaTime, bool bInDarkness );

        // ── 가방 · 상자 · 조합 ─────────────────────────────────────────────
        GridInventory&       getInventory() { return _inventory; }
        const GridInventory& getInventory() const { return _inventory; }
        /** @brief 공유 아이템 상자(어느 상자에서 열어도 같은 내용)입니다. */
        const ItemBag& getItemBox() const { return _itemBox; }
        /** @brief 가방 자리 하나에서 @p count 개를 상자로 넣습니다. 모자라면 false 입니다. */
        [[nodiscard]] bool storeInBox( int32 instanceId, int32 count );
        /** @brief 상자에서 꺼내 가방에 넣고 넣은 개수를 돌려줍니다(자리가 모자란 만큼은 상자에 남는다). */
        int32 takeFromBox( const hashed_string& itemId, int32 count );
        /** @brief 두 자리의 아이템을 하나씩 써서 섞습니다(약초 · 화약). 레시피가 없거나 결과 자리가 없으면 아무것도 바꾸지 않고 false 입니다. */
        [[nodiscard]] bool combineItems( int32 firstInstanceId, int32 secondInstanceId );

        // ── 자원 ──────────────────────────────────────────────────────────
        /** @brief 가방의 탄약을 @p count 발 씁니다. 모자라면 쏘지 않고 false 입니다. */
        [[nodiscard]] bool tryConsumeAmmo( const hashed_string& ammoItemId, int32 count );
        /** @brief 회복 · 진정 · 배터리 아이템 하나를 씁니다. 쓸 수 없는 아이템이면 false 입니다. */
        [[nodiscard]] bool tryUseItem( int32 instanceId );
        void               applyDamage( float32 amount );
        /** @brief 세이브합니다(타자기). 규칙의 제한(횟수 · 잉크 리본)을 따릅니다. */
        [[nodiscard]] HorrorSaveResult trySave();

        // ── 공포 · 빛 ─────────────────────────────────────────────────────
        /** @brief 손전등을 켜고 끕니다. 배터리가 없으면 켜지지 않고 false 입니다. */
        [[nodiscard]] bool trySetFlashlight( bool bOn );
        /** @brief 괴물을 봅니다. 깎인 정신력입니다(모르는 괴물은 0). */
        float32 witnessMonster( const hashed_string& monsterId );
        /** @brief 정신력을 깎습니다(퍼즐 · 추리 벌칙 · 초자연 전투). */
        void loseSanity( float32 amount, const hashed_string& cause );
        bool isHallucinating() const { return _bHallucinating == SW_TRUE; }
        /** @brief 조준 흔들림 배율(1 = 맑은 정신)입니다. */
        float32 computeAimSwayScale() const;

        // ── 퍼즐 · 잠금 · 지도 ────────────────────────────────────────────
        /** @brief 가방의 열쇠로 자물쇠를 엽니다 — 플래그를 세우고(`AreaGraph` 연결이 읽는다), 정의에 따라 열쇠를 씁니다. */
        HorrorPuzzleResult useKey( const hashed_string& lockId );
        /** @brief 다이얼 자물쇠에 번호를 넣습니다. */
        HorrorPuzzleResult enterDialCode( const hashed_string& lockId, const vector<int32>& listDigit );
        /** @brief 순서 퍼즐의 다음 걸음을 누릅니다. 틀리면 처음부터입니다. */
        HorrorPuzzleResult pressSequenceStep( const hashed_string& puzzleId, const hashed_string& step );
        /** @brief 지금 방에서 바로 이어진 방으로 갑니다. 잠긴 문이면 false 입니다. */
        [[nodiscard]] bool tryMoveTo( const hashed_string& areaId );

        // ── 문서 · 단서 보드 ──────────────────────────────────────────────
        /** @brief 문서를 읽습니다(처음이면 단서가 생긴다). 처음 읽었으면 true 입니다. */
        [[nodiscard]] bool readDocument( const hashed_string& documentId );
        void               addClue( const hashed_string& clueId );
        bool               hasClue( const hashed_string& clueId ) const;
        /** @brief 단서 둘을 보드에서 잇습니다. 둘 다 가진 단서여야 하고 이미 이어져 있으면 false 입니다. */
        [[nodiscard]] bool linkClues( const hashed_string& firstClue, const hashed_string& secondClue );
        bool               unlinkClues( const hashed_string& firstClue, const hashed_string& secondClue );
        bool               isLinked( const hashed_string& firstClue, const hashed_string& secondClue ) const;
        /**
         * @brief 결론을 냅니다. 답이 맞고 필요한 연결이 보드에 모두 있어야 풀립니다(찍어서 맞히기를 막는다).
         * @details 틀리면 정신력 벌칙을 받고, 규칙에 따라 보드의 연결을 모두 지웁니다. 필요한 단서를 아직 갖지 못했으면 벌칙 없이 `MissingItem` 입니다.
         */
        HorrorPuzzleResult submitDeduction( const hashed_string& deductionId, const hashed_string& answer );

        void drainEvents( vector<SurvivalHorrorEvent>& outListEvent );

        GameFlags&                    getFlags() { return _flags; }
        const GameFlags&              getFlags() const { return _flags; }
        const ResourceGauge&          getSanity() const { return _sanity; }
        const ResourceGauge&          getBattery() const { return _battery; }
        const vector<HorrorClueLink>& getClueLinks() const { return _listClueLink; }
        const hashed_string&          getCurrentArea() const { return _currentArea; }
        float32                       getHealth() const { return _health; }
        bool                          isFlashlightOn() const { return _bFlashlightOn == SW_TRUE; }
        bool                          isDead() const { return _health <= 0.0f; }
        int32                         getSaveCount() const { return _saveCount; }
        int32                         getWrongDeductionCount() const { return _wrongDeductionCount; }

    private:
        void pushEvent( SurvivalHorrorEvent::Kind kind, const hashed_string& id, float32 value = 0.0f );
        void refreshHallucination();
        bool markSolved( const hashed_string& puzzleId, const hashed_string& flag );

        GridInventory                       _inventory;
        ItemBag                             _itemBox;
        GameFlags                           _flags;
        ResourceGauge                       _sanity;
        ResourceGauge                       _battery;
        unordered_set<hashed_string>        _uniqueSeenMonster;
        unordered_set<hashed_string>        _uniqueReadDocument;
        unordered_set<hashed_string>        _uniqueClue;
        unordered_set<hashed_string>        _uniqueSolvedPuzzle;
        unordered_map<hashed_string, int32> _mapDialAttempt;      ///< 다이얼 자물쇠 → 틀린 횟수
        unordered_map<hashed_string, int32> _mapSequenceProgress; ///< 순서 퍼즐 → 맞힌 걸음 수
        vector<HorrorClueLink>              _listClueLink;
        vector<SurvivalHorrorEvent>         _listEvent;
        hashed_string                       _currentArea;
        const HorrorCatalog*                _pCatalog;
        AreaGraph*                          _pAreaGraph;
        float32                             _health;
        int32                               _saveCount;
        int32                               _wrongDeductionCount;
        uint8                               _bFlashlightOn;
        uint8                               _bHallucinating;
    };
} // namespace sw
