/**
 * @file SurvivalHorrorCatalog.h
 * @brief 생존 공포의 데이터를 담습니다. 아이템(격자 크기, 겹침, 회복, 정신력, 배터리), 조합, 괴물, 열쇠 자물쇠, 다이얼 자물쇠, 순서 퍼즐, 문서, 추리, 규칙 수치를 정의합니다.
 * @details 조합은 기반 `RecipeCatalog` 의 레시피(작업대 `Combine`, 재료 둘에서 결과 하나)로 담아 둔다. 약초 섞기도 제작과 같은 정의이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Crafting.h"
#include "GameFramework/Runtime/Gameplay/Inventory/GridInventory.h"

namespace sw
{
    class XMLNode;

    /** @brief 아이템의 쓰임입니다. */
    ENUM()
    enum class SurvivalHorrorItemKind : uint8
    {
        None = 0,
        Weapon,
        Ammo,
        Heal,     ///< 체력과 정신력을 회복한다(약초, 진정제)
        Key,      ///< 자물쇠를 연다
        SaveItem, ///< 잉크 리본이다. 세이브 한 번에 하나를 쓴다
        Battery,  ///< 손전등 배터리
        Document
    };

    /** @brief 세이브 제한 방식입니다. */
    ENUM()
    enum class SurvivalHorrorSaveMode : uint8
    {
        Unlimited = 0,
        Limited,  ///< 정해진 횟수만 세이브할 수 있다
        InkRibbon ///< 세이브마다 `SaveItem` 하나를 쓴다
    };

    /** @brief 아이템 하나입니다. 격자에서 `_width` x `_height` 셀을 차지하며, 돌리면 바뀝니다. */
    REFLECT()
    struct SurvivalHorrorItemDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        string _name{};
        PROPERTY( Name = "heal", Min = 0.0 )
        float32 _healAmount{ 0.0f }; ///< 쓰면 차는 체력
        PROPERTY( Name = "sanity", Min = 0.0 )
        float32 _sanityAmount{ 0.0f }; ///< 쓰면 차는 정신력
        PROPERTY( Name = "battery", Min = 0.0 )
        float32 _batteryAmount{ 0.0f }; ///< 쓰면 차는 손전등 배터리
        PROPERTY( Name = "w", Min = 1, Max = 8 )
        int32 _width{ 1 };
        PROPERTY( Name = "h", Min = 1, Max = 8 )
        int32 _height{ 1 };
        PROPERTY( Min = 1 )
        int32 _maxStack{ 1 }; ///< 한 슬롯에 겹치는 수이다(탄약)
        PROPERTY()
        SurvivalHorrorItemKind _kind{ SurvivalHorrorItemKind::None };
    };
} // namespace sw

namespace sw
{
    /** @brief 괴물 하나입니다. 목격 공포와 턴제 초자연 전투의 수치를 가집니다. */
    REFLECT()
    struct SurvivalHorrorMonsterDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Min = 0.0 )
        float32 _sanityLoss{ 10.0f }; ///< 처음 목격할 때 깎이는 정신력
        PROPERTY( Min = 0.01 )
        float32 _speed{ 1.0f }; ///< 턴제 전투의 속도
        PROPERTY( Min = 1 )
        int32 _toughness{ 3 }; ///< 턴제 전투에서 이만큼 성공을 쌓으면 쓰러진다
        PROPERTY( Min = 0 )
        int32 _damage{ 1 }; ///< 턴제 전투에서 한 번 때릴 때의 체력 피해
        PROPERTY( Min = 0 )
        int32 _horror{ 1 }; ///< 턴제 전투에서 공포 판정에 지면 깎이는 정신력
    };
} // namespace sw

namespace sw
{
    /** @brief 열쇠로 여는 자물쇠(문)입니다. 열면 `_flag` 가 서고, `AreaGraph` 연결의 조건이 그 플래그를 읽습니다. */
    REFLECT()
    struct SurvivalHorrorKeyLockDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Name = "key", Required )
        hashed_string _keyItem{};
        PROPERTY( Required )
        hashed_string _flag{};
        PROPERTY()
        bool _bConsumeKey{ true }; ///< 연 뒤 열쇠가 없어진다(바이오하자드의 "더는 필요 없다")
    };
} // namespace sw

namespace sw
{
    /** @brief 숫자 다이얼 자물쇠입니다. */
    REFLECT()
    struct SurvivalHorrorDialLockDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        hashed_string _flag{};
        vector<int32> _listDigit{};
        PROPERTY( Name = "attempts", Min = 0 )
        int32 _maxAttempts{ 0 }; ///< 0 이면 제한이 없다. 넘으면 잠겨 버린다(경보)
    };
} // namespace sw

