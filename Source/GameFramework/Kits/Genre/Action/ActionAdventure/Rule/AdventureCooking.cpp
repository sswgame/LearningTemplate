#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureCooking.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Base/Gameplay/Inventory/Crafting.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    SW_LOG_CALLER( "AdventureCooking" );

    namespace
    {
        /** @brief 레시피의 재료가 냄비와 똑같은지(종류와 개수 모두) 확인합니다. */
        bool isSameItems( const ItemStackList& lhs, const ItemStackList& rhs )
        {
            if ( lhs.getItems().size() != rhs.getItems().size() )
                return false;
            for ( const auto& [itemID, count] : lhs.getItems() )
            {
                if ( rhs.getItemCount( itemID ) != count )
                    return false;
            }
            return true;
        }
    } // namespace
} // namespace sw

namespace sw
{
    AdventureCooking::AdventureCooking()
        : _ingredientCatalog{}
        , _effectCatalog{}
        , _rules{}
    {
    }

    uint32 AdventureCooking::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 범위 밖 · 모르는 속성 · 깨진 값은 그 원소를 넣지 않고 오류다(readFields 가 남긴다).
        AdventureCookingRulesDef rules;
        if ( GameDataXML::readFields( root, rules, sourceName ) == false )
            return 0;
        _rules = rules;
        for ( XMLNode node = root.findChild( "Effect" ); node; node = node.findNextSibling( "Effect" ) )
        {
            AdventureCookEffectDef effect;
            if ( GameDataXML::readFields( node, effect, sourceName ) )
                (void)_effectCatalog.add( effect ); // 같은 id 는 카탈로그가 알리고 뒤의 것을 버린다
        }
        uint32 loadedCount = 0;
        for ( XMLNode node = root.findChild( "Ingredient" ); node; node = node.findNextSibling( "Ingredient" ) )
        {
            AdventureIngredientDef ingredient;
            if ( GameDataXML::readFields( node, ingredient, sourceName ) == false )
                continue;
            if ( ingredient._effect.empty() == false && _effectCatalog.find( ingredient._effect ) == nullptr )
            {
                SW_LOG_ERROR( "%#: ingredient '%#' has an undeclared effect '%#' - not added", sourceName, ingredient._id.c_str(), ingredient._effect.c_str() );
                continue;
            }
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
        if ( static_cast<int32>( listIngredient.size() ) > _rules._maxIngredientCount )
            return AdventureCookResult::TooManyIngredients;
        int32         heartQuarters = 0;
        float32       potency       = 0.0f;
        float32       duration      = 0.0f;
        hashed_string effect{};
        bool          bMixed = false;
        for ( const hashed_string& ingredientID : listIngredient )
        {
            const AdventureIngredientDef* pIngredient = _ingredientCatalog.find( ingredientID );
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
            // 서로 다른 효과는 서로를 지운다. 효과도 회복 배율도 없는 수상한 요리가 된다.
            outDish._itemID        = _rules._dubiousDish;
            outDish._heartQuarters = _rules._dubiousHeartQuarters;
            outDish._bDubious      = SW_TRUE;
            return AdventureCookResult::Ok;
        }
        outDish._itemID        = _rules._genericDish;
        outDish._heartQuarters = static_cast<int32>( static_cast<float32>( heartQuarters ) * _rules._heartScale + 0.5f );
        if ( effect.empty() )
            return AdventureCookResult::Ok;
        const AdventureCookEffectDef* pEffect = _effectCatalog.find( effect );
        outDish._effect                       = effect;
        outDish._potency                      = potency;
        outDish._duration                     = duration + _rules._durationPerIngredient * static_cast<float32>( listIngredient.size() );
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
        for ( const hashed_string& ingredientID : listIngredient )
        {
            pot.addItem( ingredientID, 1 );
        }
        if ( inventory.hasItems( pot ) == false )
            return AdventureCookResult::MissingIngredients;

        // 이름이 있는 요리를 만든다. 재료가 똑같은 냄비 레시피가 있으면 기반 제작으로 만든다(수상한 요리는 이름이 없다).
        if ( outDish._bDubious == SW_FALSE )
        {
            for ( const RecipeDef& recipe : recipes.getAll() )
            {
                const bool bSingleOutput   = recipe._outputs.getItems().size() == 1;
                const bool bRecipeMismatch = ( recipe._station == _rules._station ) == false || bSingleOutput == false || isSameItems( recipe._inputs, pot ) == false;
                if ( bRecipeMismatch )
                    continue;
                const CraftResult craftResult = crafter.craft( recipe._id, inventory, _rules._station, level );
                if ( craftResult == CraftResult::NoRoom )
                    return AdventureCookResult::NoRoom;
                if ( craftResult != CraftResult::Ok )
                    break; // 아직 모르는 레시피이거나 레벨이 부족하면 일반 요리로 넘어간다
                outDish._itemID       = recipe._outputs.getItems().front()._itemID;
                outDish._bNamedRecipe = SW_TRUE;
                return AdventureCookResult::Ok;
            }
        }

        for ( const auto& [itemID, count] : pot.getItems() )
        {
            (void)inventory.removeItem( itemID, count ); // 재료는 위의 hasItems 가 확인했다
        }
        if ( inventory.addItem( outDish._itemID, 1 ) == 1 )
            return AdventureCookResult::Ok;
        for ( const auto& [itemID, count] : pot.getItems() )
        {
            (void)inventory.addItem( itemID, count );
        }
        return AdventureCookResult::NoRoom;
    }
} // namespace sw
