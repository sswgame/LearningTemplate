/**
 * @file RestaurantCatalog.h
 * @brief 식당 정의를 담습니다. 메뉴(기반 `RecipeDef` 를 가리키는 요리, 분류, 기본 가격, 숙련도별 품질), 재료 유통 기한, 손님 성향, 시간대별 도착, 날씨 배율, 영업 시간을 정의합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /**
     * @brief 메뉴의 요리 하나입니다. 재료, 조리 시간, 조리 스테이션, 필요한 레벨은 기반 레시피(`RecipeCatalog`)에서 봅니다.
     * @details 품질은 만든 요리사 레벨이 `_listQualityLevel` 의 임계값을 몇 개 넘었는지로 정한다(최소 1이며, "1,3,5" 이면 레벨 1 은 별 1개, 3 은 별 2개, 5 는 별 3개).
     */
    REFLECT()
    struct SW_KIT_API DishDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Required )
        string _name{};
        PROPERTY( Name = "recipe", Required )
        hashed_string _recipeID{};
        PROPERTY()
        hashed_string _category{}; ///< "Noodle", "Grill", "Dessert" 같은 분류이다(손님 선호와 맞춘다)
        PROPERTY( Name = "price", Min = 1 )
        int32 _basePrice{ 10 };

        vector<int32> _listQualityLevel{}; ///< 품질 단계의 요리사 레벨 임계값이다(낮은 것부터, `quality` 를 손으로 읽는다)

        /** @brief 레벨 @p cookLevel 의 요리사가 만든 품질(1..최대)을 반환합니다. */
        int32 computeQuality( int32 cookLevel ) const;
        int32 getMaxQuality() const { return _listQualityLevel.empty() ? 1 : static_cast<int32>( _listQualityLevel.size() ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 재료 하나의 유통 기한입니다. 카탈로그에 없는 재료는 상하지 않습니다. */
    REFLECT()
    struct IngredientDef
    {
        REFLECT_BODY();

        PROPERTY( Name = "item", Required )
        hashed_string _id{}; ///< 아이템 id
        PROPERTY( Min = 0 )
        int32 _shelfLife{ 0 }; ///< 날 수이다(0 이면 상하지 않는다)
    };
} // namespace sw

namespace sw
{
    /** @brief 손님 성향 하나입니다. */
    REFLECT()
    struct CustomerTypeDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Min = 0.0 )
        float32 _weight{ 1.0f }; ///< 손님이 올 때 이 성향일 가중치
        PROPERTY( Min = 1.0 )
        float32 _patience{ 30.0f }; ///< 자리나 요리를 기다리는 시간(분)
        PROPERTY( Min = 0.0 )
        float32 _budget{ 1.2f }; ///< 지불 의사이다. 가격 / 기본 가격이 이 값보다 크면 시키지 않는다
        PROPERTY( Min = 0.0 )
        float32 _tipRate{ 0.1f }; ///< 품질이 최고이고 기다림이 0 일 때 가격에 대한 팁 비율
        PROPERTY( Name = "eat", Min = 0.0 )
        float32 _eatMinutes{ 15.0f };

        vector<hashed_string> _listCategory{}; ///< 좋아하는 분류이다(`categories` 를 손으로 읽는다. 고를 때 `_preferredWeight` 배로 반영한다)
    };
} // namespace sw

namespace sw
{
    /** @brief 날씨 하나의 손님 배율입니다. */
    REFLECT()
    struct RestaurantWeatherDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Name = "arrival", Required, Min = 0.0 )
        float32 _arrivalScale{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief `<Arrival>` 한 줄의 스키마입니다. 그 시각의 시간당 손님 수입니다. */
    REFLECT()
    struct RestaurantArrivalDef
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 0, Max = 23 )
        int32 _hour{ 0 };
        PROPERTY( Required, Min = 0.0 )
        float32 _rate{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief `<RestaurantCatalog>` 루트 속성의 스키마입니다. 닫는 시각은 여는 시각보다 늦어야 합니다(읽는 쪽이 확인한다). */
    REFLECT()
    struct RestaurantCatalogRootDef
    {
        REFLECT_BODY();

        PROPERTY( Name = "open", Min = 0, Max = 23 )
        int32 _openHour{ 11 };
        PROPERTY( Name = "close", Min = 1, Max = 24 )
        int32 _closeHour{ 21 };
        PROPERTY( Name = "window", Min = 1 )
        int32 _ratingWindow{ 10 }; ///< 별점 이동 평균에 쓰는 손님 수
    };
} // namespace sw

