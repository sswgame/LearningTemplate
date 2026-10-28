/**
 * @file AdventureCooking.h
 * @brief 야생의 숨결의 요리 규칙입니다. 냄비에 넣은 재료(최대 다섯 개)의 효과를 합산합니다.
 *        같은 효과의 재료끼리는 효과 세기와 지속 시간이 더해지고, 서로 다른 효과가 섞이면 효과가 사라진 수상한 요리가 됩니다. 회복량은 재료 회복량의 합에 배율을 곱한 값입니다.
 * @details 요리 이름(꼬치, 스튜)은 기반 `Crafting` 의 레시피로 정한다. 작업대가 `_station` 이고 재료가 냄비와 똑같은 레시피가 있으면 `Crafter::craft` 로 그 결과를 만들고, 없으면 일반 요리(`_genericDish`)가 된다.
 *          효과와 회복량은 어느 쪽이든 이 파일의 규칙으로 정한다. 정의 구조체는 리플렉션 스키마다 — 기본값은 초기값, 범위는 `PROPERTY( Min · Max )`, 필수는 `Required` 이고
 *          `GameDataXML::readFields` 가 읽는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/GameCatalog.h"
#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Crafter;
    class Inventory;
    class RecipeCatalog;
    class XMLNode;

    /** @brief 효과 하나의 단계 기준입니다. */
    REFLECT()
    struct AdventureCookEffectDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY( Name = "tier2", Min = 0.0 )
        float32 _tier2Potency{ 4.0f }; ///< 세기 합이 이만큼이면 2단계
        PROPERTY( Name = "tier3", Min = "_tier2Potency" )
        float32 _tier3Potency{ 7.0f };
        PROPERTY( Min = 0.0 )
        float32 _maxDuration{ 1800.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 재료 하나입니다. 효과가 없는 재료(사과, 고기)는 어느 효과와도 섞입니다. 효과가 있으면 앞에 적은 `<Effect>` 여야 합니다. */
    REFLECT()
    struct AdventureIngredientDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _id{};
        PROPERTY()
        hashed_string _effect{};
        PROPERTY( Min = 0.0 )
        float32 _potency{ 0.0f };
        PROPERTY( Min = 0.0 )
        float32 _duration{ 0.0f }; ///< 단위는 초
        PROPERTY( Name = "hearts", Min = 0 )
        int32 _heartQuarters{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 루트 `<AdventureCooking>` 의 속성(요리 규칙)입니다. */
    REFLECT()
    struct AdventureCookingRulesDef
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _station{ "CookingPot" }; ///< 레시피의 작업대
        PROPERTY( Name = "generic" )
        hashed_string _genericDish{ "simmeredDish" }; ///< 레시피가 없을 때의 요리
        PROPERTY( Name = "dubious" )
        hashed_string _dubiousDish{ "dubiousFood" }; ///< 서로 다른 효과가 섞였을 때의 요리
        PROPERTY( Min = 0.0 )
        float32 _heartScale{ 2.0f };
        PROPERTY( Min = 0.0 )
        float32 _durationPerIngredient{ 30.0f }; ///< 효과 요리에서 재료 하나마다 더하는 시간(초)
        PROPERTY( Name = "maxIngredients", Min = 1 )
        int32 _maxIngredientCount{ 5 };
        PROPERTY( Name = "dubiousHearts", Min = 0 )
        int32 _dubiousHeartQuarters{ 4 };
    };
} // namespace sw

namespace sw
{
    /** @brief 요리 결과입니다. */
    struct AdventureDish
    {
        hashed_string _itemID{};
        hashed_string _effect{};
        float32       _duration{ 0.0f };
        float32       _potency{ 0.0f };
        int32         _effectTier{ 0 }; ///< 0은 효과 없음, 1~3은 단계
        int32         _heartQuarters{ 0 };
        uint8         _bDubious{ SW_FALSE }; ///< 서로 다른 효과가 섞였다
        uint8         _bNamedRecipe{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 요리 결과 코드입니다. */
    ENUM()
    enum class AdventureCookResult : uint8
    {
        Ok = 0,
        EmptyPot,
        TooManyIngredients,
        UnknownIngredient,
        MissingIngredients, ///< 인벤토리에 재료가 없다
        NoRoom
    };

    /**
     * @class AdventureCooking
     * @brief 다음 형식의 XML 을 읽는 카탈로그입니다. `<AdventureCooking maxIngredients="5" heartScale="2" durationPerIngredient="30" station="CookingPot" generic="simmeredDish"
     *        dubious="dubiousFood" dubiousHearts="4"><Effect id="Chilly" tier2="4" tier3="7" maxDuration="1800"/>
     *        <Ingredient id="hydromelon" effect="Chilly" potency="1" duration="150" hearts="2"/></AdventureCooking>`
     */
    class SW_KIT_API AdventureCooking : public XMLCatalog<AdventureCooking>
    {
        friend class XMLCatalog<AdventureCooking>;

    public:
        AdventureCooking();

        /** @brief 재료 목록(같은 재료를 여러 번 넣을 수 있다)만 보고 요리 결과를 정합니다. 인벤토리는 보지 않으므로 미리 보기에 씁니다. */
        AdventureCookResult evaluate( const vector<hashed_string>& listIngredient, AdventureDish& outDish ) const;
        /**
         * @brief 인벤토리의 재료로 요리합니다. 재료가 똑같은 레시피(작업대 `_station`)가 있으면 그 결과 아이템을 넣고, 없으면 일반 요리나 수상한 요리를 넣습니다.
         * @param level 레시피 레벨 조건에 넘길 값
         */
        AdventureCookResult cook( const vector<hashed_string>& listIngredient, Inventory& inventory, Crafter& crafter, const RecipeCatalog& recipes, int32 level,
                                  AdventureDish& outDish ) const;

        const AdventureIngredientDef* findIngredient( const hashed_string& id ) const { return _ingredientCatalog.find( id ); }
        const AdventureCookEffectDef* findEffect( const hashed_string& id ) const { return _effectCatalog.find( id ); }
        const hashed_string&          getStation() const { return _rules._station; }
        int32                         getMaxIngredientCount() const { return _rules._maxIngredientCount; }

    private:
        static constexpr const utf8* kXMLRootName = "AdventureCooking"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );

        GameCatalog<AdventureIngredientDef> _ingredientCatalog;
        GameCatalog<AdventureCookEffectDef> _effectCatalog;
        AdventureCookingRulesDef            _rules;
    };
} // namespace sw
