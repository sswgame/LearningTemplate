/**
 * @file CraftingDemoComponent.h
 * @brief 제작(`Crafter`)의 시연입니다 — 같은 씬 캐릭터의 가방에서 재료를 꺼내 작업대 대기열에 넣고, 끝난 결과를 가방에 넣습니다.
 * @details 레시피는 데이터(`*.recipes.xml`)이고 명령은 `craft.enqueue` · `craft.cancel` · `craft.learn` · `craft.level` 이다. 대기열과 배운 레시피는 세이브가 싣는다(가방은 장비 컴포넌트가 싣는다).
 *          시나리오: `game/playground/automation/crafting.scenario.xml`(무대 maps/inventorydemo).
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Crafting.h"

namespace sw
{
    class CharacterEquipmentComponent;
    class Inventory;

    /**
     * @class CraftingDemoComponent
     * @brief 작업대 하나의 대기열을 프레임마다 흘립니다. 결과를 넣을 자리가 없으면 그 작업은 기다립니다.
     */
    REFLECT( Category = "FeatureDemo", DisplayName = "Crafting Demo", Tooltip = "Crafting queue of one station over a character's bag" )
    class CraftingDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        CraftingDemoComponent();
        virtual ~CraftingDemoComponent() override;

        /** @brief 레시피를 @p count 번 대기열에 넣습니다. 재료는 지금 가방에서 나갑니다. 캐릭터가 없으면 비어 있습니다. */
        optional<CraftResult> enqueue( const hashed_string& recipeID, int32 count );
        /** @brief 대기열 @p index 를 빼고 재료를 돌려받습니다. 돌려줄 자리가 없거나 없는 자리면 false 입니다. */
        [[nodiscard]] bool cancel( int32 index );
        /** @brief 레시피를 배웁니다. */
        void learn( const hashed_string& recipeID ) { _crafter.learnRecipe( recipeID ); }
        /** @brief 제작 레벨을 정합니다. */
        void setLevel( int32 level ) { _level = level; }

        const hashed_string& getLastResult() const { return _lastResult; }

    protected:
        void               serializeGameState( StateIO& io ) override;
        [[nodiscard]] bool startGame() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 같은 씬 캐릭터의 가방입니다(장비 컴포넌트가 시작했을 때만). */
        [[nodiscard]] Inventory* findBag() const;

    private:
        PROPERTY( Category = "Data", DisplayName = "Recipes", Tooltip = "Recipe catalog (*.recipes.xml)" )
        AssetRef<RecipeCatalog> _recipePath;
        PROPERTY( Category = "Data", DisplayName = "Station", Tooltip = "Station id recipes ask for" )
        hashed_string _station;
        PROPERTY( Category = "Data", DisplayName = "Start Level", Min = 1 )
        int32 _startLevel;
        PROPERTY( Category = "Scene", DisplayName = "Character", Tooltip = "Object with the CharacterEquipmentComponent whose bag is used" )
        hashed_string _characterName;

        PROPERTY( RuntimeState, Min = 0 )
        int32 _level;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _finishedCount;
        PROPERTY( RuntimeState, Min = 0 )
        int32 _queueCount;
        PROPERTY( RuntimeState )
        hashed_string _lastResult; ///< 마지막 명령의 `CraftResult` 이름

        SharedAssetRef<RecipeCatalog> _pRecipes; ///< 제작이 빌려 쓴다
        Crafter                       _crafter;
    };
} // namespace sw
