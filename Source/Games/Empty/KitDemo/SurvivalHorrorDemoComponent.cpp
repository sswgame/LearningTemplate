#include "pch.h"

#include "Games/Empty/KitDemo/SurvivalHorrorDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorSession.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Gameplay/Inventory/GridInventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"

namespace sw
{
    namespace
    {
        struct SurvivalHorrorDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDSH" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr int32   kItemBoxSlots = 4;     ///< 아이템 상자 슬롯(시연은 쓰지 않지만 세션이 빌린다)
            static constexpr int32   kWitnessCount = 2;     ///< 같은 괴물을 보는 횟수(두 번째부터 배율이 걸린다)
            static constexpr float32 kDamage       = 50.0f; ///< 약초가 다 채우지 못하게 남기는 피해
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoSurvivalHorrorState, "KitDemo.SurvivalHorror.State", "Survival horror demo state (0 not started, 1 session run finished)", SurvivalHorrorDemoComponent,
                                &SurvivalHorrorDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoSurvivalHorrorSanity, "KitDemo.SurvivalHorror.Sanity", "Sanity after the monster sightings", SurvivalHorrorDemoComponent,
                                &SurvivalHorrorDemoComponent::getSanity );
    SW_AUTOMATION_GETTER_WATCH( kitDemoSurvivalHorrorHealth, "KitDemo.SurvivalHorror.Health", "Health after the damage and the healing item", SurvivalHorrorDemoComponent,
                                &SurvivalHorrorDemoComponent::getHealth );

    SurvivalHorrorDemoComponent::SurvivalHorrorDemoComponent()
        : _catalogPath{}
        , _pCatalog{}
        , _sanity{ 0 }
        , _health{ 0 }
    {
    }

    SurvivalHorrorDemoComponent::~SurvivalHorrorDemoComponent() = default;

    bool SurvivalHorrorDemoComponent::runSession()
    {
        // 방을 옮기지 않는 시연이라 지도 · 플래그는 빌려 주지 않는다.
        GridInventory         grid;
        Inventory             itemBox;
        SurvivalHorrorSession session;
        if ( itemBox.initialize( nullptr, SurvivalHorrorDemoComponentInternal::kItemBoxSlots ) == false )
            return false;
        if ( session.initialize( _pCatalog.get(), nullptr, hashed_string{}, SessionStateRefs{}, grid, itemBox ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.SurvivalHorrorOpenFailed", "Survival horror demo: the session refused the catalog rules - the session does not open" );
            return false;
        }
        for ( int32 index = 0; index < SurvivalHorrorDemoComponentInternal::kWitnessCount; ++index )
        {
            (void)session.witnessMonster( "zombie" );
        }
        session.applyDamage( SurvivalHorrorDemoComponentInternal::kDamage );
        if ( grid.addItem( "herb", 1 ) != 1 || grid.getItems().empty() )
        {
            SW_LOG_ERROR_ID( "KitDemo.SurvivalHorrorNoHerb", "Survival horror demo: the grid bag did not take item 'herb'" );
            return false;
        }
        if ( session.tryUseItem( grid.getItems().front()._instanceID ) == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.SurvivalHorrorHerbRefused", "Survival horror demo: the session refused to use item 'herb'" );
            return false;
        }
        _sanity = static_cast<int32>( session.getSanity().getValue() );
        _health = static_cast<int32>( session.getHealth() );
        markRunFinished();
        return true;
    }

    bool SurvivalHorrorDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        return runSession();
    }

    void SurvivalHorrorDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.SurvivalHorrorDone", "[KitDemo] survival horror: sanity %#, health %#", _sanity, _health );
    }

    void SurvivalHorrorDemoComponent::serializeGameState( StateIO& io )
    {
        serializeResults( io, SurvivalHorrorDemoComponentInternal::kStateTag, SurvivalHorrorDemoComponentInternal::kStateVersion );
    }
} // namespace sw
