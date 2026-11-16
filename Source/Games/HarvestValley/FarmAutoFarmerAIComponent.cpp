#include "pch.h"

#include "Games/HarvestValley/FarmAutoFarmerAIComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/AI/Blackboard.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Runtime/World/Environment/WorldClock.h"

#include "Games/HarvestValley/FarmGameModeComponent.h"

namespace sw
{
    namespace
    {
        struct FarmAutoFarmerAIComponentInternal
        {
            /** @brief 지친 것으로 보는 체력 · 시각입니다(이보다 적거나 늦으면 출하하고 잔다). */
            static constexpr int32   kTiredStamina = 8;
            static constexpr float32 kTiredHour    = 22.0f;

            /** @brief 칸에 할 일 하나의 종류와 블랙보드 키입니다. 표의 순서가 칸 하나를 가르는 순서다(거둘 것 → 물 → 심기 → 갈기). */
            enum class TileJob : uint8
            {
                Harvest = 0,
                Water,
                Sow,
                Till,
                Count
            };
            static constexpr const utf8* kArrTileKey[] = { "HarvestTile", "WaterTile", "SowTile", "TillTile" };
            static_assert( std::size( kArrTileKey ) == static_cast<size_t>( TileJob::Count ), "one blackboard key per tile job" );

            /** @brief 손에 든 도구의 참거짓 키입니다(`FarmTool` 순서). */
            static constexpr const utf8* kArrHoldingKey[] = { "HoldingHoe", "HoldingWateringCan", "HoldingSeeds", "HoldingHand" };

            /** @brief 이번 계절에 자라는 씨앗의 순번입니다(가진 것 먼저). 없으면 -1 입니다. */
            static int32 findSeasonalSeed( const CropCatalog& catalog, const Inventory& bag, const vector<hashed_string>& listSeed, const hashed_string& season )
            {
                int32 seasonalSeed = -1;
                for ( int32 seedIndex = 0; seedIndex < static_cast<int32>( listSeed.size() ); ++seedIndex )
                {
                    const CropDef* pCrop = catalog.findCropBySeed( listSeed[static_cast<size_t>( seedIndex )] );
                    if ( pCrop == nullptr || pCrop->canGrowIn( season ) == false )
                        continue;
                    if ( seasonalSeed < 0 || bag.getItemCount( pCrop->_seedItem ) > 0 )
                        seasonalSeed = seedIndex;
                    if ( bag.getItemCount( pCrop->_seedItem ) > 0 )
                        break;
                }
                return seasonalSeed;
            }

            /** @brief 가방에 든 수확물의 수입니다. */
            static int32 countProduce( const CropCatalog& catalog, const Inventory& bag )
            {
                int32 produceCount = 0;
                for ( const CropDef& crop : catalog.getCrops() )
                {
                    produceCount += bag.getItemCount( crop._produceItem );
                }
                return produceCount;
            }

