#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/RestaurantCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"

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
        , _openHour{ RestaurantCatalogRootDef{}._openHour }
        , _closeHour{ RestaurantCatalogRootDef{}._closeHour }
        , _ratingWindow{ RestaurantCatalogRootDef{}._ratingWindow }
    {
    }

    float32 RestaurantCatalog::getArrivalRate( int32 hour ) const
    {
        if ( hour < 0 || hour >= kHoursPerDay )
            return 0.0f;
        return _arrArrivalRate[hour];
    }

    float32 RestaurantCatalog::findWeatherScale( const hashed_string& weatherID ) const
    {
        const RestaurantWeatherDef* pWeather = _weatherCatalog.find( weatherID );
        return pWeather != nullptr ? pWeather->_arrivalScale : 1.0f;
    }

    int32 RestaurantCatalog::findShelfLife( const hashed_string& itemID ) const
    {
        const IngredientDef* pIngredient = _ingredientCatalog.find( itemID );
        return pIngredient != nullptr ? pIngredient->_shelfLife : 0;
    }

    uint32 RestaurantCatalog::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        RestaurantCatalogRootDef rootDef;
        if ( XMLFields::readFields( root, rootDef, sourceName ) == false )
            return 0;
        if ( rootDef._closeHour <= rootDef._openHour )
        {
            SW_LOG_ERROR_ID( "RestaurantCatalog.CloseMustLaterOpen", "%#: close=\"%#\" must be later than open=\"%#\"", sourceName, rootDef._closeHour, rootDef._openHour );
            return 0;
        }
        _openHour     = rootDef._openHour;
        _closeHour    = rootDef._closeHour;
        _ratingWindow = rootDef._ratingWindow;

        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const utf8* pElement = node.getName();
            if ( StringUtil::equals( pElement, "Dish" ) )
            {
                if ( loadDish( node, sourceName ) )
                    ++loadedCount;
            }
            else if ( StringUtil::equals( pElement, "Customer" ) )
            {
                if ( loadCustomer( node, sourceName ) )
                    ++loadedCount;
            }
            else if ( StringUtil::equals( pElement, "Ingredient" ) )
            {
                IngredientDef ingredient;
                if ( XMLFields::readFields( node, ingredient, sourceName ) )
                    (void)_ingredientCatalog.add( ingredient ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
            }
            else if ( StringUtil::equals( pElement, "Arrival" ) )
            {
                RestaurantArrivalDef arrival;
                if ( XMLFields::readFields( node, arrival, sourceName ) )
                    _arrArrivalRate[arrival._hour] = arrival._rate;
            }
            else if ( StringUtil::equals( pElement, "Weather" ) )
            {
                RestaurantWeatherDef weather;
                if ( XMLFields::readFields( node, weather, sourceName ) )
                    (void)_weatherCatalog.add( weather );
            }
            else
            {
                SW_LOG_ERROR_ID( "RestaurantCatalog.UnknownElementSkipped", "%#: unknown element <%#> - skipped", sourceName, pElement );
            }
        }
        if ( loadedCount == 0 )
            SW_LOG_WARNING_ID( "RestaurantCatalog.NoDishCustomerEntries", "%#: no <Dish> / <Customer> entries", sourceName );
        return loadedCount;
    }

    bool RestaurantCatalog::loadDish( const XMLNode& node, string_view sourceName )
    {
        DishDef    dish;
        const bool bValid = XMLFields::readFields( node, dish, sourceName );
        VectorUtil::sort( dish._listQualityLevel );
        if ( bValid )
            addDish( dish );
        return bValid;
    }

    bool RestaurantCatalog::loadCustomer( const XMLNode& node, string_view sourceName )
    {
        CustomerTypeDef customerType;
        const bool      bValid = XMLFields::readFields( node, customerType, sourceName );
        if ( bValid )
            addCustomerType( customerType );
        return bValid;
    }
} // namespace sw
