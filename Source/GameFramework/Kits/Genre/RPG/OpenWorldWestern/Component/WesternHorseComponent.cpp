#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternHorseComponent.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Scenario/IAutomationStepContext.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternGameModeComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternHorseMountComponent.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"

namespace sw
{
    namespace
    {
        /** @brief 말 상태(스태미나 · 유대)를 모든 게임의 상태 스냅샷에 싣습니다(게임마다 올리지 않는다). */
        const StatefulComponentRegistrar<WesternHorseComponent> s_horseStateRegistration{ "WesternHorseComponent" };

        /** @brief 말 사건을 한 줄 로그로 남깁니다(유대 단계 · 능력 · 지침 · 겁). */
        void logHorseEvent( const utf8* pHorseName, const WesternHorseEvent& horseEvent )
        {
            switch ( horseEvent._kind )
            {
                case WesternHorseEvent::Kind::BondLevelUp:
                {
                    SW_LOG_INFO_ID( "WesternHorse.BondLevel", "[Western] %# bond level %#", pHorseName, horseEvent._value );
                    break;
                }
                case WesternHorseEvent::Kind::AbilityUnlocked:
                {
                    SW_LOG_INFO_ID( "WesternHorse.Learned", "[Western] %# learned %#", pHorseName, horseEvent._ability.c_str() );
                    break;
                }
                case WesternHorseEvent::Kind::Exhausted:
                {
                    SW_LOG_INFO_ID( "WesternHorse.OutBreathNoGallop", "[Western] %# is out of breath - no gallop until it recovers", pHorseName );
                    break;
                }
                case WesternHorseEvent::Kind::Spooked:
                {
                    SW_LOG_INFO_ID( "WesternHorse.RearsUp", "[Western] %# rears up", pHorseName );
                    break;
                }
                case WesternHorseEvent::Kind::ThrewRider:
                {
                    SW_LOG_INFO_ID( "WesternHorse.ThrewRider", "[Western] %# threw its rider", pHorseName );
                    break;
                }
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    WesternHorseComponent::WesternHorseComponent()
        : _gameMode{}
        , _breed{}
        , _seed{ 1 }
        , _gameHoursPerSecond{ 0.0f }
        , _horse{}
        , _pendingStateBytes{}
        , _bOpened{ SW_FALSE }
        , _bFailed{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    WesternHorseComponent::~WesternHorseComponent() = default;

    void WesternHorseComponent::onEndPlay()
    {
        GameObject*                 pOwner = getOwner();
        WesternHorseMountComponent* pMount = pOwner != nullptr ? pOwner->getComponent<WesternHorseMountComponent>() : nullptr;
        if ( pMount != nullptr && pMount->findHorse() == &_horse )
            pMount->bindHorse( nullptr );
        _bOpened = SW_FALSE;
        Component::onEndPlay();
    }

    void WesternHorseComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _bFailed == SW_TRUE || hasBegunPlay() == false ) // 편집 중인 에디터도 틱을 부른다 — 디렉터가 열리기 전이라 카탈로그가 없다
            return;
        if ( _bOpened == SW_FALSE )
        {
            if ( openHorse() == false )
            {
                _bFailed = SW_TRUE;
                return;
            }
            applyPendingState();
        }
        _horse.update( context.getDeltaTime(), context.getDeltaTime().count() * _gameHoursPerSecond );
        vector<WesternHorseEvent> listEvent;
        _horse.pollEvents( listEvent );
        const GameObject* pOwner = getOwner();
        for ( const WesternHorseEvent& horseEvent : listEvent )
        {
            logHorseEvent( pOwner != nullptr ? pOwner->getName().c_str() : "?", horseEvent );
        }
    }

    bool WesternHorseComponent::openHorse()
    {
        GameObject*                     pOwner    = getOwner();
        const GameObjectManager*        pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const WesternGameModeComponent* pGameMode = pManager != nullptr ? GameModeComponent::resolve<WesternGameModeComponent>( *pManager, _gameMode ) : nullptr;
        const WesternCatalog*           pCatalog  = pGameMode != nullptr ? pGameMode->findWesternCatalog() : nullptr;
        WesternHorseMountComponent*     pMount    = pOwner != nullptr ? pOwner->getComponent<WesternHorseMountComponent>() : nullptr;
        const utf8*                     pName     = pOwner != nullptr ? pOwner->getName().c_str() : "?";
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "WesternHorse.HorseNoWesternCatalog", "[Western] horse '%#' has no western catalog - %#", pName, pGameMode == nullptr ? "Game Mode does not point at a western game mode" : "the game mode did not start" );
            return false;
        }
        if ( pMount == nullptr )
        {
            SW_LOG_ERROR_ID( "WesternHorse.HorseNoWesternHorse", "[Western] horse '%#' has no Western Horse Mount on its object", pName );
            return false;
        }
        if ( _horse.initialize( *pCatalog, _breed.key(), static_cast<uint32>( _seed ) ) == false )
        {
            SW_LOG_ERROR_ID( "WesternHorse.HorseBreedNotWestern", "[Western] horse '%#' breed '%#' is not in the western catalog", pName, _breed.c_str() );
            return false;
        }
        pMount->bindHorse( &_horse );
        _bOpened = SW_TRUE;
        return true;
    }

    void WesternHorseComponent::serializeState( StateIO& io )
    {
        io.header( kStateFormat );
        uint8 bOpened = _bOpened;
        io.value( bOpened );
        if ( bOpened == SW_TRUE )
        {
            io.valueAtomic( _horse );
        }
        io.end();
    }

    void WesternHorseComponent::restoreState( vector<uint8>&& bytes )
    {
        _pendingStateBytes = std::move( bytes );
        if ( _bOpened == SW_TRUE )
            applyPendingState();
    }

    void WesternHorseComponent::applyPendingState()
    {
        if ( _pendingStateBytes.empty() )
            return;
        Archive    archive( _pendingStateBytes.data(), _pendingStateBytes.size() );
        const bool bRead = readState( archive );
        _pendingStateBytes.clear();
        if ( bRead == false )
        {
            const GameObject* pOwner = getOwner();
            SW_LOG_WARNING_ID( "WesternHorse.HorseStateCouldNot", "[Western] horse '%#' state could not be restored - it starts fresh", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
        }
    }
} // namespace sw