namespace sw
{
    /** @brief 순서 퍼즐(종, 피아노, 석상 돌리기)입니다. 틀리면 처음부터 다시 합니다. */
    REFLECT()
    struct SurvivalHorrorSequenceDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        hashed_string _flag{};
        PROPERTY( Name = "steps" )
        vector<hashed_string> _listStep{};
        PROPERTY( Min = 0.0 )
        float32 _mistakeSanity{ 0.0f }; ///< 틀릴 때 깎이는 정신력
    };
} // namespace sw

namespace sw
{
    /** @brief 문서(일지, 쪽지)입니다. 읽으면 단서가 생깁니다. */
    REFLECT()
    struct SurvivalHorrorDocumentDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        string _title{};
        PROPERTY( Name = "clues" )
        vector<hashed_string> _listClue{};
    };
} // namespace sw

namespace sw
{
    /** @brief 단서 보드의 연결 하나입니다. 순서는 상관없습니다(`isSame`). */
    struct SurvivalHorrorClueLink
    {
        hashed_string _first{};
        hashed_string _second{};

        bool isSame( const hashed_string& first, const hashed_string& second ) const
        {
            return ( _first == first && _second == second ) || ( _first == second && _second == first );
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 추리 하나입니다. 정답과, 그 답에 이르는 데 꼭 필요한 단서 연결을 가집니다. */
    REFLECT()
    struct SurvivalHorrorDeductionDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        hashed_string _answer{};
        PROPERTY()
        hashed_string                  _flag{};
        vector<SurvivalHorrorClueLink> _listRequiredLink{};
        PROPERTY( Min = 0.0 )
        float32 _wrongSanity{ 10.0f }; ///< 틀린 추리의 벌칙(정신력)
    };
} // namespace sw

namespace sw
{
    /** @brief 규칙 수치입니다. 시간 단위는 초입니다. */
    REFLECT()
    struct SurvivalHorrorRules
    {
        REFLECT_BODY();

