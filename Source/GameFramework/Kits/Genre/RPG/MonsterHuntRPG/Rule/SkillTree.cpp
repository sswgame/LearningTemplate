#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/SkillTree.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    const SkillDef* SkillTreeDef::findSkill( const hashed_string& skillID ) const
    {
        return VectorUtil::findBy<&SkillDef::_id>( _listSkill, skillID );
    }

    vector<hashed_string> SkillTreeDef::collectUnlearnableSkills() const
    {
        // 배울 수 있는 스킬에서 넓혀 간다. 끝까지 남는 것은 선행이 없거나, 도달 못 할 랭크를 요구하거나, 순환이다.
        vector<uint8> listLearnable( _listSkill.size(), SW_FALSE );
        bool          bChanged = true;
        while ( bChanged )
        {
            bChanged = false;
            for ( size_t index = 0; index < _listSkill.size(); ++index )
            {
                if ( listLearnable[index] == SW_TRUE )
                    continue;
                bool bReady = true;
                for ( const SkillRequirement& requirement : _listSkill[index]._listRequirement )
                {
                    const SkillDef* pRequired = findSkill( requirement._skillID );
                    const bool      bKnown    = pRequired != nullptr && requirement._rank <= pRequired->_maxRank;
                    bReady                    = bReady && bKnown && listLearnable[static_cast<size_t>( pRequired - _listSkill.data() )] == SW_TRUE;
                }
                if ( bReady == false )
                    continue;
                listLearnable[index] = SW_TRUE;
                bChanged             = true;
            }
        }
        vector<hashed_string> listUnlearnable;
        for ( size_t index = 0; index < _listSkill.size(); ++index )
        {
            if ( listLearnable[index] == SW_FALSE )
                listUnlearnable.push_back( _listSkill[index]._id );
        }
        return listUnlearnable;
    }

    // ------------------------------------------------------------------------------
    // SkillTreeCatalog
    // ------------------------------------------------------------------------------

    uint32 SkillTreeCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        using Element                          = XMLCatalogElement<SkillTreeCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&SkillTreeCatalog::loadTree>( "Tree" ),
        };
        const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement );
        return result._loadedCount;
    }

    bool SkillTreeCatalog::loadTree( const XMLNode& treeNode, string_view sourceName )
    {

        SkillTreeHeadParams head;
        if ( XMLFields::readFields( treeNode, head, sourceName ) == false )
            return false;
        SkillTreeDef tree;
        tree._id = head._id;
        for ( XMLNode node = treeNode.findChild( "Skill" ); node; node = node.findNextSibling( "Skill" ) )
        {
            // 적지 않은 칸은 SkillDef 의 초기값이다. 깨진 값 · 범위 밖 값은 그 스킬을 넣지 않는 오류다(자르지 않는다).
            SkillDef skill;
            bool     bValid = XMLFields::readFields( node, skill, sourceName, { "requires" } );
            if ( skill._id.empty() )
                continue;
            const utf8* const pID = skill._id.c_str();
            if ( skill._name.empty() )
                skill._name = pID;
            // "basic:2,stance" 형식이며, 이름:랭크로 적고 랭크를 생략하면 SkillRequirement 의 초기값(1)이다. 읽히지 않거나 1 아래인 랭크는 오류다.
            XMLReadUtil::forEachNamedNumber( node.getAttributeText( "requires" ), ", ", SkillRequirement{}._rank, XMLReadUtil::MissingValue::UseDefault,
                                             [&]( string_view token, string_view skillName, int32 rank, bool bNumberValid )
            {
                if ( bNumberValid == false || rank < 1 )
                {
                    SW_LOG_ERROR_ID( "SkillTree.SkillInvalidRequirement", "%#: skill '%#' has an invalid requirement '%#'", sourceName, pID, token );
                    bValid = false;
                    return;
                }
                SkillRequirement requirement;
                requirement._skillID = hashed_string( skillName );
                requirement._rank    = rank;
                skill._listRequirement.push_back( requirement );
            } );
            if ( bValid == false )
            {
                SW_LOG_ERROR_ID( "SkillTree.SkillInvalidFieldsSkipped", "%#: skill '%#' has invalid fields - skipped", sourceName, pID );
                continue;
            }
            const XMLNode statNode = node.findChild( "Stats" );
            if ( statNode )
                skill._statsPerRank.addAttributes( statNode ); // 읽은 속성 수만 반환한다. 속성이 없으면 빈 스탯이다
            if ( tree.findSkill( skill._id ) != nullptr )
            {
                // 찾기는 앞의 것만 돌려주어 뒤의 것은 배울 수 없는 칸으로 남는다
                SW_LOG_ERROR_ID( "SkillTree.SkillDuplicateSkipped", "%#: tree '%#' has a duplicate skill '%#' - skipped", sourceName, tree._id.c_str(), pID );
                continue;
            }
            tree._listSkill.push_back( skill );
        }
        // 없는 선행 스킬은 참조 표(`engine/settings/datacatalogreferences.xml` — 같은 트리 안)가 오류로 알리고 로드를 멈춘다(예전에는 경고만 하고 영원히 못 배우는 스킬이 섰다).
        for ( const hashed_string& skillID : tree.collectUnlearnableSkills() )
        {
            SW_LOG_ERROR_ID( "SkillTree.SkillNeverLearnable", "%#: skill '%#' of tree '%#' can never be learned (missing, unreachable or circular requirements)", sourceName, skillID.c_str(), tree._id.c_str() );
        }
        (void)add( tree );
        return true;
    }

    // ------------------------------------------------------------------------------
    // SkillTreeState
    // ------------------------------------------------------------------------------
    SkillTreeState::SkillTreeState()
        : _listRank{}
        , _pTree{ nullptr }
        , _points{ 0 }
    {
    }

    void SkillTreeState::initialize( const SkillTreeDef* pTree )
    {
        _pTree = pTree;
        _listRank.assign( pTree != nullptr ? pTree->_listSkill.size() : 0, 0 );
        _points = 0; // 랭크만 비우면 다시 시작한 캐릭터가 앞 판의 남은 점수를 들고 간다
    }

    optional<int32> SkillTreeState::findIndex( const hashed_string& skillID ) const
    {
        if ( _pTree == nullptr )
            return nullopt;
        for ( size_t index = 0; index < _pTree->_listSkill.size(); ++index )
        {
            if ( _pTree->_listSkill[index]._id == skillID )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    int32 SkillTreeState::getRank( const hashed_string& skillID ) const
    {
        const optional<int32> index = findIndex( skillID );
        return index.has_value() ? _listRank[static_cast<size_t>( *index )] : 0;
    }

    int32 SkillTreeState::getSpentPoints() const
    {
        int32 spent = 0;
        for ( size_t index = 0; index < _listRank.size(); ++index )
        {
            spent += _listRank[index] * _pTree->_listSkill[index]._cost;
        }
        return spent;
    }

    SkillResult SkillTreeState::evaluateRankUp( const hashed_string& skillID, int32 characterLevel ) const
    {
        const optional<int32> found = findIndex( skillID );
        if ( found.has_value() == false )
            return SkillResult::UnknownSkill;
        const int32     index = *found;
        const SkillDef& skill = _pTree->_listSkill[static_cast<size_t>( index )];
        const int32     rank  = _listRank[static_cast<size_t>( index )];
        if ( rank >= skill._maxRank )
            return SkillResult::MaxRank;
        if ( characterLevel < skill._requiredLevel )
            return SkillResult::LevelTooLow;
        if ( getSpentPoints() < skill._requiredSpent )
            return SkillResult::TierLocked;
        for ( const SkillRequirement& requirement : skill._listRequirement )
        {
            if ( getRank( requirement._skillID ) < requirement._rank )
                return SkillResult::RequirementMissing;
        }
        if ( skill._group.empty() == false && rank == 0 )
        {
            for ( size_t other = 0; other < _listRank.size(); ++other )
            {
                const bool bGroupConflict = static_cast<int32>( other ) != index && _listRank[other] > 0 && _pTree->_listSkill[other]._group == skill._group;
                if ( bGroupConflict )
                    return SkillResult::GroupTaken;
            }
        }
        if ( _points < skill._cost )
            return SkillResult::NotEnoughPoints;
        return SkillResult::Ok;
    }

    void SkillTreeState::writeDebugLines( DebugHUDSectionWriter& writer, int32 characterLevel, uint32 maxLines ) const
    {
        if ( _pTree == nullptr )
        {
            writer.addLine( "skills", "no tree" );
            return;
        }
        writer.addLine( string( "tree " ) + _pTree->_id.c_str(), "points " + to_string( _points ) + ", spent " + to_string( getSpentPoints() ) + ", level " + to_string( characterLevel ) );
        uint32 written = 1;
        for ( size_t index = 0; index < _pTree->_listSkill.size(); ++index )
        {
            if ( written >= maxLines )
            {
                writer.addLine( "...", to_string( static_cast<uint64>( _pTree->_listSkill.size() - index ) ) + " more" );
                return;
            }
            const SkillDef&   skill  = _pTree->_listSkill[index];
            const SkillResult result = evaluateRankUp( skill._id, characterLevel );
            string            value  = to_string( _listRank[index] ) + "/" + to_string( skill._maxRank ) + " next: " + getEnumName( result );
            switch ( result )
            {
                case SkillResult::LevelTooLow:
                    value += " (needs level " + to_string( skill._requiredLevel ) + ")";
                    break;
                case SkillResult::TierLocked:
                    value += " (spent " + to_string( getSpentPoints() ) + "/" + to_string( skill._requiredSpent ) + ")";
                    break;
                case SkillResult::RequirementMissing:
                    for ( const SkillRequirement& requirement : skill._listRequirement )
                    {
                        if ( getRank( requirement._skillID ) < requirement._rank )
                            value += string( " (needs " ) + requirement._skillID.c_str() + ":" + to_string( requirement._rank ) + ")";
                    }
                    break;
                case SkillResult::GroupTaken:
                    value += string( " (group " ) + skill._group.c_str() + ")";
                    break;
                case SkillResult::NotEnoughPoints:
                    value += " (points " + to_string( _points ) + "/" + to_string( skill._cost ) + ")";
                    break;
                case SkillResult::Ok:
                case SkillResult::UnknownSkill:
                case SkillResult::MaxRank:
                case SkillResult::RequiredByOther:
                    break;
            }
            writer.addLine( skill._id.c_str(), value );
            ++written;
        }
    }

    SkillResult SkillTreeState::rankUp( const hashed_string& skillID, int32 characterLevel )
    {
        const SkillResult result = evaluateRankUp( skillID, characterLevel );
        if ( result != SkillResult::Ok )
            return result;
        const int32 index = findIndex( skillID ).value(); // 위의 검사가 기술을 확인했다
        _points -= _pTree->_listSkill[static_cast<size_t>( index )]._cost;
        ++_listRank[static_cast<size_t>( index )];
        return SkillResult::Ok;
    }

    SkillResult SkillTreeState::rankDown( const hashed_string& skillID )
    {
        const optional<int32> found = findIndex( skillID );
        if ( found.has_value() == false || _listRank[static_cast<size_t>( *found )] <= 0 )
            return SkillResult::UnknownSkill;
        const int32     index   = *found;
        const SkillDef& skill   = _pTree->_listSkill[static_cast<size_t>( index )];
        const int32     newRank = _listRank[static_cast<size_t>( index )] - 1;
        for ( size_t other = 0; other < _listRank.size(); ++other )
        {
            if ( _listRank[other] <= 0 )
                continue;
            const SkillDef& otherSkill = _pTree->_listSkill[other];
            for ( const SkillRequirement& requirement : otherSkill._listRequirement )
            {
                if ( requirement._skillID == skill._id && newRank < requirement._rank )
                    return SkillResult::RequiredByOther;
            }
        }
        vector<int32> listRankAfter                 = _listRank;
        listRankAfter[static_cast<size_t>( index )] = newRank;
        if ( isSpendOrderReachable( listRankAfter ) == false )
            return SkillResult::RequiredByOther;
        _listRank = std::move( listRankAfter );
        _points += skill._cost;
        return SkillResult::Ok;
    }

    bool SkillTreeState::isSpendOrderReachable( const vector<int32>& listRank ) const
    {
        // 스킬마다 "자기 몫을 뺀 점수" 를 따로 보면 같은 층의 둘이 서로를 열어 주는 것으로 통과한다(아래층을 다 빼도 위층 둘이 남는다).
        // 실제로 배우는 순서를 되짚는다 — 쓴 점수로 열리는 스킬을 그 랭크째 더하기를 더 열리지 않을 때까지 하고, 못 연 것이 남으면 닿을 수 없는 배치다.
        vector<int32> listLeft = listRank;
        int32         spent    = 0;
        bool          bOpened  = true;
        while ( bOpened )
        {
            bOpened = false;
            for ( size_t index = 0; index < listLeft.size(); ++index )
            {
                const SkillDef& skill = _pTree->_listSkill[index];
                if ( listLeft[index] <= 0 || skill._requiredSpent > spent )
                    continue;
                spent += listLeft[index] * skill._cost;
                listLeft[index] = 0;
                bOpened         = true;
            }
        }
        for ( const int32 rank : listLeft )
        {
            if ( rank > 0 )
                return false;
        }
        return true;
    }

    int32 SkillTreeState::refundAll()
    {
        const int32 refunded = getSpentPoints();
        _points += refunded;
        for ( int32& rank : _listRank )
        {
            rank = 0;
        }
        return refunded;
    }

    void SkillTreeState::computeStats( StatBlock& outStats ) const
    {
        outStats.clear();
        for ( size_t index = 0; index < _listRank.size(); ++index )
        {
            if ( _listRank[index] > 0 )
                outStats.merge( _pTree->_listSkill[index]._statsPerRank, static_cast<float32>( _listRank[index] ) );
        }
    }

    void SkillTreeState::collectAbilities( vector<hashed_string>& outListAbility ) const
    {
        outListAbility.clear();
        for ( size_t index = 0; index < _listRank.size(); ++index )
        {
            if ( _listRank[index] > 0 && _pTree->_listSkill[index]._ability.empty() == false )
                outListAbility.push_back( _pTree->_listSkill[index]._ability );
        }
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<SkillTreeCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( SkillTreeCatalog, SkillTreeCatalog, SW_KIT_API, SkillTreeCatalog::kAssetType.withLoader( AssetTypeLoader::of<SkillTreeCatalog>( MemoryTag::Game ) ) );
} // namespace sw
