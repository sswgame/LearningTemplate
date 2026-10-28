#include "pch.h"

#include "Games/HarvestValley/FarmAutoFarmerAIComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Base/World/Environment/WorldClock.h"

#include "Games/HarvestValley/FarmDirectorComponent.h"

namespace sw
{
    namespace
    {
        struct FarmAutoFarmerAIComponentInternal
        {
            /** @brief 할 일 하나입니다. */
            enum class TaskKind : uint8
            {
                None = 0, ///< 잔다
                Tile,     ///< 칸에 도구
                Ship,     ///< 출하
                Shop,     ///< 씨앗 사기
            };

            struct Task
            {
                float3   _standPosition{};
                FarmTool _tool{ FarmTool::Hand };
                TaskKind _kind{ TaskKind::None };
            };

            static FarmButton findToolButton( FarmTool tool )
            {
                switch ( tool )
                {
                    case FarmTool::Hoe:
                        return FarmButton::Tool1;
                    case FarmTool::WateringCan:
                        return FarmButton::Tool2;
                    case FarmTool::Seeds:
                        return FarmButton::Tool3;
                    case FarmTool::Hand:
                        return FarmButton::Tool4;
                }
                return FarmButton::Tool4;
            }

            /**
             * @brief 할 일의 상태 이름입니다(감시 값 `AI.State` 가 읽는다). 타일 일은 도구로 가른다 — `Till` · `Water` · `Sow` · `Harvest`.
             * @details 이름은 시나리오의 데이터다(`<ExpectVisited probe="AI.State" sequence="Till,Sow,Water"/>`). 바꾸면 시나리오도 함께 고친다.
             */
            static const utf8* getStateName( const Task& task )
            {
                switch ( task._kind )
                {
                    case TaskKind::None:
                        return "Sleep";
                    case TaskKind::Ship:
                        return "Ship";
                    case TaskKind::Shop:
                        return "Shop";
                    case TaskKind::Tile:
                        break;
                }
                switch ( task._tool )
                {
                    case FarmTool::Hoe:
                        return "Till";
                    case FarmTool::WateringCan:
                        return "Water";
                    case FarmTool::Seeds:
                        return "Sow";
                    case FarmTool::Hand:
                        return "Harvest";
                }
                return "Harvest";
            }

