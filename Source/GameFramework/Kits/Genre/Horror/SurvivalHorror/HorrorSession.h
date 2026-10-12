/**
 * @file HorrorSession.h
 * @brief 생존 공포 한 판을 진행합니다. 격자 가방, 아이템 상자, 조합, 탄약, 회복, 세이브 제한, 정신력(어둠과 괴물 목격이 환각과 조준 흔들림으로 이어진다), 손전등,
 *        열쇠 문(`AreaGraph` 조건), 다이얼 자물쇠, 순서 퍼즐, 문서, 단서 보드 추리를 다룹니다.
 * @details 시간은 `update` 로만 흐르고 난수를 쓰지 않는다(결정적). 지도는 게임이 가진 `AreaGraph` 를 빌려 쓴다(방문 상태는 그쪽에 남는다).
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/Combat/Health/ResourceGauge.h"
#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Gameplay/Inventory/GridInventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/HorrorCatalog.h"

namespace sw
{
    struct SessionStateRefs;

    class Archive;
    class AreaGraph;
    class GameFlags;
    class Inventory;

    /** @brief 퍼즐, 자물쇠, 추리의 결과입니다. */
    enum class HorrorPuzzleResult : uint8
    {
        Solved = 0,
        Progress,      ///< 순서 퍼즐에서 맞는 한 걸음이다
        Wrong,         ///< 틀렸다(벌칙이 있으면 받았다)
        AlreadySolved, ///< 이미 풀었다(아무 일도 없다)
        LockedOut,     ///< 시도 횟수를 다 써서 더는 할 수 없다
        MissingItem,   ///< 열쇠가 없거나 추리에 쓸 단서가 없다
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

    /** @brief 한 판에서 일어난 일을 알리는 이벤트입니다. */
    struct SurvivalHorrorEvent
    {
        enum class Kind : uint8
        {
            SanityLost = 0, ///< `_value` 는 깎인 양, `_id` 는 원인이다(괴물, 퍼즐, 추리이며 어둠에 의한 감소는 알리지 않는다)
            HallucinationStarted,
            HallucinationEnded,
            FlashlightDied, ///< 배터리가 다 되어 꺼졌다
            DoorUnlocked,   ///< `_id` 는 자물쇠이다
            PuzzleSolved,   ///< `_id` 는 다이얼 또는 순서 퍼즐이다
            DocumentRead,   ///< `_id` 는 문서이다
            ClueGained,     ///< `_id` 는 단서이다
            DeductionSolved,
            DeductionFailed,
            Combined, ///< `_id` 는 만든 아이템이다
            Saved,    ///< `_value` 는 이번까지 한 세이브 수이다
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
     * @brief 플레이어 한 명의 생존 공포 상태입니다. 카탈로그와 지도는 빌려 쓰므로 이 객체보다 오래 살아야 합니다.
     * @details 정신력은 빛 없이 어둠에 있으면 초당 깎이고(`update( dt, bInDarkness )`), 괴물을 처음 보면 그 괴물의 양만큼, 다시 보면 배율만큼 깎인다.
     *          밝은 곳에서는 지연 뒤 천천히 찬다. 비율이 임계값 아래면 환각이고(게임은 가짜 괴물과 소리를 낸다), 조준 흔들림 배율은 1 + (1 - 비율) x 최대값이다.
     *          손전등은 켜 있는 동안 배터리를 쓰고, 다 되면 꺼진다. 그 뒤의 어둠은 다시 정신력을 깎는다.
     */
    class SW_GF_API HorrorSession
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "HSES" );
        static constexpr uint32 kStateVersion = 1;

        HorrorSession();

        /**
         * @brief 새 판을 시작합니다. @p pAreaGraph 가 있으면 @p startArea 에 들어갑니다.
         * @details 플래그(풀린 퍼즐과 문, 길 조건이 읽는다)는 @p refs 에서 빌린다(없으면 길 조건은 빈 플래그로 본다). 격자 가방(@p inventory, 장르 고유 그릇이라 게임이 가진다)과 아이템 상자(@p itemBox, 세계 보관함)도 빌리므로 세션보다 오래 살아야 한다.
         *          격자 가방은 여기서 카탈로그의 모양과 규칙의 크기로 다시 연다. 이 함수를 호출하기 전에는 다른 함수를 호출하지 않는다.
         */
        void initialize( const HorrorCatalog* pCatalog, AreaGraph* pAreaGraph, const hashed_string& startArea, const SessionStateRefs& refs, GridInventory& inventory,
                         Inventory& itemBox );

        /** @brief 시간을 진행합니다. 손전등 배터리, 어둠에서의 정신력 감소, 밝은 곳에서의 회복을 처리합니다. */
        void update( float32 deltaTime, bool bInDarkness );

        // ── 가방, 상자, 조합 ───────────────────────────────────────────────
        GridInventory&       getInventory() { return *_pInventory; }
        const GridInventory& getInventory() const { return *_pInventory; }
        /** @brief 가방 슬롯 하나에서 @p count 개를 상자로 넣습니다. 모자라면 false 를 반환합니다. */
        [[nodiscard]] bool storeInBox( int32 instanceID, int32 count );
        /** @brief 상자에서 꺼내 가방에 넣고 넣은 개수를 반환합니다(슬롯이 모자란 만큼은 상자에 남습니다). */
        int32 takeFromBox( const hashed_string& itemID, int32 count );
        /** @brief 두 슬롯의 아이템을 하나씩 써서 섞습니다(약초, 화약). 레시피가 없거나 결과를 둘 슬롯이 없으면 아무것도 바꾸지 않고 false 를 반환합니다. */
        [[nodiscard]] bool combineItems( int32 firstInstanceID, int32 secondInstanceID );

        // ── 자원 ──────────────────────────────────────────────────────────
        /** @brief 가방의 탄약을 @p count 발 씁니다. 모자라면 쏘지 않고 false 를 반환합니다. */
        [[nodiscard]] bool tryConsumeAmmo( const hashed_string& ammoItemID, int32 count );
        /** @brief 회복, 진정, 배터리 아이템 하나를 씁니다. 쓸 수 없는 아이템이면 false 를 반환합니다. */
        [[nodiscard]] bool tryUseItem( int32 instanceID );
        void               applyDamage( float32 amount );
        /** @brief 세이브합니다(타자기). 규칙의 제한(횟수, 잉크 리본)을 따릅니다. */
        [[nodiscard]] HorrorSaveResult trySave();

        // ── 공포, 빛 ─────────────────────────────────────────────────────
        /** @brief 손전등을 켜고 끕니다. 배터리가 없으면 켜지지 않고 false 를 반환합니다. */
        [[nodiscard]] bool trySetFlashlight( bool bOn );
        /** @brief 괴물을 보고, 깎인 정신력을 반환합니다(모르는 괴물은 0). */
        float32 witnessMonster( const hashed_string& monsterID );
        /** @brief 정신력을 깎습니다(퍼즐과 추리 벌칙, 초자연 전투에서 씁니다). */
        void loseSanity( float32 amount, const hashed_string& cause );
        bool isHallucinating() const { return _bHallucinating == SW_TRUE; }
        /** @brief 조준 흔들림 배율을 반환합니다(1 이면 맑은 정신입니다). */
        float32 computeAimSwayScale() const;

        // ── 퍼즐, 잠금, 지도 ─────────────────────────────────────────────
        /** @brief 가방의 열쇠로 자물쇠를 엽니다. 플래그를 세우고(`AreaGraph` 연결이 읽습니다), 정의에 따라 열쇠를 씁니다. */
        HorrorPuzzleResult useKey( const hashed_string& lockID );
        /** @brief 다이얼 자물쇠에 번호를 넣습니다. */
        HorrorPuzzleResult enterDialCode( const hashed_string& lockID, const vector<int32>& listDigit );
        /** @brief 순서 퍼즐의 다음 걸음을 누릅니다. 틀리면 처음부터 다시 합니다. */
        HorrorPuzzleResult pressSequenceStep( const hashed_string& puzzleID, const hashed_string& step );
        /** @brief 지금 방에서 바로 이어진 방으로 갑니다. 잠긴 문이면 false 를 반환합니다. */
        [[nodiscard]] bool tryMoveTo( const hashed_string& areaID );

        // ── 문서, 단서 보드 ───────────────────────────────────────────────
        /** @brief 문서를 읽습니다(처음이면 단서가 생깁니다). 처음 읽었으면 true 를 반환합니다. */
        [[nodiscard]] bool readDocument( const hashed_string& documentID );
        void               addClue( const hashed_string& clueID );
        bool               hasClue( const hashed_string& clueID ) const;
        /** @brief 단서 둘을 보드에서 연결합니다. 둘 다 가진 단서여야 하고, 이미 연결되어 있으면 false 를 반환합니다. */
        [[nodiscard]] bool linkClues( const hashed_string& firstClue, const hashed_string& secondClue );
        bool               unlinkClues( const hashed_string& firstClue, const hashed_string& secondClue );
        bool               isLinked( const hashed_string& firstClue, const hashed_string& secondClue ) const;
        /**
         * @brief 결론을 냅니다. 답이 맞고 필요한 연결이 보드에 모두 있어야 풀립니다(찍어서 맞히기를 막습니다).
         * @details 틀리면 정신력 벌칙을 받고, 규칙에 따라 보드의 연결을 모두 지운다. 필요한 단서를 아직 갖지 못했으면 벌칙 없이 `MissingItem` 을 반환한다.
         */
        HorrorPuzzleResult submitDeduction( const hashed_string& deductionID, const hashed_string& answer );

        void drainEvents( vector<SurvivalHorrorEvent>& outListEvent );

        /**
         * @brief 정신력과 배터리 게이지, 본 괴물, 읽은 문서, 단서, 푼 퍼즐(이름 순), 다이얼 시도와 순서 진행(이름 순), 단서 연결, 현재 위치, 체력, 세이브 수,
         *        틀린 추리 수, 손전등, 환각을 씁니다. 같은 상태면 같은 바이트가 나옵니다. 카탈로그, 빌린 격자 가방, 아이템 상자, 플래그, 방 그래프는 저장하지 않고, 이벤트는 읽을 때 비웁니다.
         */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

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
        bool markSolved( const hashed_string& puzzleID, const hashed_string& flag );

        ResourceGauge                       _sanity;
        ResourceGauge                       _battery;
        unordered_set<hashed_string>        _uniqueSeenMonster;
        unordered_set<hashed_string>        _uniqueReadDocument;
        unordered_set<hashed_string>        _uniqueClue;
        unordered_set<hashed_string>        _uniqueSolvedPuzzle;
        unordered_map<hashed_string, int32> _mapDialAttempt;      ///< 다이얼 자물쇠별 틀린 횟수
        unordered_map<hashed_string, int32> _mapSequenceProgress; ///< 순서 퍼즐별로 맞힌 걸음 수
        vector<HorrorClueLink>              _listClueLink;
        EventBuffer<SurvivalHorrorEvent>    _eventBuffer;
        hashed_string                       _currentArea;
        const HorrorCatalog*                _pCatalog;
        GridInventory*                      _pInventory; ///< 격자 가방(빌림)
        Inventory*                          _pItemBox;   ///< 아이템 상자(빌림, 세계 보관함)
        GameFlags*                          _pFlags;     ///< 빌린 플래그
        AreaGraph*                          _pAreaGraph;
        float32                             _health;
        int32                               _saveCount;
        int32                               _wrongDeductionCount;
        uint8                               _bFlashlightOn;
        uint8                               _bHallucinating;
    };
} // namespace sw
