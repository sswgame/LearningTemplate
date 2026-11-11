#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/RestaurantCatalog.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

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
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readProperties 가 남긴다).
        RestaurantCatalogRootDef rootDef;
        if ( XMLFields::readProperties( root, rootDef, sourceName ) == false )
            return 0;
        if ( rootDef._closeHour <= rootDef._openHour )
        {
            SW_LOG_ERROR_ID( "RestaurantCatalog.CloseMustLaterOpen", "%#: close=\"%#\" must be later than open=\"%#\"", sourceName, rootDef._closeHour, rootDef._openHour );
            return 0;
        }
        _openHour     = rootDef._openHour;
        _closeHour    = rootDef._closeHour;
        _ratingWindow = rootDef._ratingWindow;

        using Element                          = XMLCatalogElement<RestaurantCatalog>;
        static constexpr Element kArrElement[] = {
            Element::of<&RestaurantCatalog::loadDish>( "Dish" ),
            Element::of<&RestaurantCatalog::loadCustomer>( "Customer" ),
            Element::of<&RestaurantCatalog::_ingredientCatalog>( "Ingredient" ).uncounted(),
            Element::of<&RestaurantCatalog::loadArrival>( "Arrival" ).uncounted(),
            Element::of<&RestaurantCatalog::_weatherCatalog>( "Weather" ).uncounted(),
        };
        return XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement )._loadedCount;
    }

    bool RestaurantCatalog::loadArrival( const XMLNode& node, string_view sourceName )
    {
        RestaurantArrivalDef arrival;
        if ( XMLFields::readProperties( node, arrival, sourceName ) == false )
            return false;
        _arrArrivalRate[arrival._hour] = arrival._rate;
        return true;
    }

    bool RestaurantCatalog::loadDish( const XMLNode& node, string_view sourceName )
    {
        DishDef    dish;
        const bool bValid = XMLFields::readProperties( node, dish, sourceName );
        VectorUtil::sort( dish._listQualityLevel );
        if ( bValid )
            addDish( dish );
        return bValid;
    }

    bool RestaurantCatalog::loadCustomer( const XMLNode& node, string_view sourceName )
    {
        CustomerTypeDef customerType;
        const bool      bValid = XMLFields::readProperties( node, customerType, sourceName );
        if ( bValid )
            addCustomerType( customerType );
        return bValid;
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<RestaurantCatalog>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( RestaurantCatalog, RestaurantCatalog, SW_KIT_API, RestaurantCatalog::kAssetType.withLoader( AssetTypeLoader::of<RestaurantCatalog>( MemoryTag::Game ) ) );
} // namespace sw
