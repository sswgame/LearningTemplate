#include "pch.h"

#include "GameFramework/Base/Actor/Control/Controller/IntentPlayback.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/Controller/IntentTrackControllerComponent.h"
#include "GameFramework/Base/Actor/Control/Intent/ControlIntentHistory.h"
#include "GameFramework/Base/Actor/Control/Pawn/PawnComponent.h"

namespace sw
{
    SW_LOG_CALLER( "IntentPlayback" );
} // namespace sw

namespace sw
{
    uint32 IntentPlayback::start( GameObjectManager& manager, const ControlIntentHistory& history, bool bReturnWhenDone, uint32* pOutSkippedCount )
    {
        uint32 started = 0;
        uint32 skipped = 0;
        for ( int32 trackIndex = 0; trackIndex < history.getTrackCount(); ++trackIndex )
        {
            const hashed_string   pawnName    = history.getTrack( trackIndex )._pawnName;
            GameObject*           pPawnObject = manager.findGameObjectByName( pawnName );
            PawnComponent*        pPawn       = pPawnObject != nullptr ? pPawnObject->getComponent<PawnComponent>() : nullptr;
            vector<ControlIntent> listIntent;
            if ( pPawn == nullptr || history.copyTrack( trackIndex, listIntent ) == false )
            {
                SW_LOG_WARNING( "Intent playback skipped the track of '%#' (no pawn in the scene or the track has empty ticks)", pawnName.c_str() );
                ++skipped;
                continue;
            }
            const string controllerName = string( kControllerPrefix ) + pawnName.c_str();
            GameObject*  pTrackObject   = manager.findGameObjectByName( hashed_string( controllerName ) );
            if ( pTrackObject == nullptr )
                pTrackObject = manager.createGameObject( hashed_string( controllerName ) );
            IntentTrackControllerComponent* pTrack = pTrackObject != nullptr ? pTrackObject->getComponent<IntentTrackControllerComponent>() : nullptr;
            if ( pTrack == nullptr && pTrackObject != nullptr )
                pTrack = pTrackObject->addComponent<IntentTrackControllerComponent>();
            if ( pTrack == nullptr )
            {
                ++skipped;
                continue;
            }
            pTrack->play( *pPawn, listIntent, bReturnWhenDone );
            ++started;
        }
        if ( pOutSkippedCount != nullptr )
            *pOutSkippedCount = skipped;
        return started;
    }

    bool IntentPlayback::startFromFile( GameObjectManager& manager, string_view path, bool bReturnWhenDone, string& outError, uint32* pOutStartedCount )
    {
        ControlIntentHistory history;
        if ( history.loadFromFile( path, outError ) == false )
        {
            if ( pOutStartedCount != nullptr )
                *pOutStartedCount = 0;
            return false;
        }
        const uint32 started = start( manager, history, bReturnWhenDone );
        if ( pOutStartedCount != nullptr )
            *pOutStartedCount = started;
        return true;
    }
} // namespace sw
