#include "pch.h"

#include "GameFramework/Runtime/Character/Control/Controller/IntentPlayback.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/TagIndex.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Controller/IntentTrackControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntentHistory.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    IntentTrackControllerComponent* IntentPlayback::getOrCreateTrackController( GameObjectManager& manager, string_view pawnName )
    {
        fixed_string<constant::buffer::kMax128> controllerText;
        formatstring( controllerText.data(), controllerText.capacity(), "%#%#", kControllerPrefix, pawnName );
        const hashed_string controllerName( controllerText.c_str() );
        GameObject*         pTrackObject = manager.findGameObjectByName( controllerName );
        if ( pTrackObject == nullptr )
            pTrackObject = manager.createGameObject( controllerName );
        if ( pTrackObject == nullptr )
            return nullptr;
        IntentTrackControllerComponent* pTrack = pTrackObject->getComponent<IntentTrackControllerComponent>();
        return pTrack != nullptr ? pTrack : pTrackObject->addComponent<IntentTrackControllerComponent>();
    }

    uint32 IntentPlayback::start( GameObjectManager& manager, const ControlIntentHistory& history, bool bReturnWhenDone, uint32 firstIndex, uint32* pOutSkippedCount )
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
                SW_LOG_WARNING_ID( "IntentPlayback.IntentPlaybackSkippedTrack", "Intent playback skipped the track of '%#' (no pawn in the scene or the track has empty ticks)", pawnName.c_str() );
                ++skipped;
                continue;
            }
            if ( firstIndex > 0 )
            {
                if ( firstIndex >= listIntent.size() )
                    continue; // 이 트랙은 firstIndex 틱 전에 이미 끝났다 — 폰은 지금 상태 그대로다
                listIntent.erase( listIntent.begin(), listIntent.begin() + static_cast<std::ptrdiff_t>( firstIndex ) );
            }
            IntentTrackControllerComponent* pTrack = getOrCreateTrackController( manager, pawnName.view() );
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

    expected<uint32> IntentPlayback::startFromFile( GameObjectManager& manager, const AbsolutePath& path, bool bReturnWhenDone )
    {
        ControlIntentHistory history;
        if ( expected<void> loaded = history.loadFromFile( path ); loaded.has_value() == false )
            return unexpected( loaded.error() );
        return start( manager, history, bReturnWhenDone, 0 );
    }
} // namespace sw
