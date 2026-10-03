/**
 * @file AdventureCooking.h
 * @brief 야생의 숨결의 요리 — 냄비에 넣은 재료(최대 다섯)의 효과를 합산합니다. 같은 효과 재료끼리는 효과 세기와 지속 시간이 더해지고,
 *        서로 다른 효과가 섞이면 효과가 사라진 수상한 요리가 됩니다. 회복량은 재료 회복의 합 × 배율입니다.
 * @details 요리 이름(꼬치 · 스튜)은 기반 `Crafting` 의 레시피입니다 — 작업대가 `_station` 이고 재료가 냄비와 꼭 같은 레시피가 있으면
 *          `Crafter::craft` 로 그 결과를 만들고, 없으면 일반 요리(`_genericDish`)가 됩니다. 효과 · 회복은 어느 쪽이든 이 파일의 규칙입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Crafter;
    class Inventory;
    class RecipeCatalog;
    class XmlNode;

    /** @brief 효과 하나의 단계 기준입니다. */
    struct AdventureCookEffectDef
    {
        hashed_string _id{};
        float32       _tier2Potency{ 4.0f }; ///< 세기 합이 이만큼이면 2 단계
        float32       _tier3Potency{ 7.0f };
        float32       _maxDuration{ 1800.0f };
    };

    /** @brief 재료 하나입니다. 효과가 없는 재료(사과 · 고기)는 어느 효과와도 섞입니다. */
    struct AdventureIngredientDef
    {
        hashed_string _id{};
        hashed_string _effect{};
        float32       _potency{ 0.0f };
        float32       _duration{ 0.0f }; ///< 초
        int32         _heartQuarters{ 0 };
    };

    /** @brief 요리 결과입니다. */
    struct AdventureDish
    {
        hashed_string _itemId{};
        hashed_string _effect{};
        float32       _duration{ 0.0f };
        float32       _potency{ 0.0f };
        int32         _effectTier{ 0 }; ///< 0 = 효과 없음 · 1..3
        int32         _heartQuarters{ 0 };
        uint8         _bDubious{ SW_FALSE }; ///< 서로 다른 효과가 섞였다
        uint8         _bNamedRecipe{ SW_FALSE };
    };

    /** @brief 요리 결과 코드입니다. */
    enum class AdventureCookResult : uint8
    {
        Ok = 0,
        EmptyPot,
        TooManyIngredients,
        UnknownIngredient,
        MissingIngredients, ///< 인벤토리에 없다
        NoRoom
    };

    SW_GF_API const utf8* toString( AdventureCookResult result );

    /**
     * @class AdventureCooking
     * @brief `<AdventureCooking maxIngredients="5" heartScale="2" durationPerIngredient="30" station="CookingPot" generic="simmeredDish"
     *        dubious="dubiousFood" dubiousHearts="4"><Effect id="Chilly" tier2="4" tier3="7" maxDuration="1800"/>
     *        <Ingredient id="hydromelon" effect="Chilly" potency="1" duration="150" hearts="2"/></AdventureCooking>` 를 읽습니다.
     */
    class SW_GF_API AdventureCooking
    {
    public:
        AdventureCooking();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );

        /** @brief 재료 목록(같은 재료 여러 번 가능)만 보고 요리 결과를 정합니다(인벤토리는 보지 않는다 — 미리 보기). */
        AdventureCookResult evaluate( const vector<hashed_string>& listIngredient, AdventureDish& outDish ) const;
        /**
         * @brief 인벤토리의 재료로 요리합니다. 재료가 꼭 같은 레시피(작업대 `_station`)가 있으면 그 결과 아이템, 아니면 일반 · 수상한 요리를 넣습니다.
         * @param level 레시피 레벨 조건에 넘길 값
         */
        AdventureCookResult cook( const vector<hashed_string>& listIngredient, Inventory& inventory, Crafter& crafter, const RecipeCatalog& recipes, int32 level,
                                  AdventureDish& outDish ) const;

        const AdventureIngredientDef* findIngredient( const hashed_string& id ) const { return _ingredientCatalog.find( id ); }
        const AdventureCookEffectDef* findEffect( const hashed_string& id ) const { return _effectCatalog.find( id ); }
        const hashed_string&          getStation() const { return _station; }
        int32                         getMaxIngredientCount() const { return _maxIngredientCount; }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<AdventureIngredientDef> _ingredientCatalog;
        GameCatalog<AdventureCookEffectDef> _effectCatalog;
        hashed_string                       _station;
        hashed_string                       _genericDish;
        hashed_string                       _dubiousDish;
        float32                             _heartScale;
        float32                             _durationPerIngredient; ///< 효과 요리면 재료 하나마다 더하는 초
        int32                               _maxIngredientCount;
        int32                               _dubiousHeartQuarters;
    };
} // namespace sw
