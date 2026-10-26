#include "pch.h"

#include "GameFramework/Base/Actor/Combat/Damage/ElementChart.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"

namespace sw
{
    SW_LOG_CALLER( "ElementChart" );

    ElementChart::ElementChart()
        : _listElement{}
        , _mapRule{}
        , _listStatusChance{}
    {
    }

    void ElementChart::clear()
    {
        _listElement.clear();
        _mapRule.clear();
        _listStatusChance.clear();
    }

    void ElementChart::addElement( const hashed_string& element )
    {
        if ( element.empty() || hasElement( element ) )
            return;
        _listElement.push_back( element );
    }

    bool ElementChart::hasElement( const hashed_string& element ) const
    {
        return VectorUtil::contains( _listElement, element );
    }

    void ElementChart::setMultiplier( const hashed_string& attack, const hashed_string& defend, float32 multiplier )
    {
        _mapRule[attack][defend] = MathUtil::max( 0.0f, multiplier );
    }

    void ElementChart::addStatusChance( const ElementStatusChance& statusChance )
    {
        ElementStatusChance entry = statusChance;
        entry._chance             = MathUtil::saturate( entry._chance );
        _listStatusChance.push_back( entry );
    }

    float32 ElementChart::getMultiplier( const hashed_string& attack, const hashed_string& defend ) const
    {
        const auto attackIter = _mapRule.find( attack );
        if ( attackIter == _mapRule.end() )
            return 1.0f;
        const auto defendIter = attackIter->second.find( defend );
        return defendIter != attackIter->second.end() ? defendIter->second : 1.0f;
    }

    float32 ElementChart::computeMultiplier( const hashed_string& attack, const vector<hashed_string>& listDefend ) const
    {
        float32 multiplier = 1.0f;
        for ( const hashed_string& defend : listDefend )
        {
            multiplier *= getMultiplier( attack, defend );
        }
        return multiplier;
    }

    bool ElementChart::isImmune( const hashed_string& attack, const vector<hashed_string>& listDefend ) const
    {
        return computeMultiplier( attack, listDefend ) <= 0.0f;
    }

    hashed_string ElementChart::rollStatus( const hashed_string& element, GameRandom& random ) const
    {
        for ( const ElementStatusChance& entry : _listStatusChance )
        {
            if ( entry._element == element && random.nextChance( entry._chance ) )
                return entry._status;
        }
        return hashed_string{};
    }

    uint32 ElementChart::loadRoot( const XMLNode& root, string_view sourceName )
    {
        clear();
        for ( XMLNode node = root.findChild( "Element" ); node; node = node.findNextSibling( "Element" ) )
        {
            const utf8* pID = XMLReadUtil::findRequiredID( node, sourceName );
            if ( pID != nullptr )
                addElement( hashed_string( pID ) );
        }
        if ( _listElement.empty() )
        {
            SW_LOG_WARNING( "%#: no <Element> entries", sourceName );
            return 0;
        }

        for ( XMLNode node = root.findChild( "Rule" ); node; node = node.findNextSibling( "Rule" ) )
        {
            const hashed_string attack( node.getAttributeText( "attack" ) );
            const hashed_string defend( node.getAttributeText( "defend" ) );
            if ( hasElement( attack ) == false || hasElement( defend ) == false )
            {
                SW_LOG_WARNING( "%#: rule '%#' -> '%#' names an undeclared element - skipped", sourceName, attack.c_str(), defend.c_str() );
                continue;
            }
            // 규칙 줄은 배율을 적으려고 있는 것이다 — 빠진 배율을 1 로 두지 않는다.
            const std::optional<float32> multiplier = node.readRequiredAttribute<float32>( "multiplier" );
            if ( multiplier.has_value() )
                setMultiplier( attack, defend, *multiplier );
        }

        for ( XMLNode node = root.findChild( "Status" ); node; node = node.findNextSibling( "Status" ) )
        {
            ElementStatusChance entry;
            entry._element = hashed_string( node.getAttributeText( "element" ) );
            entry._status  = hashed_string( node.getAttributeText( "status" ) );
            // 확률은 필수이고 [0, 1] 이다.
            const std::optional<float32> chance       = node.readRequiredAttribute<float32>( "chance" );
            const bool                   bChanceValid = chance.has_value() && 0.0f <= *chance && *chance <= 1.0f;
            if ( bChanceValid )
                entry._chance = *chance;
            const bool bEntryValid = hasElement( entry._element ) && entry._status.empty() == false && bChanceValid;
            if ( bEntryValid == false )
            {
                SW_LOG_WARNING( "%#: status '%#' on '%#' is invalid - skipped", sourceName, entry._status.c_str(), entry._element.c_str() );
                continue;
            }
            addStatusChance( entry );
        }
        return static_cast<uint32>( _listElement.size() );
    }
} // namespace sw
