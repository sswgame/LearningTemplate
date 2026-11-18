/**
 * @file SkillTree.h
 * @brief 스킬 트리입니다. 스킬(최대 랭크, 비용, 필요 레벨, 선행 스킬 랭크, 트리에 쓴 점수, 배타 그룹, 랭크당 능력치, 주는 어빌리티)과 캐릭터마다의 랭크를 다룹니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalog.h"

namespace sw
{
    class DebugHUDSectionWriter;
    class XMLNode;

    /** @brief 선행 조건 하나입니다. 그 스킬이 이 랭크 이상이어야 합니다. */
    struct SkillRequirement
    {
        hashed_string _skillID{};
        int32         _rank{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 스킬 하나입니다. */
    REFLECT()
    struct SW_KIT_API SkillDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        hashed_string _group{}; ///< 같은 그룹에서는 하나만 배운다(갈림길, 전문화)
        PROPERTY()
        hashed_string _ability{}; ///< 랭크 1 부터 주는 어빌리티 id. 게임이 `AbilityComponent` 에 부여한다
        PROPERTY()
        string                   _name{};            ///< 적지 않으면 id
        vector<SkillRequirement> _listRequirement{}; ///< `requires="basic:2,stance"` 는 손으로 푼다
        StatBlock                _statsPerRank{};    ///< `<Stats …/>`
        PROPERTY( Min = 1 )
        int32 _maxRank{ 1 };
        PROPERTY( Min = 0 )
        int32 _cost{ 1 }; ///< 랭크마다 드는 점수
        PROPERTY( Name = "level" )
        int32 _requiredLevel{ 0 };
        PROPERTY( Name = "spent", Min = 0 )
        int32 _requiredSpent{ 0 }; ///< 이 트리에 이만큼 써야 열린다. 단이나 층 개념이다
    };

    /** @brief 스킬 트리 머리(`<Tree id>`)의 칸입니다. */
    REFLECT()
    struct SW_KIT_API SkillTreeHeadParams
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
    };
} // namespace sw

namespace sw
{
    /** @brief 트리 하나입니다. */
    struct SW_KIT_API SkillTreeDef
    {
        hashed_string    _id{};
        vector<SkillDef> _listSkill{};

        const SkillDef* findSkill( const hashed_string& skillID ) const;
        /** @brief 영원히 배울 수 없는 스킬의 id 입니다. 선행이 없거나, 선행의 최대 랭크보다 높은 랭크를 요구하거나, 순환이거나, 그런 스킬에 기대는 경우입니다. */
        vector<hashed_string> collectUnlearnableSkills() const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkillTreeCatalog
     * @brief `<SkillTreeCatalog><Tree id="combat"><Skill id="power" maxRank="3" cost="1" level="2" spent="0" requires="basic:1,stance"
     *        group="" ability="PowerStrike"><Stats attack="5"/></Skill></Tree></SkillTreeCatalog>` 를 읽습니다(`<Stats>` 는 랭크 하나에 해당하는 양).
     */
    class SW_KIT_API SkillTreeCatalog : public GameCatalog<SkillTreeDef, XMLCatalog<SkillTreeCatalog>>
    {
        friend class XMLCatalog<SkillTreeCatalog>;

    public:
        /** @brief 이 에셋 종류의 접미사입니다(종류 표 `AssetTypeRegistry`). 타입 있는 참조 `AssetRef<SkillTreeCatalog>` 가 접미사로 가른다. */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".skilltrees.xml", AssetSuffixRole::Plain },
        };
        /** @brief 이 에셋 종류입니다(등록은 `SkillTree.cpp` 의 줄이 로더를 더한다). */
        static constexpr AssetTypeDef kAssetType{ "SkillTreeCatalog", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };

    private:
        static constexpr const utf8* kXMLRootName = "SkillTreeCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLCatalogRoot& root, string_view sourceName );
        [[nodiscard]] bool           loadTree( const XMLNode& node, string_view sourceName );
    };

    /** @brief 공유 캐시는 이 카탈로그의 .cpp 가 인스턴스화합니다. 쓰는 쪽은 사본을 읽지 않고 `SharedAssetCache<SkillTreeCatalog>::acquire` 로 한 벌을 받습니다. */
    SW_DECLARE_SHARED_ASSET_CACHE( SW_KIT_API_EXTERN_TEMPLATE, SkillTreeCatalog );
} // namespace sw

namespace sw
{
    /** @brief 랭크 올리기의 결과입니다. */
    ENUM()
    enum class SkillResult : uint8
    {
        Ok = 0,
        UnknownSkill,
        MaxRank,
        NotEnoughPoints,
        LevelTooLow,
        RequirementMissing,
        TierLocked,     ///< 트리에 쓴 점수가 모자란다
        GroupTaken,     ///< 같은 그룹의 다른 스킬을 배웠다
        RequiredByOther ///< 내리기에서 쓴다. 다른 스킬이 이 랭크를 요구한다
    };

    /**
     * @class SkillTreeState
     * @brief 캐릭터 하나가 트리 하나에서 가진 랭크와 남은 점수입니다. 트리 정의는 소유하지 않고 참조만 합니다.
     */
    class SW_KIT_API SkillTreeState
    {
    public:
        SkillTreeState();

        void initialize( const SkillTreeDef* pTree );
        void addPoints( int32 points ) { _points += points; }

        SkillResult evaluateRankUp( const hashed_string& skillID, int32 characterLevel ) const;
        SkillResult rankUp( const hashed_string& skillID, int32 characterLevel );
        /** @brief 랭크를 하나 내리고 점수를 돌려받습니다. 다른 스킬이 그 랭크를 요구하면 내릴 수 없습니다. */
        SkillResult rankDown( const hashed_string& skillID );
        /** @brief 모두 되돌리고 돌려받은 점수입니다. 초기화 물약에 씁니다. */
        int32 refundAll();

        int32 getRank( const hashed_string& skillID ) const;
        int32 getPoints() const { return _points; }
        int32 getSpentPoints() const;
        /** @brief 배운 스킬의 능력치에 랭크를 곱한 값을 더합니다. */
        void computeStats( StatBlock& outStats ) const;
        /** @brief 랭크 1 이상인 스킬이 주는 어빌리티 id 목록입니다. */
        void                collectAbilities( vector<hashed_string>& outListAbility ) const;
        const SkillTreeDef* getTree() const { return _pTree; }
        /**
         * @brief 스킬 디버거 줄을 씁니다 — 머리 줄(남은 점수 · 쓴 점수), 스킬마다 `랭크/최대` 와 다음 랭크의 판정(`evaluateRankUp` 과 같은 순서)과 까닭의 값
         *        (필요 레벨 · 쓴 점수/필요 · 빠진 선행 스킬:랭크 · 차지한 그룹 스킬 · 점수/비용). 이 상태를 가진 쪽(게임)이 자기 HUD 섹션에서 부른다 — 키트는 컴포넌트를 갖지 않는다.
         */
        void writeDebugLines( DebugHUDSectionWriter& writer, int32 characterLevel, uint32 maxLines ) const;

    private:
        [[nodiscard]] optional<int32> findIndex( const hashed_string& skillID ) const;
        /** @brief @p listRank 를 트리의 층(`_requiredSpent`)을 지키며 배워 닿을 수 있는지 판정합니다. */
        bool isSpendOrderReachable( const vector<int32>& listRank ) const;

    private:
        vector<int32>       _listRank; ///< 트리의 스킬마다
        const SkillTreeDef* _pTree;
        int32               _points;
    };
} // namespace sw