namespace sw
{
    /**
     * @class RestaurantCatalog
     * @brief 식당 카탈로그의 XML 형식입니다.
     * @code
     *     <RestaurantCatalog open="11" close="21" window="10">
     *       <Dish id="ramen" name="Ramen" recipe="ramen" category="Noodle" price="40" quality="1,3,5"/>
     *       <Ingredient item="noodle" shelfLife="5"/>
     *       <Customer id="student" weight="3" categories="Noodle" patience="30" budget="1.2" tipRate="0.1" eat="15"/>
     *       <Arrival hour="11" rate="6"/>
     *       <Weather id="rain" arrival="0.5"/>
     *     </RestaurantCatalog>
     * @endcode
     * @details `Arrival` 은 그 시각의 시간당 손님 수이다(적지 않은 시각은 0). `window` 는 별점 이동 평균에 쓰는 손님 수이다.
     */
    class SW_KIT_API RestaurantCatalog : public XMLCatalog<RestaurantCatalog>
    {
        friend class XMLCatalog<RestaurantCatalog>;

    public:
        static constexpr int32 kHoursPerDay = 24;

        RestaurantCatalog();

        void addDish( const DishDef& dish ) { (void)_dishCatalog.add( dish ); }
        void addCustomerType( const CustomerTypeDef& customerType ) { (void)_customerCatalog.add( customerType ); }

        const DishDef*                 findDish( const hashed_string& id ) const { return _dishCatalog.find( id ); }
        const CustomerTypeDef*         findCustomerType( const hashed_string& id ) const { return _customerCatalog.find( id ); }
        const IngredientDef*           findIngredient( const hashed_string& itemID ) const { return _ingredientCatalog.find( itemID ); }
        const vector<DishDef>&         getDishes() const { return _dishCatalog.getAll(); }
        const vector<CustomerTypeDef>& getCustomerTypes() const { return _customerCatalog.getAll(); }
        /** @brief 그 시각의 시간당 손님 수를 반환합니다. */
        float32 getArrivalRate( int32 hour ) const;
        /** @brief 그 날씨의 손님 배율을 반환합니다(모르는 날씨는 1). */
        float32 findWeatherScale( const hashed_string& weatherID ) const;
        /** @brief 유통 기한 날 수를 반환합니다(0 이면 상하지 않습니다). */
        int32 findShelfLife( const hashed_string& itemID ) const;
        int32 getOpenHour() const { return _openHour; }
        int32 getCloseHour() const { return _closeHour; }
        int32 getRatingWindow() const { return _ratingWindow; }

    private:
        static constexpr const utf8* kXMLRootName = "RestaurantCatalog"; ///< XML 루트 원소 이름이다(`XMLCatalog` 이 사용한다)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief `<Dish>` 하나를 스키마로 읽습니다. 오류면 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadDish( const XMLNode& node, string_view sourceName );
        /** @brief `<Customer>` 하나를 스키마로 읽습니다. 오류면 넣지 않고 false 입니다. */
        [[nodiscard]] bool loadCustomer( const XMLNode& node, string_view sourceName );

        GameCatalog<DishDef>              _dishCatalog;
        GameCatalog<IngredientDef>        _ingredientCatalog;
        GameCatalog<CustomerTypeDef>      _customerCatalog;
        GameCatalog<RestaurantWeatherDef> _weatherCatalog;
        float32                           _arrArrivalRate[kHoursPerDay];
        int32                             _openHour;
        int32                             _closeHour;
        int32                             _ratingWindow;
    };
} // namespace sw
