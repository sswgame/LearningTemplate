#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternHorseComponent.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternDirectorComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternHorseMountComponent.h"
#include "GameFramework/Runtime/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    SW_LOG_CALLER( "WesternHorse" );

    namespace
    {
        /** @brief 말 사건을 한 줄 로그로 남깁니다(유대 단계 · 능력 · 지침 · 겁). */
        void logHorseEvent( const utf8* pHorseName, const WesternHorseEvent& horseEvent )
        {
            switch ( horseEvent._kind )
            {
                case WesternHorseEvent::Kind::BondLevelUp:
                {
                    SW_LOG_INFO( "[WildWest] %# bond level %#", pHorseName, horseEvent._value );
                    break;
                }
                case WesternHorseEvent::Kind::AbilityUnlocked:
                {
                    SW_LOG_INFO( "[WildWest] %# learned %#", pHorseName, horseEvent._ability.c_str() );
                    break;
                }
                case WesternHorseEvent::Kind::Exhausted:
                {
                    SW_LOG_INFO( "[WildWest] %# is out of breath - no gallop until it recovers", pHorseName );
                    break;
                }
                case WesternHorseEvent::Kind::Spooked:
                {
                    SW_LOG_INFO( "[WildWest] %# rears up", pHorseName );
                    break;
                }
                case WesternHorseEvent::Kind::ThrewRider:
                {
                    SW_LOG_INFO( "[WildWest] %# threw its rider", pHorseName );
                    break;
                }
            }
        }
    } // namespace
} // namespace sw

namespace sw
{
    WesternHorseComponent::WesternHorseComponent()
        : _director{}
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
        _horse.drainEvents( listEvent );
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
        const WesternDirectorComponent* pDirector = pManager != nullptr ? GameDirectorComponent::resolve<WesternDirectorComponent>( *pManager, _director ) : nullptr;
        const WesternCatalog*           pCatalog  = pDirector != nullptr ? pDirector->findWesternCatalog() : nullptr;
        WesternHorseMountComponent*     pMount    = pOwner != nullptr ? pOwner->getComponent<WesternHorseMountComponent>() : nullptr;
        const utf8*                     pName     = pOwner != nullptr ? pOwner->getName().c_str() : "?";
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR( "[Western] horse '%#' has no western catalog - %#", pName, pDirector == nullptr ? "Director does not point at a western director" : "the director did not start" );
            return false;
        }
        if ( pMount == nullptr )
        {
            SW_LOG_ERROR( "[Western] horse '%#' has no Western Horse Mount on its object", pName );
            return false;
        }
        if ( _horse.initialize( pCatalog, _breed, static_cast<uint32>( _seed ) ) == false )
        {
            SW_LOG_ERROR( "[Western] horse '%#' breed '%#' is not in the western catalog", pName, _breed.c_str() );
            return false;
        }
        pMount->bindHorse( &_horse );
        _bOpened = SW_TRUE;
        return true;
    }

    void WesternHorseComponent::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeHeader( outArchive, kStateTag, kStateVersion );
        outArchive << _bOpened;
        if ( _bOpened == SW_TRUE )
            _horse.writeState( outArchive );
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
        uint8      bWasOpened = SW_FALSE;
        const bool bHeader    = StateArchiveUtil::readHeader( archive, kStateTag, kStateVersion );
        if ( bHeader )
            archive >> bWasOpened;
        const bool bRead = bHeader && ( bWasOpened == SW_FALSE || _horse.readState( archive ) ) && archive.getRemainingBytes() == 0;
        _pendingStateBytes.clear();
        if ( bRead == false )
        {
            const GameObject* pOwner = getOwner();
            SW_LOG_WARNING( "[Western] horse '%#' state could not be restored - it starts fresh", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
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
