/**
 * @file HorrorCatalog.h
 * @brief 생존 공포의 데이터 — 아이템(격자 크기 · 겹침 · 회복 · 정신력 · 배터리) · 조합 · 괴물 · 열쇠 자물쇠 · 다이얼 자물쇠 · 순서 퍼즐 · 문서 · 추리 · 규칙 수치입니다.
 * @details 조합은 기반 `RecipeCatalog` 의 레시피(작업대 `Combine`, 재료 둘 → 결과)로 담아 둡니다 — 약초 섞기도 제작과 같은 정의입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Inventory/Crafting.h"

namespace sw
{
    class XmlNode;

    /** @brief 아이템의 쓰임입니다. */
    enum class HorrorItemKind : uint8
    {
        Misc = 0,
        Weapon,
        Ammo,
        Heal,     ///< 체력 · 정신력 회복(약초 · 진정제)
        Key,      ///< 자물쇠를 연다
        SaveItem, ///< 잉크 리본 — 세이브 한 번에 하나
        Battery,  ///< 손전등 배터리
        Document
    };

    /** @brief 세이브 제한 방식입니다. */
    enum class HorrorSaveMode : uint8
    {
        Unlimited = 0,
        Limited,  ///< 정해진 횟수만
        InkRibbon ///< 세이브마다 `SaveItem` 하나를 쓴다
    };

    [[nodiscard]] SW_GF_API bool parseHorrorItemKind( string_view text, HorrorItemKind& outKind );
    SW_GF_API const utf8*        toString( HorrorItemKind kind );

    /** @brief 아이템 하나입니다. 격자에서 `_width` × `_height` 칸을 차지합니다(돌리면 바뀐다). */
    struct HorrorItemDef
    {
        hashed_string  _id{};
        string         _name{};
        float32        _healAmount{ 0.0f };    ///< 쓰면 차는 체력
        float32        _sanityAmount{ 0.0f };  ///< 쓰면 차는 정신력
        float32        _batteryAmount{ 0.0f }; ///< 쓰면 차는 손전등 배터리
        int32          _width{ 1 };
        int32          _height{ 1 };
        int32          _maxStack{ 1 }; ///< 한 자리에 겹치는 수(탄약)
        HorrorItemKind _kind{ HorrorItemKind::Misc };
    };

    /** @brief 괴물 하나입니다 — 목격 공포와 턴제 초자연 전투 수치입니다. */
    struct HorrorMonsterDef
    {
        hashed_string _id{};
        float32       _sanityLoss{ 10.0f }; ///< 처음 목격할 때 깎이는 정신력
        float32       _speed{ 1.0f };       ///< 턴제 전투의 속도
        int32         _toughness{ 3 };      ///< 턴제 전투 — 이만큼 성공을 쌓으면 쓰러진다
        int32         _damage{ 1 };         ///< 턴제 전투 — 한 번 때리는 체력 피해
        int32         _horror{ 1 };         ///< 턴제 전투 — 공포 판정에 지면 깎이는 정신력
    };

    /** @brief 열쇠로 여는 자물쇠(문)입니다. 열면 `_flag` 가 서고 `AreaGraph` 연결의 조건이 그 플래그를 읽습니다. */
    struct HorrorKeyLockDef
    {
        hashed_string _id{};
        hashed_string _keyItem{};
        hashed_string _flag{};
        uint8         _bConsumeKey{ SW_TRUE }; ///< 연 뒤 열쇠가 없어진다(바이오하자드의 "더는 필요 없다")
    };

    /** @brief 숫자 다이얼 자물쇠입니다. */
    struct HorrorDialLockDef
    {
        hashed_string _id{};
        hashed_string _flag{};
        vector<int32> _listDigit{};
        int32         _maxAttempts{ 0 }; ///< 0 = 제한 없음. 넘으면 잠겨 버린다(경보)
    };

    /** @brief 순서 퍼즐(종 · 피아노 · 석상 돌리기)입니다. 틀리면 처음부터입니다. */
    struct HorrorSequenceDef
    {
        hashed_string         _id{};
        hashed_string         _flag{};
        vector<hashed_string> _listStep{};
        float32               _mistakeSanity{ 0.0f }; ///< 틀릴 때 깎이는 정신력
    };

    /** @brief 문서(일지 · 쪽지)입니다. 읽으면 단서가 생깁니다. */
    struct HorrorDocumentDef
    {
        hashed_string         _id{};
        string                _title{};
        vector<hashed_string> _listClue{};
    };

    /** @brief 단서 보드의 연결 하나입니다. 순서는 상관없습니다(`isSame`). */
    struct HorrorClueLink
    {
        hashed_string _first{};
        hashed_string _second{};

        bool isSame( const hashed_string& first, const hashed_string& second ) const
        {
            return ( _first == first && _second == second ) || ( _first == second && _second == first );
        }
    };

    /** @brief 추리 하나 — 정답과 그 답에 이르는 데 꼭 필요한 단서 연결들입니다. */
    struct HorrorDeductionDef
    {
        hashed_string          _id{};
        hashed_string          _answer{};
        hashed_string          _flag{};
        vector<HorrorClueLink> _listRequiredLink{};
        float32                _wrongSanity{ 10.0f }; ///< 틀린 추리의 벌칙(정신력)
    };

    /** @brief 규칙 수치입니다. 시간은 초입니다. */
    struct HorrorRules
    {
        float32        _maxHealth{ 100.0f };
        float32        _maxSanity{ 100.0f };
        float32        _darknessDrain{ 2.0f };          ///< 빛 없이 어둠에 있으면 초당 깎이는 정신력
        float32        _sanityRegen{ 1.0f };            ///< 밝은 곳에서 초당 차는 정신력
        float32        _sanityRegenDelay{ 3.0f };       ///< 마지막으로 깎인 뒤 이만큼 지나야 찬다
        float32        _repeatSightingScale{ 0.25f };   ///< 이미 본 괴물을 다시 볼 때의 배율
        float32        _hallucinationThreshold{ 0.3f }; ///< 정신력 비율이 이 아래면 환각
        float32        _maxAimSway{ 2.0f };             ///< 정신력 0 일 때 조준 흔들림 배율에 더하는 양(1 + 이 값)
        float32        _maxBattery{ 100.0f };
        float32        _batteryDrain{ 1.0f }; ///< 손전등을 켜면 초당 쓰는 배터리
        int32          _gridWidth{ 6 };
        int32          _gridHeight{ 4 };
        int32          _maxSaves{ 0 }; ///< `Limited` 의 횟수
        HorrorSaveMode _saveMode{ HorrorSaveMode::Unlimited };
        uint8          _bClearLinksOnWrong{ SW_TRUE }; ///< 틀린 추리 뒤 단서 연결을 모두 지운다
    };

    /**
     * @class HorrorCatalog
     * @brief `<HorrorCatalog><Rules saveMode="InkRibbon" gridWidth="6" .../><Item id="herbGreen" kind="Heal" w="1" h="1" heal="25"/>
     *        <Combine id="mixGG" a="herbGreen" b="herbGreen" out="herbMix"/><Monster id="zombie" sanityLoss="10" toughness="3"/>
     *        <KeyLock id="redDoor" key="keyRed" flag="redDoorOpen"/><DialLock id="safe" code="3 1 4" flag="safeOpen" attempts="3"/>
     *        <Sequence id="bells" steps="low,high,mid" flag="bellsDone" mistakeSanity="5"/><Document id="diary" title="Diary" clues="knife,gloves"/>
     *        <Deduction id="culprit" answer="butler" links="knife-gloves,gloves-pantry" flag="caseSolved" wrongSanity="15"/></HorrorCatalog>` 를 읽습니다.
     */
    class SW_GF_API HorrorCatalog
    {
    public:
        HorrorCatalog();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );

        /** @brief 조합 작업대 이름(`Combine`)입니다. */
        static hashed_string getCombineStation() { return hashed_string( "Combine" ); }
        /** @brief 두 아이템(순서 무관)을 섞는 레시피입니다. 없으면 nullptr 입니다. */
        const RecipeDef* findCombine( const hashed_string& firstItem, const hashed_string& secondItem ) const;

        const HorrorItemDef*      findItem( const hashed_string& id ) const { return _itemCatalog.find( id ); }
        const HorrorMonsterDef*   findMonster( const hashed_string& id ) const { return _monsterCatalog.find( id ); }
        const HorrorKeyLockDef*   findKeyLock( const hashed_string& id ) const { return _keyLockCatalog.find( id ); }
        const HorrorDialLockDef*  findDialLock( const hashed_string& id ) const { return _dialLockCatalog.find( id ); }
        const HorrorSequenceDef*  findSequence( const hashed_string& id ) const { return _sequenceCatalog.find( id ); }
        const HorrorDocumentDef*  findDocument( const hashed_string& id ) const { return _documentCatalog.find( id ); }
        const HorrorDeductionDef* findDeduction( const hashed_string& id ) const { return _deductionCatalog.find( id ); }
        const RecipeCatalog&      getRecipeCatalog() const { return _recipeCatalog; }
        const HorrorRules&        getRules() const { return _rules; }
        void                      setRules( const HorrorRules& rules ) { _rules = rules; }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<HorrorItemDef>      _itemCatalog;
        GameCatalog<HorrorMonsterDef>   _monsterCatalog;
        GameCatalog<HorrorKeyLockDef>   _keyLockCatalog;
        GameCatalog<HorrorDialLockDef>  _dialLockCatalog;
        GameCatalog<HorrorSequenceDef>  _sequenceCatalog;
        GameCatalog<HorrorDocumentDef>  _documentCatalog;
        GameCatalog<HorrorDeductionDef> _deductionCatalog;
        RecipeCatalog                   _recipeCatalog;
        HorrorRules                     _rules;
    };
} // namespace sw
