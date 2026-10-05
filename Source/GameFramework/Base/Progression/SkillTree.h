/**
 * @file SkillTree.h
 * @brief 스킬 트리 — 스킬(최대 랭크 · 비용 · 필요 레벨 · 선행 스킬 랭크 · 트리에 쓴 점수 · 배타 그룹 · 랭크당 능력치 · 주는 어빌리티)과 캐릭터마다의 랭크입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Data/GameCatalog.h"
#include "GameFramework/Base/Data/StatBlock.h"
#include "GameFramework/Base/Data/XmlCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 선행 조건 하나 — 그 스킬이 이 랭크 이상입니다. */
    struct SkillRequirement
    {
        hashed_string _skillId{};
        int32         _rank{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 스킬 하나입니다. */
    struct SkillDef
    {
        hashed_string            _id{};
        hashed_string            _group{};   ///< 같은 그룹에서는 하나만 배운다(갈림길 · 전문화)
        hashed_string            _ability{}; ///< 랭크 1 부터 주는 어빌리티 id(게임이 `AbilitySystemComponent` 에 준다)
        string                   _name{};
        vector<SkillRequirement> _listRequirement{};
        StatBlock                _statsPerRank{};
        int32                    _maxRank{ 1 };
        int32                    _cost{ 1 }; ///< 랭크마다 드는 점수
        int32                    _requiredLevel{ 0 };
        int32                    _requiredSpent{ 0 }; ///< 이 트리에 이만큼 써야 열린다(단 · 층)
    };
} // namespace sw

namespace sw
{
    /** @brief 트리 하나입니다. */
    struct SW_GF_API SkillTreeDef
    {
        hashed_string    _id{};
        vector<SkillDef> _listSkill{};

        const SkillDef* findSkill( const hashed_string& skillId ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SkillTreeCatalog
     * @brief `<SkillTreeCatalog><Tree id="combat"><Skill id="power" maxRank="3" cost="1" level="2" spent="0" requires="basic:1,stance"
     *        group="" ability="PowerStrike"><Stats attack="5"/></Skill></Tree></SkillTreeCatalog>` 를 읽습니다(`<Stats>` 는 랭크 하나의 몫).
     */
    class SW_GF_API SkillTreeCatalog : public XmlCatalog<SkillTreeCatalog>
    {
        friend class XmlCatalog<SkillTreeCatalog>;

    public:
        const SkillTreeDef*         findTree( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<SkillTreeDef>& getTrees() const { return _catalog.getAll(); }

    private:
        static constexpr const utf8* kXmlRootName = "SkillTreeCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<SkillTreeDef> _catalog{};
    };
} // namespace sw

namespace sw
{
    /** @brief 랭크 올리기 결과입니다. */
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
        RequiredByOther ///< 내리기 — 다른 스킬이 이 랭크를 요구한다
    };

    SW_GF_API const utf8* toString( SkillResult result );

    /**
     * @class SkillTreeState
     * @brief 캐릭터 하나가 트리 하나에서 가진 랭크와 남은 점수입니다. 트리 정의는 빌려 씁니다.
     */
    class SW_GF_API SkillTreeState
    {
    public:
        SkillTreeState();

        void initialize( const SkillTreeDef* pTree );
        void addPoints( int32 points ) { _points += points; }

        SkillResult evaluateRankUp( const hashed_string& skillId, int32 characterLevel ) const;
        SkillResult rankUp( const hashed_string& skillId, int32 characterLevel );
        /** @brief 랭크를 하나 내리고 점수를 돌려받습니다(다른 스킬이 그 랭크를 요구하면 못 한다). */
        SkillResult rankDown( const hashed_string& skillId );
        /** @brief 모두 되돌리고 돌려받은 점수입니다(초기화 물약). */
        int32 refundAll();

        int32 getRank( const hashed_string& skillId ) const;
        int32 getPoints() const { return _points; }
        int32 getSpentPoints() const;
        /** @brief 배운 스킬의 능력치(랭크 배)를 더합니다. */
        void computeStats( StatBlock& outStats ) const;
        /** @brief 랭크 1 이상인 스킬이 주는 어빌리티 id 입니다. */
        void                collectAbilities( vector<hashed_string>& outListAbility ) const;
        const SkillTreeDef* getTree() const { return _pTree; }

    private:
        int32 findIndex( const hashed_string& skillId ) const;

        vector<int32>       _listRank; ///< 트리의 스킬 자리마다
        const SkillTreeDef* _pTree;
        int32               _points;
    };
} // namespace sw