            /** @brief 이번 계절에 자라는 씨앗의 순번입니다(가진 것 먼저). 없으면 -1 입니다. */
            static int32 findSeasonalSeed( const CropCatalog& catalog, const Inventory& bag, const vector<hashed_string>& listSeed, const hashed_string& season )
            {
                int32 seasonalSeed = -1;
                for ( int32 seedIndex = 0; seedIndex < static_cast<int32>( listSeed.size() ); ++seedIndex )
                {
                    const CropDef* pCrop = catalog.findCropBySeed( listSeed[static_cast<size_t>( seedIndex )] );
                    if ( pCrop == nullptr || pCrop->growsIn( season ) == false )
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

            /** @brief 출하 할 일입니다(출하함 옆자리). */
            static Task makeShipTask( const FarmDirectorComponent& director )
            {
                Task task;
                task._kind          = TaskKind::Ship;
                task._standPosition = director.getShippingBinPosition() + float3{ -1.0f, 0.0f, 0.0f };
                return task;
            }

            /**
             * @brief 칸 순서로 훑어 첫 할 일(수확 5 > 물 4 > 심기 3 > 갈기 2)을 @p inoutTask 에 담고 그 우선순위를 돌려줍니다. 우선순위가 같은 칸이면 앞 칸입니다. 할 일이 없으면 0 입니다.
             * @param cultivateLimit 갈아 둘 칸의 상한입니다.
             */
            static int32 chooseTileTask( const FarmField& field, int32 cultivateLimit, int32 seasonalSeed, bool bHasSeed, Task& inoutTask )
            {
                int32 cultivatedCount = 0;
                for ( int32 y = 0; y < FarmDirectorComponent::kFieldHeight; ++y )
                {
                    for ( int32 x = 0; x < FarmDirectorComponent::kFieldWidth; ++x )
                    {
                        cultivatedCount += field.findTile( x, y )->_bTilled != SW_FALSE ? 1 : 0;
                    }
                }
                int32 bestPriority = 0;
                for ( int32 y = 0; y < FarmDirectorComponent::kFieldHeight; ++y )
                {
                    for ( int32 x = 0; x < FarmDirectorComponent::kFieldWidth; ++x )
                    {
                        const FarmTile* pTile         = field.findTile( x, y );
                        int32           priority      = 0;
                        FarmTool        tool          = FarmTool::Hand;
                        const bool      bCanSow       = pTile->_bTilled != SW_FALSE && pTile->hasCrop() == false && bHasSeed;
                        const bool      bCanCultivate = pTile->_bTilled == SW_FALSE && cultivatedCount < cultivateLimit && seasonalSeed >= 0;
                        if ( pTile->_bReady != SW_FALSE || pTile->_bWithered != SW_FALSE )
                        {
                            priority = 5;
                            tool     = FarmTool::Hand;
                        }
                        else if ( pTile->hasCrop() && pTile->_bWatered == SW_FALSE )
                        {
                            priority = 4;
                            tool     = FarmTool::WateringCan;
                        }
                        else if ( bCanSow )
                        {
                            priority = 3;
                            tool     = FarmTool::Seeds;
                        }
                        else if ( bCanCultivate )
                        {
                            priority = 2;
                            tool     = FarmTool::Hoe;
                        }
                        if ( priority > bestPriority )
                        {
                            bestPriority             = priority;
                            inoutTask._kind          = TaskKind::Tile;
                            inoutTask._tool          = tool;
                            inoutTask._standPosition = FarmDirectorComponent::computeTileCenter( x, y ) + float3{ 0.0f, 0.0f, -1.0f };
                        }
                    }
                }
                return bestPriority;
            }

            /**
             * @brief 지금 할 일을 고릅니다 — 늦었거나 지쳤으면 출하하고 잔다, 거둘 것 → 물 → 심기 → 갈기 → 씨앗 사기. 할 일이 없으면 종류가 `None` 입니다.
             * @param seasonalSeed 이번 계절 씨앗의 번호입니다(없으면 음수).
             */
            static Task chooseTask( const FarmDirectorComponent& director, const SessionStateComponent& state, const vector<hashed_string>& listSeed, int32 seasonalSeed, bool bHasSeed,
                                    int32 cultivateLimit )
            {
                const Inventory&   bag          = state.getInventory();
                const CropCatalog& catalog      = director.getCropCatalog();
                const bool         bTired       = director.getStamina() < 8 || director.getHourOfDay() >= 22.0f || director.getDayStarted() < state.getClock().getDay();
                const int32        produceCount = countProduce( catalog, bag );
                Task               task;
                if ( produceCount > 0 && ( bTired || produceCount >= 6 ) )
                {
                    task = makeShipTask( director );
                }
                else if ( bTired == false )
                {
                    const int32 bestPriority = chooseTileTask( director.getField(), cultivateLimit, seasonalSeed, bHasSeed, task );
                    // 심을 칸이 있는데 씨앗이 없으면 산다.
                    const bool bNeedsSeedPurchase = bestPriority < 3 && bHasSeed == false && seasonalSeed >= 0;
                    if ( bNeedsSeedPurchase )
                    {
                        const CropDef* pCrop = catalog.findCropBySeed( listSeed[static_cast<size_t>( seasonalSeed )] );
                        if ( pCrop != nullptr && state.getWallet().canAfford( director.getCurrency(), pCrop->_seedPrice ) )
                        {
                            task._kind          = TaskKind::Shop;
                            task._standPosition = director.getShopPosition() + float3{ 1.4f, 0.0f, 0.0f };
                        }
                    }
                }
                // 밭에 할 일이 없으면 남은 수확물을 넣고, 그것도 없으면 잔다.
                if ( task._kind == TaskKind::None && produceCount > 0 )
                    task = makeShipTask( director );
                return task;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    FarmAutoFarmerAIComponent::FarmAutoFarmerAIComponent()
        : _actionInterval{ 0.25f }
        , _cultivateLimit{ 32 }
        , _buyBatch{ 6 }
        , _standTolerance{ 0.2f }
        , _approachBack{ 0.4f }
        , _director{}
        , _actionTimer{ 0.0f }
        , _buyRemaining{ 0 }
        , _seedCountAtBuy{ 0 }
    {
    }

    bool FarmAutoFarmerAIComponent::walkTo( const FarmDirectorComponent& director, const float3& standPosition )
    {
        if ( float3::getHorizontalDistance( director.getPlayerPosition(), standPosition ) <= _standTolerance )
        {
            stopMoving();
            return true;
        }
        moveTo( standPosition );
        return false;
    }

    bool FarmAutoFarmerAIComponent::walkUpTo( const FarmDirectorComponent& director, const float3& standPosition )
    {
        const float3& position  = director.getPlayerPosition();
        const bool    bFacingUp = director.getFacing()._z > 0.5f;
        const bool    bOnSpot   = float3::getHorizontalDistance( position, standPosition ) <= _standTolerance;
        if ( bFacingUp && bOnSpot )
        {
            stopMoving();
            return true;
        }
        // 칸 아래 통로(같은 X 줄, 선 자리보다 아래)에 있으면 위로 걸어 들어가고, 아니면(자리에 섰는데 다른 쪽을 보는 것도) 통로 아래로 먼저 간다.
        const bool bInLane = bOnSpot == false && MathUtil::abs( position._x - standPosition._x ) <= _standTolerance && position._z <= standPosition._z;
        if ( bInLane )
            moveTo( standPosition );
        else
            moveTo( standPosition - float3{ 0.0f, 0.0f, _approachBack } );
        return false;
    }

    void FarmAutoFarmerAIComponent::think( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        using Internal                          = FarmAutoFarmerAIComponentInternal;
        const GameObject*            pPawnOwner = pawn.getOwner();
        const GameObjectManager*     pManager   = pPawnOwner != nullptr ? pPawnOwner->getManager() : nullptr;
        const FarmDirectorComponent* pDirector  = pManager != nullptr ? GameDirectorComponent::resolve<FarmDirectorComponent>( *pManager, _director ) : nullptr;
        const SessionStateComponent* pState     = pDirector != nullptr ? SessionStateComponent::findOnOwner( *pDirector ) : nullptr;
        if ( pState == nullptr || pDirector->isStarted() == false )
        {
            stopMoving();
            return;
        }
        _actionTimer -= context._deltaTime;
        const FarmDirectorComponent& director = *pDirector;
        const Inventory&             bag      = pState->getInventory();
        const CropCatalog&           catalog  = director.getCropCatalog();
        const hashed_string          season   = pState->getClock().getSeasonName();
        const vector<hashed_string>& listSeed = director.getSeeds();

        // 가게 앞에서 사는 중이면 다 살 때까지 선다(한 번에 하나씩 산다 — 틱마다 한 번 누른다). 지난 누름이 못 샀으면(돈 · 가방) 그만 산다.
        const int32 selectedSeedCount = listSeed.empty() ? 0 : bag.getItemCount( listSeed[static_cast<size_t>( director.getSelectedSeedIndex() )] );
        const bool  bLastBuyFailed    = _buyRemaining < _buyBatch && selectedSeedCount <= _seedCountAtBuy;
        const bool  bShouldBuySeeds   = _buyRemaining > 0 && bLastBuyFailed == false && director.isNearShop();
        if ( bShouldBuySeeds )
        {
            setStateName( "Buy" );
            stopMoving();
            pressButton( hashed_string( FarmDirectorComponent::getButtonName( FarmButton::Buy ) ) );
            _seedCountAtBuy = selectedSeedCount;
            --_buyRemaining;
            return;
        }
        _buyRemaining = 0;

        // 이번 계절 씨앗(가진 것 먼저) — 디렉터의 고른 씨앗이 다르면 다음 씨앗을 누른다(틱마다 한 칸).
        const int32 seasonalSeed  = Internal::findSeasonalSeed( catalog, bag, listSeed, season );
        const bool  bSeedSelected = seasonalSeed < 0 || director.getSelectedSeedIndex() == seasonalSeed;
        if ( bSeedSelected == false )
            pressButton( hashed_string( FarmDirectorComponent::getButtonName( FarmButton::SeedNext ) ) );
        const bool bHasSeed = seasonalSeed >= 0 && bag.getItemCount( listSeed[static_cast<size_t>( seasonalSeed )] ) > 0;
        // 할 일 — 늦었거나 지쳤으면 출하하고 잔다, 거둘 것 → 물 → 심기 → 갈기 → 씨앗 사기.
        const Internal::Task task = Internal::chooseTask( director, *pState, listSeed, seasonalSeed, bHasSeed, _cultivateLimit );
        setStateName( hashed_string( Internal::getStateName( task ) ) );
        getBlackboard().setInt( "Stamina", director.getStamina() );
        if ( task._kind == Internal::TaskKind::None )
        {
            getBlackboard().clearValue( "StandPosition" );
            stopMoving();
            pressButton( hashed_string( FarmDirectorComponent::getButtonName( FarmButton::Sleep ) ) );
            return;
        }

        getBlackboard().setVector( "StandPosition", task._standPosition );
        const bool bStanding = task._kind == Internal::TaskKind::Tile ? walkUpTo( director, task._standPosition ) : walkTo( director, task._standPosition );
        if ( bStanding == false || _actionTimer > 0.0f )
            return;
        switch ( task._kind )
        {
            case Internal::TaskKind::Ship:
            {
                _actionTimer = _actionInterval;
                pressButton( hashed_string( FarmDirectorComponent::getButtonName( FarmButton::Ship ) ) );
                break;
            }
            case Internal::TaskKind::Shop:
            {
                // 고른 씨앗이 맞을 때 산다 — 이번 틱부터 `_buyBatch` 개.
                if ( bSeedSelected )
                    _buyRemaining = _buyBatch;
                break;
            }
            case Internal::TaskKind::Tile:
            {
                if ( task._tool == FarmTool::Seeds && bSeedSelected == false )
                    break;
                _actionTimer = _actionInterval;
                // 도구 버튼과 쓰기를 같은 틱에 — 디렉터는 도구를 먼저 바꾸고 쓴다.
                if ( director.getTool() != task._tool )
                    pressButton( hashed_string( FarmDirectorComponent::getButtonName( Internal::findToolButton( task._tool ) ) ) );
                pressButton( hashed_string( FarmDirectorComponent::getButtonName( FarmButton::Use ) ) );
                break;
            }
            case Internal::TaskKind::None:
            {
                break;
            }
        }
    }
} // namespace sw
