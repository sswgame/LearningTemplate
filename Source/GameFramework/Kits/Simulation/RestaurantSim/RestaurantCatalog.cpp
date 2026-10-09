#include "pch.h"

#include "GameFramework/Kits/Simulation/RestaurantSim/RestaurantCatalog.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"

namespace sw
{
    SW_LOG_CALLER( "RestaurantCatalog" );

    namespace
    {
        struct RestaurantCatalogInternal
        {
            static void parseIdList( string_view text, vector<hashed_string>& outListId )
            {
                outListId.clear();
                GameDataXml::forEachToken( text, ",;| ", [&]( string_view token )
                { outListId.push_back( hashed_string( token ) ); } );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    int32 DishDef::computeQuality( int32 cookLevel ) const
    {
        int32 quality = 0;
        for ( const int32 threshold : _listQualityLevel )
        {
            if ( cookLevel >= threshold )
                ++quality;
        }
        return MathUtil::max( 1, quality );
    }

    RestaurantCatalog::RestaurantCatalog()
        : _dishCatalog{}
        , _ingredientCatalog{}
        , _customerCatalog{}
        , _weatherCatalog{}
        , _arrArrivalRate{}
        , _openHour{ 11 }
        , _closeHour{ 21 }
        , _ratingWindow{ 10 }
    {
    }

    float32 RestaurantCatalog::getArrivalRate( int32 hour ) const
    {
        if ( hour < 0 || hour >= kHoursPerDay )
            return 0.0f;
        return _arrArrivalRate[hour];
    }

    float32 RestaurantCatalog::findWeatherScale( const hashed_string& weatherId ) const
    {
        const RestaurantWeatherDef* pWeather = _weatherCatalog.find( weatherId );
        return pWeather != nullptr ? pWeather->_arrivalScale : 1.0f;
    }

    int32 RestaurantCatalog::findShelfLife( const hashed_string& itemId ) const
    {
        const IngredientDef* pIngredient = _ingredientCatalog.find( itemId );
        return pIngredient != nullptr ? pIngredient->_shelfLife : 0;
    }

    uint32 RestaurantCatalog::loadRoot( const XmlNode& root, string_view sourceName )
    {
        _openHour     = MathUtil::clamp( root.getAttributeInt( "open", _openHour ), 0, kHoursPerDay - 1 );
        _closeHour    = MathUtil::clamp( root.getAttributeInt( "close", _closeHour ), _openHour + 1, kHoursPerDay );
        _ratingWindow = MathUtil::max( 1, root.getAttributeInt( "window", _ratingWindow ) );

        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Dish" ); node; node = node.findNextSibling( "Dish" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            DishDef dish;
            dish._id            = hashed_string( pId );
            const utf8* pName   = node.findAttribute( "name" );
            dish._name          = pName != nullptr ? pName : pId;
            const utf8* pRecipe = node.findAttribute( "recipe" );
            dish._recipeId      = hashed_string( pRecipe != nullptr ? pRecipe : pId );
            dish._category      = hashed_string( node.getAttributeText( "category" ) );
            dish._basePrice     = node.getAttributeInt( "price", dish._basePrice );
            if ( dish._basePrice <= 0 )
            {
                SW_LOG_WARNING( "%#: dish '%#' has no positive price - skipped", sourceName, pId );
                continue;
            }
            GameDataXml::forEachToken( node.getAttributeText( "quality" ), ",;| ", [&]( string_view token )
            {
                float32 level = 0.0f;
                if ( GameDataXml::parseFloats( token, &level, 1 ) == 1 )
                    dish._listQualityLevel.push_back( static_cast<int32>( level ) );
            } );
            std::sort( dish._listQualityLevel.begin(), dish._listQualityLevel.end() );
            addDish( dish );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Ingredient" ); node; node = node.findNextSibling( "Ingredient" ) )
        {
            const utf8* pItem = node.findAttribute( "item" );
            if ( pItem == nullptr )
            {
                SW_LOG_WARNING( "%#: <Ingredient> without an item - skipped", sourceName );
                continue;
            }
            (void)_ingredientCatalog.add( IngredientDef{ hashed_string( pItem ), MathUtil::max( 0, node.getAttributeInt( "shelfLife", 0 ) ) } );
        }

        for ( XmlNode node = root.findChild( "Customer" ); node; node = node.findNextSibling( "Customer" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            CustomerTypeDef customerType;
            customerType._id         = hashed_string( pId );
            customerType._weight     = MathUtil::max( 0.0f, node.getAttributeFloat( "weight", customerType._weight ) );
            customerType._patience   = MathUtil::max( 1.0f, node.getAttributeFloat( "patience", customerType._patience ) );
            customerType._budget     = MathUtil::max( 0.0f, node.getAttributeFloat( "budget", customerType._budget ) );
            customerType._tipRate    = MathUtil::max( 0.0f, node.getAttributeFloat( "tipRate", customerType._tipRate ) );
            customerType._eatMinutes = MathUtil::max( 0.0f, node.getAttributeFloat( "eat", customerType._eatMinutes ) );
            RestaurantCatalogInternal::parseIdList( node.getAttributeText( "categories" ), customerType._listCategory );
            addCustomerType( customerType );
            ++loadedCount;
        }

        for ( XmlNode node = root.findChild( "Arrival" ); node; node = node.findNextSibling( "Arrival" ) )
        {
            const int32 hour = node.getAttributeInt( "hour", -1 );
            if ( hour < 0 || hour >= kHoursPerDay )
            {
                SW_LOG_WARNING( "%#: <Arrival> hour %# is out of 0..23 - skipped", sourceName, hour );
                continue;
            }
            _arrArrivalRate[hour] = MathUtil::max( 0.0f, node.getAttributeFloat( "rate", 0.0f ) );
        }

        for ( XmlNode node = root.findChild( "Weather" ); node; node = node.findNextSibling( "Weather" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId != nullptr )
                (void)_weatherCatalog.add( RestaurantWeatherDef{ hashed_string( pId ), MathUtil::max( 0.0f, node.getAttributeFloat( "arrival", 1.0f ) ) } );
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING( "%#: no <Dish> / <Customer> entries", sourceName );
        return loadedCount;
    }
} // namespace sw