        PROPERTY( Min = 1.0 )
        float32 _maxHealth{ 100.0f };
        PROPERTY( Min = 1.0 )
        float32 _maxSanity{ 100.0f };
        PROPERTY( Min = 0.0 )
        float32 _darknessDrain{ 2.0f }; ///< 빛 없이 어둠에 있으면 초당 깎이는 정신력
        PROPERTY( Min = 0.0 )
        float32 _sanityRegen{ 1.0f }; ///< 밝은 곳에서 초당 차는 정신력
        PROPERTY( Min = 0.0 )
        float32 _sanityRegenDelay{ 3.0f }; ///< 마지막으로 깎인 뒤 이만큼 지나야 차기 시작한다
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _repeatSightingScale{ 0.25f }; ///< 이미 본 괴물을 다시 볼 때의 배율
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _hallucinationThreshold{ 0.3f }; ///< 정신력 비율이 이 값 아래면 환각이 생긴다
        PROPERTY( Min = 0.0 )
        float32 _maxAimSway{ 2.0f }; ///< 정신력이 0 일 때 조준 흔들림 배율에 더하는 양이다(1 + 이 값)
        PROPERTY( Min = 1.0 )
        float32 _maxBattery{ 100.0f };
        PROPERTY( Min = 0.0 )
        float32 _batteryDrain{ 1.0f }; ///< 손전등을 켜면 초당 쓰는 배터리
        PROPERTY( Min = 1, Max = 32 )
        int32 _gridWidth{ 6 };
        PROPERTY( Min = 1, Max = 32 )
        int32 _gridHeight{ 4 };
        PROPERTY( Min = 0 )
        int32 _maxSaves{ 0 }; ///< `Limited` 일 때의 횟수
        PROPERTY()
        SurvivalHorrorSaveMode _saveMode{ SurvivalHorrorSaveMode::Unlimited };
        PROPERTY()
        bool _bClearLinksOnWrong{ true }; ///< 틀린 추리 뒤 단서 연결을 모두 지운다
    };
} // namespace sw

namespace sw
{
    /** @brief `<Combine id="mixGR" a="herbGreen" b="herbRed" out="herbMix" count="1"/>` 의 스키마입니다(읽은 뒤 기반 `RecipeDef` 로 옮긴다). */
    REFLECT()
    struct SurvivalHorrorCombineDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Name = "a", Required )
        hashed_string _firstItem{};
        PROPERTY( Name = "b", Required )
        hashed_string _secondItem{};
        PROPERTY( Name = "out", Required )
        hashed_string _outItem{};
        PROPERTY( Min = 1 )
        int32 _count{ 1 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class SurvivalHorrorCatalog
     * @brief `<SurvivalHorrorCatalog><Rules saveMode="InkRibbon" gridWidth="6" .../><Item id="herbGreen" kind="Heal" w="1" h="1" heal="25"/>
     *        <Combine id="mixGG" a="herbGreen" b="herbGreen" out="herbMix"/><Monster id="zombie" sanityLoss="10" toughness="3"/>
     *        <KeyLock id="redDoor" key="keyRed" flag="redDoorOpen"/><DialLock id="safe" code="3 1 4" flag="safeOpen" attempts="3"/>
     *        <Sequence id="bells" steps="low,high,mid" flag="bellsDone" mistakeSanity="5"/><Document id="diary" title="Diary" clues="knife,gloves"/>
     *        <Deduction id="culprit" answer="butler" links="knife-gloves,gloves-pantry" flag="caseSolved" wrongSanity="15"/></SurvivalHorrorCatalog>` 를 읽습니다.
     */
    class SW_KIT_API SurvivalHorrorCatalog : public XMLCatalog<SurvivalHorrorCatalog>
    {
        friend class XMLCatalog<SurvivalHorrorCatalog>;

    public:
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 타입 있는 참조 `AssetRef<SurvivalHorrorCatalog>` 가 접미사로 가른다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".horror.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `SurvivalHorrorCatalog.cpp` 의 줄이 로더를 더한다). */
        static constexpr AssetTypeDef kAssetType{ "SurvivalHorrorCatalog", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

        SurvivalHorrorCatalog();

        /** @brief 조합 작업대 이름(`Combine`)을 반환합니다. */
        static hashed_string getCombineStation() { return hashed_string( "Combine" ); }
        /** @brief 두 아이템(순서 무관)을 섞는 레시피를 반환합니다. 없으면 nullptr 입니다. */
        const RecipeDef* findCombine( const hashed_string& firstItem, const hashed_string& secondItem ) const;

        const SurvivalHorrorItemDef* findItem( const hashed_string& id ) const { return _itemCatalog.find( id ); }
        /** @brief 기반 `GridInventory` 에 넘길 모양 조회 델리게이트를 만듭니다. 이 카탈로그를 빌리므로 가방보다 오래 살아야 합니다. */
        GridInventory::ShapeDelegate      makeShapeLookup() const;
        const SurvivalHorrorMonsterDef*   findMonster( const hashed_string& id ) const { return _monsterCatalog.find( id ); }
        const SurvivalHorrorKeyLockDef*   findKeyLock( const hashed_string& id ) const { return _keyLockCatalog.find( id ); }
        const SurvivalHorrorDialLockDef*  findDialLock( const hashed_string& id ) const { return _dialLockCatalog.find( id ); }
        const SurvivalHorrorSequenceDef*  findSequence( const hashed_string& id ) const { return _sequenceCatalog.find( id ); }
        const SurvivalHorrorDocumentDef*  findDocument( const hashed_string& id ) const { return _documentCatalog.find( id ); }
        const SurvivalHorrorDeductionDef* findDeduction( const hashed_string& id ) const { return _deductionCatalog.find( id ); }
        const RecipeCatalog&              getRecipeCatalog() const { return _recipeCatalog; }
        const SurvivalHorrorRules&        getRules() const { return _rules; }
        void                              setRules( const SurvivalHorrorRules& rules ) { _rules = rules; }

    private:
        static constexpr const utf8* kXMLRootName = "SurvivalHorrorCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        [[nodiscard]] bool           loadCombine( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadDialLock( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadSequence( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadDocument( const XMLNode& node, string_view sourceName );
        [[nodiscard]] bool           loadDeduction( const XMLNode& node, string_view sourceName );

    private:
        GameCatalog<SurvivalHorrorItemDef>      _itemCatalog;
        GameCatalog<SurvivalHorrorMonsterDef>   _monsterCatalog;
        GameCatalog<SurvivalHorrorKeyLockDef>   _keyLockCatalog;
        GameCatalog<SurvivalHorrorDialLockDef>  _dialLockCatalog;
        GameCatalog<SurvivalHorrorSequenceDef>  _sequenceCatalog;
        GameCatalog<SurvivalHorrorDocumentDef>  _documentCatalog;
        GameCatalog<SurvivalHorrorDeductionDef> _deductionCatalog;
        RecipeCatalog                           _recipeCatalog;
        SurvivalHorrorRules                     _rules;
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<SurvivalHorrorCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_KIT_API_EXTERN_TEMPLATE, SurvivalHorrorCatalog );
} // namespace sw
