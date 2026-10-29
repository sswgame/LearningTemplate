#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/SkillTree.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    SW_LOG_CALLER( "SkillTree" );

    const SkillDef* SkillTreeDef::findSkill( const hashed_string& skillID ) const
    {
        return VectorUtil::findBy<&SkillDef::_id>( _listSkill, skillID );
    }

    // ------------------------------------------------------------------------------
    // SkillTreeCatalog
    // ------------------------------------------------------------------------------

    uint32 SkillTreeCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode treeNode = root.findChild( "Tree" ); treeNode; treeNode = treeNode.findNextSibling( "Tree" ) )
        {
            const utf8* pTreeID = XMLReadUtil::findRequiredID( treeNode, sourceName );
            if ( pTreeID == nullptr )
                continue;
            SkillTreeDef tree;
            tree._id = hashed_string( pTreeID );
            for ( XMLNode node = treeNode.findChild( "Skill" ); node; node = node.findNextSibling( "Skill" ) )
            {
                const utf8* pID = XMLReadUtil::findRequiredID( node, sourceName );
                if ( pID == nullptr )
                    continue;
                SkillDef skill;
                skill._id            = hashed_string( pID );
                const utf8* pName    = node.findAttribute( "name" );
                const utf8* pGroup   = node.findAttribute( "group" );
                const utf8* pAbility = node.findAttribute( "ability" );
                skill._name          = pName != nullptr ? pName : pID;
                skill._group         = pGroup != nullptr ? hashed_string( pGroup ) : hashed_string{};
                skill._ability       = pAbility != nullptr ? hashed_string( pAbility ) : hashed_string{};
                // 적지 않은 칸은 SkillDef 의 초기값이다. 깨진 값 · 범위 밖 값은 그 스킬을 넣지 않는 오류다(자르지 않는다).
                constexpr int32 kAnyInt = MathUtil::kMaxInt32;
                bool            bValid  = XMLReadUtil::readOptionalNumber( node, "maxRank", sourceName, 1, kAnyInt, skill._maxRank );
                bValid                  = XMLReadUtil::readOptionalNumber( node, "cost", sourceName, 0, kAnyInt, skill._cost ) && bValid;
                bValid                  = XMLReadUtil::readOptionalNumber( node, "level", sourceName, MathUtil::kMinInt32, kAnyInt, skill._requiredLevel ) && bValid;
                bValid                  = XMLReadUtil::readOptionalNumber( node, "spent", sourceName, 0, kAnyInt, skill._requiredSpent ) && bValid;
                // "basic:2,stance" 형식이며, 이름:랭크로 적고 랭크를 생략하면 SkillRequirement 의 초기값(1)이다. 읽히지 않거나 1 아래인 랭크는 오류다.
                XMLReadUtil::forEachNamedNumber( node.getAttributeText( "requires" ), ", ", SkillRequirement{}._rank, XMLReadUtil::MissingValue::UseDefault,
                                                 [&]( string_view token, string_view skillName, int32 rank, bool bNumberValid )
                {
                    if ( bNumberValid == false || rank < 1 )
                    {
                        SW_LOG_ERROR( "%#: skill '%#' has an invalid requirement '%#'", sourceName, pID, token );
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
                    SW_LOG_ERROR( "%#: skill '%#' has invalid fields - skipped", sourceName, pID );
                    continue;
                }
                const XMLNode statNode = node.findChild( "Stats" );
                if ( statNode )
                    skill._statsPerRank.addAttributes( statNode ); // 읽은 속성 수만 반환한다. 속성이 없으면 빈 스탯이다
                tree._listSkill.push_back( skill );
            }
            for ( const SkillDef& skill : tree._listSkill )
            {
                for ( const SkillRequirement& requirement : skill._listRequirement )
                {
                    if ( tree.findSkill( requirement._skillID ) == nullptr )
                        SW_LOG_WARNING( "%#: skill '%#' requires unknown '%#'", sourceName, skill._id.c_str(), requirement._skillID.c_str() );
                }
            }
            (void)add( tree );
            ++loadedCount;
        }
        return loadedCount;
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
    }

    int32 SkillTreeState::findIndex( const hashed_string& skillID ) const
    {
        if ( _pTree == nullptr )
            return -1;
        for ( size_t index = 0; index < _pTree->_listSkill.size(); ++index )
        {
            if ( _pTree->_listSkill[index]._id == skillID )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 SkillTreeState::getRank( const hashed_string& skillID ) const
    {
        const int32 index = findIndex( skillID );
        return index >= 0 ? _listRank[static_cast<size_t>( index )] : 0;
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
        const int32 index = findIndex( skillID );
        if ( index < 0 )
            return SkillResult::UnknownSkill;
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

    SkillResult SkillTreeState::rankUp( const hashed_string& skillID, int32 characterLevel )
    {
        const SkillResult result = evaluateRankUp( skillID, characterLevel );
        if ( result != SkillResult::Ok )
            return result;
        const int32 index = findIndex( skillID );
        _points -= _pTree->_listSkill[static_cast<size_t>( index )]._cost;
        ++_listRank[static_cast<size_t>( index )];
        return SkillResult::Ok;
    }

    SkillResult SkillTreeState::rankDown( const hashed_string& skillID )
    {
        const int32 index = findIndex( skillID );
        if ( index < 0 || _listRank[static_cast<size_t>( index )] <= 0 )
            return SkillResult::UnknownSkill;
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
            // 층 검사: 랭크를 내리면 이 스킬을 열어 준 점수가 모자라게 되는지 확인한다.
            if ( static_cast<int32>( other ) != index && otherSkill._requiredSpent > getSpentPoints() - skill._cost - otherSkill._cost * _listRank[other] )
                return SkillResult::RequiredByOther;
        }
        _listRank[static_cast<size_t>( index )] = newRank;
        _points += skill._cost;
        return SkillResult::Ok;
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
