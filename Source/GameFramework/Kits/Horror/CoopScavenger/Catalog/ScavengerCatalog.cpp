#include "pch.h"

#include "GameFramework/Kits/Horror/CoopScavenger/Catalog/ScavengerCatalog.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "ScavengerCatalog" );

    ScavengerCatalog::ScavengerCatalog()
        : _moonCatalog{}
        , _scrapCatalog{}
        , _listBuyRate{}
        , _quota{}
        , _day{}
        , _carry{}
        , _penalty{}
        , _facility{}
        , _terminalShopId{ "terminal" }
        , _currency{ "Credits" }
        , _crewHealth{ 100.0f }
    {
    }

    float32 ScavengerCatalog::computeBuyRate( int32 daysLeft ) const
    {
        if ( _listBuyRate.empty() )
            return 1.0f;
        if ( daysLeft <= _listBuyRate.front()._daysLeft )
            return _listBuyRate.front()._rate;
        for ( size_t index = 1; index < _listBuyRate.size(); ++index )
        {
            const ScavengerBuyRatePoint& upper = _listBuyRate[index];
            if ( daysLeft > upper._daysLeft )
                continue;
            const ScavengerBuyRatePoint& lower = _listBuyRate[index - 1];
            const float32                ratio = static_cast<float32>( daysLeft - lower._daysLeft ) / static_cast<float32>( upper._daysLeft - lower._daysLeft );
            return MathUtil::lerp( lower._rate, upper._rate, ratio );
        }
        return _listBuyRate.back()._rate;
    }

    uint32 ScavengerCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        const string_view shopId = root.getAttributeText( "terminalShop" );
        if ( shopId.empty() == false )
            _terminalShopId = hashed_string( shopId );
        const string_view currency = root.getAttributeText( "currency" );
        if ( currency.empty() == false )
            _currency = hashed_string( currency );

        _crewHealth = MathUtil::max( 1.0f, root.getAttributeFloat( "crewHealth", _crewHealth ) );

        if ( const XmlNode node = root.findChild( "Quota" ) )
        {
            _quota._startQuota      = MathUtil::max( 1, node.getAttributeInt( "start", _quota._startQuota ) );
            _quota._daysPerCycle    = MathUtil::max( 0, node.getAttributeInt( "days", _quota._daysPerCycle ) );
            _quota._increase        = MathUtil::max( 0.0f, node.getAttributeFloat( "increase", _quota._increase ) );
            _quota._steepness       = MathUtil::max( 0.01f, node.getAttributeFloat( "steepness", _quota._steepness ) );
            _quota._randomness      = MathUtil::clamp( node.getAttributeFloat( "randomness", _quota._randomness ), 0.0f, 2.0f );
            _quota._overtimeDivisor = MathUtil::max( 0.0f, node.getAttributeFloat( "overtime", _quota._overtimeDivisor ) );
            _quota._startCredits    = MathUtil::max( 0, node.getAttributeInt( "credits", _quota._startCredits ) );
        }
        _listBuyRate.clear();
        for ( XmlNode node = root.findChild( "BuyRate" ); node; node = node.findNextSibling( "BuyRate" ) )
        {
            ScavengerBuyRatePoint point;
            point._daysLeft = MathUtil::max( 0, node.getAttributeInt( "daysLeft", point._daysLeft ) );
            point._rate     = MathUtil::max( 0.0f, node.getAttributeFloat( "rate", point._rate ) );
            _listBuyRate.push_back( point );
        }
        std::sort( _listBuyRate.begin(), _listBuyRate.end(), []( const ScavengerBuyRatePoint& lhs, const ScavengerBuyRatePoint& rhs )
        { return lhs._daysLeft < rhs._daysLeft; } );
        for ( size_t index = 1; index < _listBuyRate.size(); ++index )
        {
            if ( _listBuyRate[index]._daysLeft == _listBuyRate[index - 1]._daysLeft )
                SW_LOG_WARNING( "%#: buy rate for %# days left is given twice - the first one wins", sourceName, _listBuyRate[index]._daysLeft );
        }
        _listBuyRate.erase( std::unique( _listBuyRate.begin(), _listBuyRate.end(),
                                         []( const ScavengerBuyRatePoint& lhs, const ScavengerBuyRatePoint& rhs )
        { return lhs._daysLeft == rhs._daysLeft; } ),
                            _listBuyRate.end() );

        if ( const XmlNode node = root.findChild( "Day" ) )
        {
            _day._secondsPerDay = MathUtil::max( 1.0f, node.getAttributeFloat( "secondsPerDay", _day._secondsPerDay ) );
            _day._arrivalHour   = MathUtil::clamp( node.getAttributeFloat( "arrival", _day._arrivalHour ), 0.0f, 23.0f );
            _day._duskHour      = MathUtil::clamp( node.getAttributeFloat( "dusk", _day._duskHour ), _day._arrivalHour, 24.0f );
            _day._departHour    = MathUtil::clamp( node.getAttributeFloat( "depart", _day._departHour ), _day._arrivalHour + 0.5f, 48.0f );
        }
        if ( const XmlNode node = root.findChild( "Carry" ) )
        {
            _carry._slotCount      = MathUtil::clamp( node.getAttributeInt( "slots", _carry._slotCount ), 1, 16 );
            _carry._speedPerWeight = MathUtil::max( 0.0f, node.getAttributeFloat( "speedPerWeight", _carry._speedPerWeight ) );
            _carry._minSpeedScale  = MathUtil::saturate( node.getAttributeFloat( "minSpeed", _carry._minSpeedScale ) );
            _carry._bodyWeight     = MathUtil::max( 0.0f, node.getAttributeFloat( "bodyWeight", _carry._bodyWeight ) );
        }
        if ( const XmlNode node = root.findChild( "Penalty" ) )
        {
            _penalty._deathFine        = MathUtil::saturate( node.getAttributeFloat( "deathFine", _penalty._deathFine ) );
            _penalty._recoveredFine    = MathUtil::saturate( node.getAttributeFloat( "recoveredFine", _penalty._recoveredFine ) );
            _penalty._allDeadLossRatio = MathUtil::saturate( node.getAttributeFloat( "allDeadLoss", _penalty._allDeadLossRatio ) );
            _penalty._allDeadMaxKept   = MathUtil::max( 0, node.getAttributeInt( "allDeadKeep", _penalty._allDeadMaxKept ) );
        }
        if ( const XmlNode node = root.findChild( "Facility" ) )
        {
            _facility._minRooms     = MathUtil::clamp( node.getAttributeInt( "rooms", _facility._minRooms ), 1, 256 );
            _facility._maxRooms     = MathUtil::clamp( node.getAttributeInt( "roomsMax", _facility._minRooms ), _facility._minRooms, 256 );
            _facility._lockedChance = MathUtil::saturate( node.getAttributeFloat( "lockedChance", _facility._lockedChance ) );
            _facility._loopChance   = MathUtil::saturate( node.getAttributeFloat( "loopChance", _facility._loopChance ) );
            _facility._fireExits    = MathUtil::max( 0, node.getAttributeInt( "fireExits", _facility._fireExits ) );
        }

        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Scrap" ); node; node = node.findNextSibling( "Scrap" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            ScavengerScrapDef def;
            def._id          = hashed_string( pId );
            def._minValue    = MathUtil::max( 0, node.getAttributeInt( "min", def._minValue ) );
            def._maxValue    = MathUtil::max( def._minValue, node.getAttributeInt( "max", def._minValue ) );
            def._weight      = MathUtil::max( 0.0f, node.getAttributeFloat( "weight", def._weight ) );
            def._spawnWeight = MathUtil::max( 0.0f, node.getAttributeFloat( "spawnWeight", def._spawnWeight ) );
            def._bTwoHanded  = node.getAttributeBool( "twoHanded", false ) ? SW_TRUE : SW_FALSE;
            (void)_scrapCatalog.add( def );
            ++loadedCount;
        }
        for ( XmlNode node = root.findChild( "Moon" ); node; node = node.findNextSibling( "Moon" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            ScavengerMoonDef def;
            def._id            = hashed_string( pId );
            const utf8* pName  = node.findAttribute( "name" );
            def._name          = pName != nullptr ? pName : pId;
            def._risk          = MathUtil::max( 0.0f, node.getAttributeFloat( "risk", def._risk ) );
            def._routeCost     = MathUtil::max( 0, node.getAttributeInt( "cost", def._routeCost ) );
            def._minScrap      = MathUtil::max( 0, node.getAttributeInt( "scrap", def._minScrap ) );
            def._maxScrap      = MathUtil::max( def._minScrap, node.getAttributeInt( "scrapMax", def._minScrap ) );
            def._minValueScale = MathUtil::max( 0.0f, node.getAttributeFloat( "valueMin", def._minValueScale ) );
            def._maxValueScale = MathUtil::max( def._minValueScale, node.getAttributeFloat( "valueMax", def._minValueScale ) );
            def._bCompany      = node.getAttributeBool( "company", false ) ? SW_TRUE : SW_FALSE;
            GameDataXml::forEachToken( node.getAttributeText( "scraps" ), ",; ", [&]( string_view token )
            {
                const hashed_string scrapId( token );
                if ( _scrapCatalog.find( scrapId ) == nullptr )
                    SW_LOG_WARNING( "%#: moon '%#' lists unknown scrap '%#' - skipped", sourceName, pId, token );
                else
                    def._listScrap.push_back( scrapId );
            } );
            (void)_moonCatalog.add( def );
            ++loadedCount;
        }
        if ( _moonCatalog.isEmpty() )
            SW_LOG_WARNING( "%#: no moons - nowhere to land", sourceName );
        return loadedCount;
    }
} // namespace sw
