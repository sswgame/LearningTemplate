#include "pch.h"

#include "GameFramework/Runtime/Character/Control/Controller/IntentTrackControllerComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntentHistory.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
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
        if ( const expected<void> loaded = history.loadFromAsset( AssetID{ _trackFile } ); loaded.has_value() == false ) // 씬 데이터의 칸은 리소스다(작업 폴더 기준 디스크 경로가 아니다)
        {
            SW_LOG_ERROR_ID( "IntentTrackControllerComponent.IntentTrackControllerCould", "Intent track controller could not load %# (%#)", loaded.error().message(), loaded.error().detail() );
            return;
        }
        if ( loadTrack( history, trackName ) == false )
            SW_LOG_ERROR_ID( "IntentTrackControllerComponent.IntentRecordingNoComplete", "Intent recording '%#' has no complete track for pawn '%#'", _trackFile.c_str(), trackName.c_str() );
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
            pSystem->queuePossess( _returnController, TComponentHandle<PawnComponent>{ &pawn } );
        else
            pSystem->queuePossess( TComponentHandle<ControllerComponent>{ this }, TComponentHandle<PawnComponent>{} );
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
        const optional<int32> trackIndex = history.findTrack( pawnName );
        const bool            bLoaded    = trackIndex.has_value() && history.copyTrack( *trackIndex, listIntent );
        setTrack( listIntent );
        return bLoaded;
    }
} // namespace sw
