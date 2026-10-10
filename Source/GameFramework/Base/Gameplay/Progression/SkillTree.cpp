#include "pch.h"

#include "GameFramework/Base/Gameplay/Progression/SkillTree.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    SW_LOG_CALLER( "SkillTree" );

    const utf8* toString( SkillResult result )
    {
        switch ( result )
        {
            case SkillResult::Ok:
                return "Ok";
            case SkillResult::UnknownSkill:
                return "UnknownSkill";
            case SkillResult::MaxRank:
                return "MaxRank";
            case SkillResult::NotEnoughPoints:
                return "NotEnoughPoints";
            case SkillResult::LevelTooLow:
                return "LevelTooLow";
            case SkillResult::RequirementMissing:
                return "RequirementMissing";
            case SkillResult::TierLocked:
                return "TierLocked";
            case SkillResult::GroupTaken:
                return "GroupTaken";
            case SkillResult::RequiredByOther:
                return "RequiredByOther";
        }
        return "Unknown";
    }

    const SkillDef* SkillTreeDef::findSkill( const hashed_string& skillID ) const
    {
        for ( const SkillDef& skill : _listSkill )
        {
            if ( skill._id == skillID )
                return &skill;
        }
        return nullptr;
    }

    // ------------------------------------------------------------------------------
    // SkillTreeCatalog
    // ------------------------------------------------------------------------------

    uint32 SkillTreeCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        uint32 loadedCount = 0;
        for ( XMLNode treeNode = root.findChild( "Tree" ); treeNode; treeNode = treeNode.findNextSibling( "Tree" ) )
        {
            const utf8* pTreeID = GameDataXML::findRequiredID( treeNode, sourceName );
            if ( pTreeID == nullptr )
                continue;
            SkillTreeDef tree;
            tree._id = hashed_string( pTreeID );
            for ( XMLNode node = treeNode.findChild( "Skill" ); node; node = node.findNextSibling( "Skill" ) )
            {
                const utf8* pID = GameDataXML::findRequiredID( node, sourceName );
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
                skill._maxRank       = MathUtil::max( 1, node.getAttributeInt( "maxRank", skill._maxRank ) );
                skill._cost          = MathUtil::max( 0, node.getAttributeInt( "cost", skill._cost ) );
                skill._requiredLevel = node.getAttributeInt( "level", skill._requiredLevel );
                skill._requiredSpent = MathUtil::max( 0, node.getAttributeInt( "spent", skill._requiredSpent ) );
                // "basic:2,stance" — 이름:랭크(생략하면 1).
                GameDataXML::forEachToken( node.getAttributeText( "requires" ), ", ", [&]( string_view token )
                {
                    SkillRequirement requirement;
                    const size_t     colon = token.find( ':' );
                    requirement._skillID   = hashed_string( colon == string_view::npos ? token : token.substr( 0, colon ) );
                    if ( colon != string_view::npos )
                    {
                        int32 rank = 1;
                        if ( StringUtil::parseInt( token.substr( colon + 1 ), rank ) )
                            requirement._rank = MathUtil::max( 1, rank );
                    }
                    skill._listRequirement.push_back( requirement );
                } );
                const XMLNode statNode = node.findChild( "Stats" );
                if ( statNode )
                    (void)skill._statsPerRank.loadFromAttributes( statNode ); // 읽은 속성 수만 돌려준다 — 없으면 빈 스탯이다
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
            (void)_catalog.add( tree );
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
                if ( static_cast<int32>( other ) != index && _listRank[other] > 0 && _pTree->_listSkill[other]._group == skill._group )
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
            // 층 — 내리면 이 스킬을 열어 준 점수가 모자라게 되는가.
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
