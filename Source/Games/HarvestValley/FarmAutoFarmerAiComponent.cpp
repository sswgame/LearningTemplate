#include "pch.h"

#include "Games/HarvestValley/FarmAutoFarmerAiComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Control/PawnComponent.h"
#include "GameFramework/Base/GameState/GameStateComponent.h"
#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/World/WorldClock.h"

#include "Games/HarvestValley/FarmDirectorComponent.h"

namespace sw
{
    namespace
    {
        struct FarmAutoFarmerAiComponentInternal
        {
            /** @brief 칸에 도구를 쓸 때 칸 아래 자리에서 더 아래로 물러서는 거리 — 여기서 위로 걸어 들어가 위를 보고 선다. */
            static constexpr float32 kApproachBack = 0.4f;

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

            static float32 computeDistanceXz( const float3& lhs, const float3& rhs )
            {
                const float32 dx = lhs._x - rhs._x;
                const float32 dz = lhs._z - rhs._z;
                return MathUtil::sqrt( dx * dx + dz * dz );
            }

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
        };
    } // namespace
} // namespace sw

namespace sw
{
    FarmAutoFarmerAiComponent::FarmAutoFarmerAiComponent()
        : _actionInterval{ 0.25f }
        , _cultivateLimit{ 32 }
        , _buyBatch{ 6 }
        , _standTolerance{ 0.2f }
        , _director{}
        , _actionTimer{ 0.0f }
        , _buyRemaining{ 0 }
        , _seedCountAtBuy{ 0 }
    {
    }

    bool FarmAutoFarmerAiComponent::walkTo( const FarmDirectorComponent& director, const float3& standPosition )
    {
        if ( FarmAutoFarmerAiComponentInternal::computeDistanceXz( director.getPlayerPosition(), standPosition ) <= _standTolerance )
        {
            stopMoving();
            return true;
        }
        moveTo( standPosition );
        return false;
    }

    bool FarmAutoFarmerAiComponent::walkUpTo( const FarmDirectorComponent& director, const float3& standPosition )
    {
        using Internal          = FarmAutoFarmerAiComponentInternal;
        const float3& position  = director.getPlayerPosition();
        const bool    bFacingUp = director.getFacing()._z > 0.5f;
        const bool    bOnSpot   = Internal::computeDistanceXz( position, standPosition ) <= _standTolerance;
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
            moveTo( standPosition - float3{ 0.0f, 0.0f, Internal::kApproachBack } );
        return false;
    }

