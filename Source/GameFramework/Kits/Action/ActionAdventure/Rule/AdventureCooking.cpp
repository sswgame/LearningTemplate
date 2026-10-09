#include "pch.h"

#include "GameFramework/Kits/Action/ActionAdventure/Rule/AdventureCooking.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Foundation/Data/GameDataXml.h"
#include "GameFramework/Base/Gameplay/Inventory/Crafting.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    SW_LOG_CALLER( "AdventureCooking" );

    namespace
    {
        struct AdventureCookingInternal
        {
            static hashed_string readName( const XmlNode& node, const utf8* pName, const utf8* pFallback )
            {
                const utf8* pValue = node.findAttribute( pName );
                return hashed_string( pValue != nullptr && pValue[0] != '\0' ? pValue : pFallback );
            }

            /** @brief 레시피의 재료가 냄비와 꼭 같은가(종류도 개수도)입니다. */
            static bool isSameItems( const ItemStackList& lhs, const ItemStackList& rhs )
            {
                if ( lhs.getItems().size() != rhs.getItems().size() )
                    return false;
                for ( const auto& [itemId, count] : lhs.getItems() )
                {
                    if ( rhs.getItemCount( itemId ) != count )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( AdventureCookResult result )
    {
        switch ( result )
        {
            case AdventureCookResult::Ok:
                return "Ok";
            case AdventureCookResult::EmptyPot:
                return "EmptyPot";
            case AdventureCookResult::TooManyIngredients:
                return "TooManyIngredients";
            case AdventureCookResult::UnknownIngredient:
                return "UnknownIngredient";
            case AdventureCookResult::MissingIngredients:
                return "MissingIngredients";
            case AdventureCookResult::NoRoom:
                return "NoRoom";
        }
        return "Unknown";
    }

    AdventureCooking::AdventureCooking()
        : _ingredientCatalog{}
        , _effectCatalog{}
        , _station{ "CookingPot" }
        , _genericDish{ "simmeredDish" }
        , _dubiousDish{ "dubiousFood" }
        , _heartScale{ 2.0f }
        , _durationPerIngredient{ 30.0f }
        , _maxIngredientCount{ 5 }
        , _dubiousHeartQuarters{ 4 }
    {
    }

    uint32 AdventureCooking::loadRoot( const XmlNode& root, string_view sourceName )
    {
        _station               = AdventureCookingInternal::readName( root, "station", "CookingPot" );
        _genericDish           = AdventureCookingInternal::readName( root, "generic", "simmeredDish" );
        _dubiousDish           = AdventureCookingInternal::readName( root, "dubious", "dubiousFood" );
        _heartScale            = MathUtil::max( 0.0f, root.getAttributeFloat( "heartScale", _heartScale ) );
        _durationPerIngredient = MathUtil::max( 0.0f, root.getAttributeFloat( "durationPerIngredient", _durationPerIngredient ) );
        _maxIngredientCount    = MathUtil::max( 1, root.getAttributeInt( "maxIngredients", _maxIngredientCount ) );
        _dubiousHeartQuarters  = MathUtil::max( 0, root.getAttributeInt( "dubiousHearts", _dubiousHeartQuarters ) );
        for ( XmlNode node = root.findChild( "Effect" ); node; node = node.findNextSibling( "Effect" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            AdventureCookEffectDef effect;
            effect._id           = hashed_string( pId );
            effect._tier2Potency = MathUtil::max( 0.0f, node.getAttributeFloat( "tier2", effect._tier2Potency ) );
            effect._tier3Potency = MathUtil::max( effect._tier2Potency, node.getAttributeFloat( "tier3", effect._tier3Potency ) );
            effect._maxDuration  = MathUtil::max( 0.0f, node.getAttributeFloat( "maxDuration", effect._maxDuration ) );
            (void)_effectCatalog.add( effect );
        }
        uint32 loadedCount = 0;
        for ( XmlNode node = root.findChild( "Ingredient" ); node; node = node.findNextSibling( "Ingredient" ) )
        {
            const utf8* pId = GameDataXml::findRequiredId( node, sourceName );
            if ( pId == nullptr )
                continue;
            AdventureIngredientDef ingredient;
            ingredient._id            = hashed_string( pId );
            const utf8* pEffect       = node.findAttribute( "effect" );
            ingredient._effect        = pEffect != nullptr ? hashed_string( pEffect ) : hashed_string{};
            ingredient._potency       = MathUtil::max( 0.0f, node.getAttributeFloat( "potency", 0.0f ) );
            ingredient._duration      = MathUtil::max( 0.0f, node.getAttributeFloat( "duration", 0.0f ) );
            ingredient._heartQuarters = MathUtil::max( 0, node.getAttributeInt( "hearts", 0 ) );
            if ( ingredient._effect.empty() == false && _effectCatalog.find( ingredient._effect ) == nullptr )
                SW_LOG_WARNING( "%#: ingredient '%#' has an undeclared effect '%#' - tier stays 1", sourceName, pId, ingredient._effect.c_str() );
            (void)_ingredientCatalog.add( ingredient );
            ++loadedCount;
        }
        return loadedCount;
    }

    AdventureCookResult AdventureCooking::evaluate( const vector<hashed_string>& listIngredient, AdventureDish& outDish ) const
    {
        outDish = AdventureDish{};
        if ( listIngredient.empty() )
            return AdventureCookResult::EmptyPot;
        if ( static_cast<int32>( listIngredient.size() ) > _maxIngredientCount )
            return AdventureCookResult::TooManyIngredients;
        int32         heartQuarters = 0;
        float32       potency       = 0.0f;
        float32       duration      = 0.0f;
        hashed_string effect{};
        bool          bMixed = false;
        for ( const hashed_string& ingredientId : listIngredient )
        {
            const AdventureIngredientDef* pIngredient = _ingredientCatalog.find( ingredientId );
            if ( pIngredient == nullptr )
                return AdventureCookResult::UnknownIngredient;
            heartQuarters += pIngredient->_heartQuarters;
            if ( pIngredient->_effect.empty() )
                continue;
            if ( effect.empty() )
                effect = pIngredient->_effect;
            else if ( ( effect == pIngredient->_effect ) == false )
                bMixed = true;
            potency += pIngredient->_potency;
            duration += pIngredient->_duration;
        }
        if ( bMixed )
        {
            // 서로 다른 효과는 서로를 지운다 — 효과도 회복 배율도 없는 수상한 요리.
            outDish._itemId        = _dubiousDish;
            outDish._heartQuarters = _dubiousHeartQuarters;
            outDish._bDubious      = SW_TRUE;
            return AdventureCookResult::Ok;
        }
        outDish._itemId        = _genericDish;
        outDish._heartQuarters = static_cast<int32>( static_cast<float32>( heartQuarters ) * _heartScale + 0.5f );
        if ( effect.empty() )
            return AdventureCookResult::Ok;
        const AdventureCookEffectDef* pEffect = _effectCatalog.find( effect );
        outDish._effect                       = effect;
        outDish._potency                      = potency;
        outDish._duration                     = duration + _durationPerIngredient * static_cast<float32>( listIngredient.size() );
        outDish._effectTier                   = 1;
        if ( pEffect != nullptr )
        {
            if ( potency >= pEffect->_tier3Potency )
                outDish._effectTier = 3;
            else if ( potency >= pEffect->_tier2Potency )
                outDish._effectTier = 2;
            outDish._duration = MathUtil::min( outDish._duration, pEffect->_maxDuration );
        }
        return AdventureCookResult::Ok;
    }

    AdventureCookResult AdventureCooking::cook( const vector<hashed_string>& listIngredient, Inventory& inventory, Crafter& crafter, const RecipeCatalog& recipes,
                                                int32 level, AdventureDish& outDish ) const
    {
        const AdventureCookResult result = evaluate( listIngredient, outDish );
        if ( result != AdventureCookResult::Ok )
            return result;
        ItemStackList pot;
        for ( const hashed_string& ingredientId : listIngredient )
        {
            pot.addItem( ingredientId, 1 );
        }
        if ( inventory.hasItems( pot ) == false )
            return AdventureCookResult::MissingIngredients;

        // 이름 붙은 요리 — 재료가 꼭 같은 냄비 레시피가 있으면 기반 제작으로 만든다(수상한 요리는 이름이 없다).
        if ( outDish._bDubious == SW_FALSE )
        {
            for ( const RecipeDef& recipe : recipes.getRecipes() )
            {
                const bool bSingleOutput = recipe._outputs.getItems().size() == 1;
                if ( ( recipe._station == _station ) == false || bSingleOutput == false || AdventureCookingInternal::isSameItems( recipe._inputs, pot ) == false )
                    continue;
                const CraftResult craftResult = crafter.craft( recipe._id, inventory, _station, level );
                if ( craftResult == CraftResult::NoRoom )
                    return AdventureCookResult::NoRoom;
                if ( craftResult != CraftResult::Ok )
                    break; // 아직 모르는 레시피 · 레벨 부족 — 일반 요리로
                outDish._itemId       = recipe._outputs.getItems().front()._itemId;
                outDish._bNamedRecipe = SW_TRUE;
                return AdventureCookResult::Ok;
            }
        }

        for ( const auto& [itemId, count] : pot.getItems() )
        {
            (void)inventory.removeItem( itemId, count ); // 재료는 위 hasItems 가 확인했다
        }
        if ( inventory.addItem( outDish._itemId, 1 ) == 1 )
            return AdventureCookResult::Ok;
        for ( const auto& [itemId, count] : pot.getItems() )
        {
            (void)inventory.addItem( itemId, count );
        }
        return AdventureCookResult::NoRoom;
    }
} // namespace sw
