#include "pch.h"

#include "GameFramework/Base/Control/IntentTrackControllerComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Control/ControlIntentHistory.h"
#include "GameFramework/Base/Control/ControlSystem.h"
#include "GameFramework/Base/Control/PawnComponent.h"

namespace sw
{
    SW_LOG_CALLER( "IntentTrackControllerComponent" );
} // namespace sw

namespace sw
{
    IntentTrackControllerComponent::IntentTrackControllerComponent()
        : _trackFile{}
        , _trackName{}
        , _bReleaseWhenDone{ false }
        , _listIntent{}
        , _returnController{}
        , _cursor{ 0 }
        , _bReturnPending{ false }
    {
    }

    void IntentTrackControllerComponent::onBeginPlay()
    {
        ControllerComponent::onBeginPlay();
        if ( _trackFile.empty() )
            return;
        const PawnComponent* pPawn       = findPawn();
        const GameObject*    pPawnObject = pPawn != nullptr ? pPawn->getOwner() : nullptr;
        const hashed_string  trackName   = _trackName.empty() == false ? _trackName : ( pPawnObject != nullptr ? pPawnObject->getName() : hashed_string{} );
        ControlIntentHistory history;
        string               error;
        if ( history.loadFromFile( _trackFile, error ) == false )
        {
            SW_LOG_ERROR( "Intent track controller could not load %#", error.c_str() );
            return;
        }
        if ( loadTrack( history, trackName ) == false )
            SW_LOG_ERROR( "Intent recording '%#' has no complete track for pawn '%#'", _trackFile.c_str(), trackName.c_str() );
    }

    void IntentTrackControllerComponent::produceIntent( const ControlFrameContext& context, const PawnComponent& pawn, ControlIntent& outIntent )
    {
        (void)context;
        if ( _cursor < _listIntent.size() )
        {
            outIntent = _listIntent[_cursor];
            setControlRotation( outIntent._controlYaw, outIntent._controlPitch );
            ++_cursor;
        }
        if ( _cursor < _listIntent.size() || _bReturnPending == false )
            return;
        // 조종자들이 의도를 내는 중이라 여기서 빙의를 옮기면 같은 틱에 두 조종자가 폰을 몬다 — 다음 틱 첫머리로 미룬다.
        _bReturnPending             = false;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        ControlSystem*     pSystem  = pManager != nullptr ? ControlSystem::find( *pManager ) : nullptr;
        if ( pSystem == nullptr )
            return;
        if ( _returnController.isValid() )
            pSystem->queuePossess( _returnController, pawn.getHandle() );
        else
            pSystem->queuePossess( getHandle(), ComponentHandle{} );
    }

    void IntentTrackControllerComponent::play( PawnComponent& pawn, const vector<ControlIntent>& listIntent, bool bReturnWhenDone )
    {
        const bool bAlreadyHeld = pawn.getController() == getHandle();
        if ( bAlreadyHeld == false )
            _returnController = pawn.getController();
        _bReleaseWhenDone = bReturnWhenDone;
        setTrack( listIntent );
        if ( bAlreadyHeld == false )
            possess( pawn );
    }

    void IntentTrackControllerComponent::setTrack( const vector<ControlIntent>& listIntent )
    {
        _listIntent     = listIntent;
        _cursor         = 0;
        _bReturnPending = _bReleaseWhenDone;
    }

    bool IntentTrackControllerComponent::loadTrack( const ControlIntentHistory& history, const hashed_string& pawnName )
    {
        vector<ControlIntent> listIntent;
        const int32           trackIndex = history.findTrack( pawnName );
        const bool            bLoaded    = trackIndex >= 0 && history.copyTrack( trackIndex, listIntent );
        setTrack( listIntent );
        return bLoaded;
    }
} // namespace sw
