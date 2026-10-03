#include "pch.h"

#include "GameFramework/Combat/ElementChart.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/GameRandom.h"
#include "GameFramework/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "ElementChart" );

    ElementChart::ElementChart()
        : _listElement{}
        , _mapRule{}
        , _listStatusChance{}
    {
    }

    bool ElementChart::loadFromResource( string_view path )
    {
        XmlDocument doc;
        XmlNode     root;
        string      sourceName;
        return GameDataXml::loadRoot( doc, path, "ElementChart", root, sourceName ) && loadRoot( root, sourceName ) > 0;
    }

    bool ElementChart::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        XmlDocument doc;
        XmlNode     root;
        return GameDataXml::parseRoot( doc, xmlText, sourceName, "ElementChart", root ) && loadRoot( root, sourceName ) > 0;
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
        for ( const hashed_string& candidate : _listElement )
        {
            if ( candidate == element )
                return true;
        }
        return false;
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
            multiplier *= getMultiplier( attack, defend );
        return multiplier;
    }

    bool ElementChart::isImmune( const hashed_string& attack, const vector<hashed_string>& listDefend ) const { return computeMultiplier( attack, listDefend ) <= 0.0f; }

    hashed_string ElementChart::rollStatus( const hashed_string& element, GameRandom& random ) const
    {
        for ( const ElementStatusChance& entry : _listStatusChance )
        {
            if ( entry._element == element && random.nextChance( entry._chance ) )
                return entry._status;
        }
        return hashed_string{};
    }

    uint32 ElementChart::loadRoot( const XmlNode& root, string_view sourceName )
    {
        clear();
        for ( XmlNode node = root.findChild( "Element" ); node; node = node.findNextSibling( "Element" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId != nullptr )
                addElement( hashed_string( pId ) );
        }
        if ( _listElement.empty() )
        {
            SW_LOG_WARNING( "%#: no <Element> entries", sourceName );
            return 0;
        }

        for ( XmlNode node = root.findChild( "Rule" ); node; node = node.findNextSibling( "Rule" ) )
        {
            const hashed_string attack( node.getAttributeText( "attack" ) );
            const hashed_string defend( node.getAttributeText( "defend" ) );
            if ( hasElement( attack ) == false || hasElement( defend ) == false )
            {
                SW_LOG_WARNING( "%#: rule '%#' -> '%#' names an undeclared element - skipped", sourceName, attack.c_str(), defend.c_str() );
                continue;
            }
            setMultiplier( attack, defend, node.getAttributeFloat( "multiplier", 1.0f ) );
        }

        for ( XmlNode node = root.findChild( "Status" ); node; node = node.findNextSibling( "Status" ) )
        {
            ElementStatusChance entry;
            entry._element = hashed_string( node.getAttributeText( "element" ) );
            entry._status  = hashed_string( node.getAttributeText( "status" ) );
            entry._chance  = node.getAttributeFloat( "chance", 0.0f );
            if ( hasElement( entry._element ) == false || entry._status.empty() )
            {
                SW_LOG_WARNING( "%#: status '%#' on '%#' is invalid - skipped", sourceName, entry._status.c_str(), entry._element.c_str() );
                continue;
            }
            addStatusChance( entry );
        }
        return static_cast<uint32>( _listElement.size() );
    }
} // namespace sw