            /**
             * @brief 칸을 칸 순서로 훑어 일마다 첫 칸의 선 자리(칸 아래 한 칸)를 적고, 그 일의 칸이 없으면 키를 지웁니다.
             * @details 칸 하나는 한 일로만 센다(거둘 것 → 물 → 심기 → 갈기 순서로 처음 맞는 것). 트리가 일의 우선순위대로 키를 물으므로
             *          "가장 급한 일의 첫 칸" 을 고르는 것과 같다. 갈기는 갈린 칸이 @p cultivateLimit 보다 적고 이번 계절 씨앗이 있을 때만 센다.
             */
            static void writeTileJob( const FarmField& field, int32 cultivateLimit, bool bHasSeasonalSeed, bool bHasSeed, Blackboard& blackboard )
            {
                int32 cultivatedCount = 0;
                for ( int32 y = 0; y < FarmGameModeComponent::kFieldHeight; ++y )
                {
                    for ( int32 x = 0; x < FarmGameModeComponent::kFieldWidth; ++x )
                    {
                        cultivatedCount += field.findTile( x, y )->_bTilled != SW_FALSE ? 1 : 0;
                    }
                }
                bool arrFound[static_cast<size_t>( TileJob::Count )]{};
                for ( int32 y = 0; y < FarmGameModeComponent::kFieldHeight; ++y )
                {
                    for ( int32 x = 0; x < FarmGameModeComponent::kFieldWidth; ++x )
                    {
                        const FarmTile* pTile         = field.findTile( x, y );
                        const bool      bCanSow       = pTile->_bTilled != SW_FALSE && pTile->hasCrop() == false && bHasSeed;
                        const bool      bCanCultivate = pTile->_bTilled == SW_FALSE && cultivatedCount < cultivateLimit && bHasSeasonalSeed;
                        TileJob         job           = TileJob::Count;
                        if ( pTile->_bReady != SW_FALSE || pTile->_bWithered != SW_FALSE )
                            job = TileJob::Harvest;
                        else if ( pTile->hasCrop() && pTile->_bWatered == SW_FALSE )
                            job = TileJob::Water;
                        else if ( bCanSow )
                            job = TileJob::Sow;
                        else if ( bCanCultivate )
                            job = TileJob::Till;
                        const size_t jobIndex = static_cast<size_t>( job );
                        if ( job == TileJob::Count || arrFound[jobIndex] )
                            continue;
                        arrFound[jobIndex] = true;
                        blackboard.setVector( hashed_string{ string_view{ kArrTileKey[jobIndex] } }, FarmGameModeComponent::computeTileCenter( x, y ) + float3{ 0.0f, 0.0f, -1.0f } );
                    }
                }
                for ( size_t jobIndex = 0; jobIndex < static_cast<size_t>( TileJob::Count ); ++jobIndex )
                {
                    if ( arrFound[jobIndex] == false )
                        blackboard.clearValue( hashed_string{ string_view{ kArrTileKey[jobIndex] } } );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FarmAutoFarmerAIComponent::FarmAutoFarmerAIComponent()
        : _cultivateLimit{ 32 }
        , _gameMode{}
    {
        requireBehaviorTree();
        // 트리가 읽고 고치는 칸의 처음 값이다(행동 간격 타이머 · 가게에서 이어서 살 개수).
        getBlackboard().setFloat( "ActionTimer", 0.0f );
        getBlackboard().setFloat( "BuyRemaining", 0.0f );
        getBlackboard().setFloat( "SeedCountAtBuy", 0.0f );
    }

    ActionResult FarmAutoFarmerAIComponent::taskWalkUpTo( ActionContext& context, const FarmWalkUpToParams& params )
    {
        AIControllerComponent* pController = context._world.find<AIControllerSlot>();
        const Blackboard*      pBlackboard = context._world.find<BlackboardSlot>();
        const optional<float3> spot        = pBlackboard != nullptr ? optional<float3>{ pBlackboard->getVector( params._key ) } : nullopt;
        const optional<float3> position    = pBlackboard != nullptr ? optional<float3>{ pBlackboard->getVector( "PlayerPosition" ) } : nullopt;
        const optional<bool>   facingUp    = pBlackboard != nullptr ? optional<bool>{ pBlackboard->getBool( "FacingUp" ) } : nullopt;
        const bool             bCannotWalk = context._bAborted == SW_TRUE || pController == nullptr || spot.has_value() == false || position.has_value() == false || facingUp.has_value() == false;
        if ( bCannotWalk )
            return ActionResult::Failed;
        const bool bOnSpot = float3::getHorizontalDistance( *position, *spot ) <= params._tolerance;
        if ( *facingUp && bOnSpot )
        {
            pController->stopMoving();
            return ActionResult::Succeeded;
        }
        // 칸 아래 통로(같은 X 줄, 선 자리보다 아래)에 있으면 위로 걸어 들어가고, 아니면(자리에 섰는데 다른 쪽을 보는 것도) 통로 아래로 먼저 간다.
        const bool bInLane = bOnSpot == false && MathUtil::abs( position->_x - spot->_x ) <= params._tolerance && position->_z <= spot->_z;
        if ( bInLane )
            pController->moveTo( *spot );
        else
            pController->moveTo( *spot - float3{ 0.0f, 0.0f, params._approachBack } );
        return ActionResult::Failed;
    }

    void FarmAutoFarmerAIComponent::updateBlackboard( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        using Internal                          = FarmAutoFarmerAIComponentInternal;
        Blackboard&                  blackboard = getBlackboard();
        const GameObject*            pPawnOwner = pawn.getOwner();
        const GameObjectManager*     pManager   = pPawnOwner != nullptr ? pPawnOwner->getManager() : nullptr;
        const FarmGameModeComponent* pGameMode  = pManager != nullptr ? GameModeComponent::resolve<FarmGameModeComponent>( *pManager, _gameMode ) : nullptr;
        const SessionStateComponent* pState     = pGameMode != nullptr ? SessionStateComponent::findOnOwner( *pGameMode ) : nullptr;
        const bool                   bReady     = pState != nullptr && pGameMode->isStarted();
        blackboard.setBool( "FarmReady", bReady );
        if ( bReady == false )
            return;
        const optional<float32> actionTimer = blackboard.getFloat( "ActionTimer" );
        SW_ASSERT( actionTimer.has_value() && "the constructor seeds ActionTimer and nothing clears it" );
        blackboard.setFloat( "ActionTimer", *actionTimer - context._deltaTime );

        const FarmGameModeComponent& gameMode = *pGameMode;
        const Inventory&             bag      = pState->getInventory();
        const CropCatalog&           catalog  = gameMode.getCropCatalog();
        const vector<hashed_string>& listSeed = gameMode.getSeeds();
        blackboard.setVector( "PlayerPosition", gameMode.getPlayerPosition() );
        blackboard.setBool( "FacingUp", gameMode.getFacing()._z > 0.5f );
        blackboard.setBool( "NearShop", gameMode.isNearShop() );
        blackboard.setInt( "Stamina", gameMode.getStamina() );
        blackboard.setBool( "Tired", gameMode.getStamina() < Internal::kTiredStamina || gameMode.getHourOfDay() >= Internal::kTiredHour || gameMode.getDayStarted() < pState->getClock().getDay() );
        blackboard.setInt( "ProduceCount", Internal::countProduce( catalog, bag ) );
        blackboard.setInt( "SelectedSeedCount", listSeed.empty() ? 0 : bag.getItemCount( listSeed[static_cast<size_t>( gameMode.getSelectedSeedIndex() )] ) );
        for ( size_t toolIndex = 0; toolIndex < std::size( Internal::kArrHoldingKey ); ++toolIndex )
        {
            blackboard.setBool( hashed_string{ string_view{ Internal::kArrHoldingKey[toolIndex] } }, static_cast<size_t>( gameMode.getTool() ) == toolIndex );
        }

        // 이번 계절 씨앗(가진 것 먼저) — 디렉터의 고른 씨앗이 다르면 트리가 다음 씨앗을 누른다(판단마다 한 칸).
        const int32    seasonalSeed  = Internal::findSeasonalSeed( catalog, bag, listSeed, pState->getClock().getSeasonName() );
        const CropDef* pSeasonalCrop = seasonalSeed >= 0 ? catalog.findCropBySeed( listSeed[static_cast<size_t>( seasonalSeed )] ) : nullptr;
        const bool     bHasSeed      = seasonalSeed >= 0 && bag.getItemCount( listSeed[static_cast<size_t>( seasonalSeed )] ) > 0;
        blackboard.setBool( "SeedSelected", seasonalSeed < 0 || gameMode.getSelectedSeedIndex() == seasonalSeed );
        blackboard.setBool( "HasSeed", bHasSeed );
        blackboard.setBool( "CanAffordSeed", pSeasonalCrop != nullptr && pState->getWallet().canAfford( gameMode.getCurrency(), pSeasonalCrop->_seedPrice ) );
        Internal::writeTileJob( gameMode.getField(), _cultivateLimit, seasonalSeed >= 0, bHasSeed, blackboard );
        blackboard.setVector( "ShipSpot", gameMode.getShippingBinPosition() + float3{ -1.0f, 0.0f, 0.0f } );
        blackboard.setVector( "ShopSpot", gameMode.getShopPosition() + float3{ 1.4f, 0.0f, 0.0f } );
    }

    // 자동 농부 트리 데이터(`data/ai/autofarmer.bt.xml`)가 `task` 속성으로 가리키는 이름이다.
    SW_GAME_ACTION( FarmWalkUpTo, "farm.walkUpTo", FarmWalkUpToParams, &FarmAutoFarmerAIComponent::taskWalkUpTo );
} // namespace sw
