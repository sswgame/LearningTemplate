#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternHorseComponent.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationStepContext.h"
#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternGameModeComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternHorseMountComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 말 사건을 한 줄 로그로 남깁니다(유대 단계 · 능력 · 지침 · 겁). */
        void logHorseEvent( const utf8* pHorseName, const WesternHorseEvent& horseEvent )
        {
            switch ( horseEvent._kind )
            {
                case WesternHorseEvent::Kind::BondLevelUp:
                {
                    SW_LOG_INFO_ID( "WesternHorse.BondLevel", "[WildWest] %# bond level %#", pHorseName, horseEvent._value );
                    break;
                }
                case WesternHorseEvent::Kind::AbilityUnlocked:
                {
                    SW_LOG_INFO_ID( "WesternHorse.Learned", "[WildWest] %# learned %#", pHorseName, horseEvent._ability.c_str() );
                    break;
                }
                case WesternHorseEvent::Kind::Exhausted:
                {
                    SW_LOG_INFO_ID( "WesternHorse.OutBreathNoGallop", "[WildWest] %# is out of breath - no gallop until it recovers", pHorseName );
                    break;
                }
                case WesternHorseEvent::Kind::Spooked:
                {
                    SW_LOG_INFO_ID( "WesternHorse.RearsUp", "[WildWest] %# rears up", pHorseName );
                    break;
                }
                case WesternHorseEvent::Kind::ThrewRider:
                {
                    SW_LOG_INFO_ID( "WesternHorse.ThrewRider", "[WildWest] %# threw its rider", pHorseName );
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

    void WesternHorseComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 탈것 컴포넌트(PostPhysics)가 이번 프레임 질주로 스태미나를 쓴 뒤에 회복 · 코어 감소를 흘린다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void WesternHorseComponent::onEndPlay()
    {
        GameObject*                 pOwner = getOwner();
        WesternHorseMountComponent* pMount = pOwner != nullptr ? pOwner->getComponent<WesternHorseMountComponent>() : nullptr;
        if ( pMount != nullptr && pMount->findHorse() == &_horse )
            pMount->bindHorse( nullptr );
        _bOpened = SW_FALSE;
        Component::onEndPlay();
    }

    void WesternHorseComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bFailed == SW_TRUE )
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
        _horse.update( deltaTime, deltaTime * _gameHoursPerSecond );
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
        if ( _horse.initialize( pCatalog, _breed, static_cast<uint32>( _seed ) ) == false )
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
        io.header( kStateTag, kStateVersion );
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

namespace sw
{
    namespace
    {
        /** @brief 감시 값 `Western.Stamina` 입니다. 대상 말의 스태미나 비율(0..1)이고, 말을 열지 않았으면 값이 없습니다. */
        [[nodiscard]] bool readStamina( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
        {
            const WesternHorseComponent* pHorseComponent = query.findTargetObject() != nullptr ? query.findTargetObject()->getComponent<WesternHorseComponent>() : nullptr;
            const WesternHorse*          pHorse          = pHorseComponent != nullptr ? pHorseComponent->findHorse() : nullptr;
            if ( pHorse == nullptr )
            {
                outValue.setText( query.findTargetObject() == nullptr ? query.getTargetProblem() : query.describeMissing( "opened Western Horse" ) );
                return false;
            }
            outValue.setNumber( static_cast<float64>( pHorse->getStamina().getRatio() ) );
            return true;
        }
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( westernStamina, "Western.Stamina", "Stamina ratio (0..1) of the western horse on target", &readStamina, true, false );
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief 단계 `<FeedHorse horse="Bessie" food="oats"/>` 입니다 — 대상 말(`AutomationTarget` 규칙)에게 카탈로그의 먹이를 준다. 키트가 제 단계를 등록하는 본보기다. */
        struct WesternHorseStepsInternal
        {
            static expected<void> validateFeedHorse( const AutomationStep& step ) { return step.hasRequiredAttributes( { "horse", "food" }, { "horse", "food" } ); }

            static bool runFeedHorse( AutomationStepContext& context, const AutomationStep& step )
            {
                const string&               horseName = *step.findAttribute( "horse" );
                const string&               foodID    = *step.findAttribute( "food" );
                const expected<GameObject*> found     = AutomationTarget::find( context.findActiveObjectManager(), horseName );
                WesternHorseComponent*      pHorse    = found.has_value() ? ( *found )->getComponent<WesternHorseComponent>() : nullptr;
                if ( found.has_value() == false )
                    context.recordFailure( step, AutomationTarget::describe( found.error() ) );
                else if ( pHorse == nullptr )
                    context.recordFailure( step, "object '" + horseName + "' has no Western Horse" );
                else if ( pHorse->feed( hashed_string{ foodID.c_str() } ) == false )
                    context.recordFailure( step, "'" + horseName + "' did not take '" + foodID + "' (horse not opened or food not in the western catalog)" );
                return true;
            }
        };
    } // namespace

    // 키트 단계도 엔진 단계와 같은 레지스트리에 제 .cpp 가 올린다(언리얼 Functional Test 의 게임 쪽 액션과 같은 자리). 씬 틱 뒤에 돈다 — 다음 틱의 탈것이 회복한 스태미나를 본다.
    SW_AUTOMATION_STEP( westernFeedHorse, "FeedHorse", &WesternHorseStepsInternal::runFeedHorse, &WesternHorseStepsInternal::validateFeedHorse, AutomationStepOptions{} );
} // namespace sw