    void FarmAutoFarmerAiComponent::think( const ControlFrameContext& context, const PawnComponent& pawn )
    {
        using Internal                          = FarmAutoFarmerAiComponentInternal;
        const GameObject*            pPawnOwner = pawn.getOwner();
        const GameObjectManager*     pManager   = pPawnOwner != nullptr ? pPawnOwner->getManager() : nullptr;
        const FarmDirectorComponent* pDirector  = pManager != nullptr ? GameDirectorComponent::resolve<FarmDirectorComponent>( *pManager, _director ) : nullptr;
        const GameStateComponent*    pState     = pDirector != nullptr ? GameStateComponent::findOnOwner( *pDirector ) : nullptr;
        if ( pState == nullptr || pDirector->isStarted() == false )
        {
            stopMoving();
            return;
        }
        _actionTimer -= context._deltaTime;
        const FarmDirectorComponent& director = *pDirector;
        const Inventory&             bag      = pState->getInventory();
        const CropCatalog&           catalog  = director.getCropCatalog();
        const FarmField&             field    = director.getField();
        const hashed_string          season   = pState->getClock().getSeasonName();
        const vector<hashed_string>& listSeed = director.getSeeds();

        // 가게 앞에서 사는 중이면 다 살 때까지 선다(한 번에 하나씩 산다 — 틱마다 한 번 누른다). 지난 누름이 못 샀으면(돈 · 가방) 그만 산다.
        const int32 selectedSeedCount = listSeed.empty() ? 0 : bag.getItemCount( listSeed[static_cast<size_t>( director.getSelectedSeedIndex() )] );
        const bool  bLastBuyFailed    = _buyRemaining < _buyBatch && selectedSeedCount <= _seedCountAtBuy;
        if ( _buyRemaining > 0 && bLastBuyFailed == false && director.isNearShop() )
        {
            stopMoving();
            pressButton( hashed_string( FarmDirectorComponent::getButtonName( FarmButton::Buy ) ) );
            _seedCountAtBuy = selectedSeedCount;
            --_buyRemaining;
            return;
        }
        _buyRemaining = 0;

        // 이번 계절 씨앗(가진 것 먼저) — 디렉터의 고른 씨앗이 다르면 다음 씨앗을 누른다(틱마다 한 칸).
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
        const bool bSeedSelected = seasonalSeed < 0 || director.getSelectedSeedIndex() == seasonalSeed;
        if ( bSeedSelected == false )
            pressButton( hashed_string( FarmDirectorComponent::getButtonName( FarmButton::SeedNext ) ) );
        const bool bHasSeed = seasonalSeed >= 0 && bag.getItemCount( listSeed[static_cast<size_t>( seasonalSeed )] ) > 0;

        // 할 일 — 늦었거나 지쳤으면 출하하고 잔다, 거둘 것 → 물 → 심기 → 갈기 → 씨앗 사기.
        const bool bTired       = director.getStamina() < 8 || director.getHourOfDay() >= 22.0f || director.getDayStarted() < pState->getClock().getDay();
        int32      produceCount = 0;
        for ( const CropDef& crop : catalog.getCrops() )
        {
            produceCount += bag.getItemCount( crop._produceItem );
        }
        Internal::Task task;
        if ( produceCount > 0 && ( bTired || produceCount >= 6 ) )
        {
            task._kind          = Internal::TaskKind::Ship;
            task._standPosition = director.getShippingBinPosition() + float3{ -1.0f, 0.0f, 0.0f };
        }
        else if ( bTired == false )
        {
            int32 cultivatedCount = 0;
            for ( int32 y = 0; y < FarmDirectorComponent::kFieldHeight; ++y )
            {
                for ( int32 x = 0; x < FarmDirectorComponent::kFieldWidth; ++x )
                {
                    cultivatedCount += field.findTile( x, y )->_bTilled != SW_FALSE ? 1 : 0;
                }
            }
            // 칸 순서로 훑어 첫 할 일. 우선순위가 같은 칸이면 앞 칸.
            int32 bestPriority = 0;
            for ( int32 y = 0; y < FarmDirectorComponent::kFieldHeight; ++y )
            {
                for ( int32 x = 0; x < FarmDirectorComponent::kFieldWidth; ++x )
                {
                    const FarmTile* pTile    = field.findTile( x, y );
                    int32           priority = 0;
                    FarmTool        tool     = FarmTool::Hand;
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
                    else if ( pTile->_bTilled != SW_FALSE && pTile->hasCrop() == false && bHasSeed )
                    {
                        priority = 3;
                        tool     = FarmTool::Seeds;
                    }
                    else if ( pTile->_bTilled == SW_FALSE && cultivatedCount < _cultivateLimit && seasonalSeed >= 0 )
                    {
                        priority = 2;
                        tool     = FarmTool::Hoe;
                    }
                    if ( priority > bestPriority )
                    {
                        bestPriority        = priority;
                        task._kind          = Internal::TaskKind::Tile;
                        task._tool          = tool;
                        task._standPosition = FarmDirectorComponent::computeTileCenter( x, y ) + float3{ 0.0f, 0.0f, -1.0f };
                    }
                }
            }
            // 심을 칸이 있는데 씨앗이 없으면 산다.
            if ( bestPriority < 3 && bHasSeed == false && seasonalSeed >= 0 )
            {
                const CropDef* pCrop = catalog.findCropBySeed( listSeed[static_cast<size_t>( seasonalSeed )] );
                if ( pCrop != nullptr && pState->getWallet().canAfford( director.getCurrency(), pCrop->_seedPrice ) )
                {
                    task._kind          = Internal::TaskKind::Shop;
                    task._standPosition = director.getShopPosition() + float3{ 1.4f, 0.0f, 0.0f };
                }
            }
        }
        // 밭에 할 일이 없으면 남은 수확물을 넣고, 그것도 없으면 잔다.
        if ( task._kind == Internal::TaskKind::None && produceCount > 0 )
        {
            task._kind          = Internal::TaskKind::Ship;
            task._standPosition = director.getShippingBinPosition() + float3{ -1.0f, 0.0f, 0.0f };
        }
        if ( task._kind == Internal::TaskKind::None )
        {
            stopMoving();
            pressButton( hashed_string( FarmDirectorComponent::getButtonName( FarmButton::Sleep ) ) );
            return;
        }

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
