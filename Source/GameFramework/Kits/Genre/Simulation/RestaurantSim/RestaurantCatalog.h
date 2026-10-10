/**
 * @file RestaurantCatalog.h
 * @brief 식당 정의 — 메뉴(기반 `RecipeDef` 를 가리키는 요리 · 분류 · 기본 가격 · 숙련도별 품질), 재료 유통 기한, 손님 성향, 시간대별 도착, 날씨 배율, 영업 시간입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /**
     * @brief 메뉴의 요리 하나입니다. 재료 · 조리 시간 · 조리 스테이션 · 필요한 레벨은 기반 레시피(`RecipeCatalog`)에서 봅니다.
     * @details 품질은 만든 요리사 레벨이 `_listQualityLevel` 의 문턱을 몇 개 넘었는가입니다(최소 1 — "1,3,5" 면 레벨 1 → ★1, 3 → ★2, 5 → ★3).
     */
    struct SW_GF_API DishDef
    {
        hashed_string _id{};
        hashed_string _recipeId{};
        hashed_string _category{};         ///< "Noodle" · "Grill" · "Dessert" — 손님 선호와 맞춘다
        vector<int32> _listQualityLevel{}; ///< 품질 단계의 요리사 레벨 문턱(낮은 것부터)
        string        _name{};
        int32         _basePrice{ 10 };

        /** @brief 레벨 @p cookLevel 의 요리사가 만든 품질(1..최대)입니다. */
        int32 computeQuality( int32 cookLevel ) const;
        int32 getMaxQuality() const { return _listQualityLevel.empty() ? 1 : static_cast<int32>( _listQualityLevel.size() ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 재료 하나의 유통 기한입니다. 카탈로그에 없는 재료는 상하지 않습니다. */
    struct IngredientDef
    {
        hashed_string _id{};           ///< 아이템 id
        int32         _shelfLife{ 0 }; ///< 날 수(0 = 상하지 않는다)
    };
} // namespace sw

namespace sw
{
    /** @brief 손님 성향 하나입니다. */
    struct CustomerTypeDef
    {
        hashed_string         _id{};
        vector<hashed_string> _listCategory{};    ///< 좋아하는 분류(고를 때 `_preferredWeight` 배)
        float32               _weight{ 1.0f };    ///< 손님이 올 때 이 성향일 몫
        float32               _patience{ 30.0f }; ///< 자리 · 요리를 기다리는 분
        float32               _budget{ 1.2f };    ///< 지불 의사 — 가격 / 기본 가격이 이보다 크면 시키지 않는다
        float32               _tipRate{ 0.1f };   ///< 품질 최고 · 기다림 0 일 때 가격에 대한 팁 몫
        float32               _eatMinutes{ 15.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 날씨 하나의 손님 배율입니다. */
    struct RestaurantWeatherDef
    {
        hashed_string _id{};
        float32       _arrivalScale{ 1.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class RestaurantCatalog
     * @brief XML 형식입니다.
     * @code
     *     <RestaurantCatalog open="11" close="21" window="10">
     *       <Dish id="ramen" recipe="ramen" category="Noodle" price="40" quality="1,3,5"/>
     *       <Ingredient item="noodle" shelfLife="5"/>
     *       <Customer id="student" weight="3" categories="Noodle" patience="30" budget="1.2" tipRate="0.1" eat="15"/>
     *       <Arrival hour="11" rate="6"/>
     *       <Weather id="rain" arrival="0.5"/>
     *     </RestaurantCatalog>
     * @endcode
     *          `Arrival` 은 그 시의 시간당 손님 수(적지 않은 시는 0)입니다. `window` 는 별점 이동 평균의 손님 수입니다.
     */
    class SW_GF_API RestaurantCatalog : public XMLCatalog<RestaurantCatalog>
    {
        friend class XMLCatalog<RestaurantCatalog>;

    public:
        static constexpr int32 kHoursPerDay = 24;

        RestaurantCatalog();

        void addDish( const DishDef& dish ) { (void)_dishCatalog.add( dish ); }
        void addCustomerType( const CustomerTypeDef& customerType ) { (void)_customerCatalog.add( customerType ); }

        const DishDef*                 findDish( const hashed_string& id ) const { return _dishCatalog.find( id ); }
        const CustomerTypeDef*         findCustomerType( const hashed_string& id ) const { return _customerCatalog.find( id ); }
        const IngredientDef*           findIngredient( const hashed_string& itemId ) const { return _ingredientCatalog.find( itemId ); }
        const vector<DishDef>&         getDishes() const { return _dishCatalog.getAll(); }
        const vector<CustomerTypeDef>& getCustomerTypes() const { return _customerCatalog.getAll(); }
        /** @brief 그 시의 시간당 손님 수입니다. */
        float32 getArrivalRate( int32 hour ) const;
        /** @brief 그 날씨의 손님 배율입니다(모르는 날씨는 1). */
        float32 findWeatherScale( const hashed_string& weatherId ) const;
        /** @brief 유통 기한 날 수입니다(0 = 상하지 않는다). */
        int32 findShelfLife( const hashed_string& itemId ) const;
        int32 getOpenHour() const { return _openHour; }
        int32 getCloseHour() const { return _closeHour; }
        int32 getRatingWindow() const { return _ratingWindow; }

    private:
        static constexpr const utf8* kXMLRootName = "RestaurantCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );

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
