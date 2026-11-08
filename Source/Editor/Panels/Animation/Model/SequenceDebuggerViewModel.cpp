#include "pch.h"

#include "Editor/Panels/Animation/Model/SequenceDebuggerViewModel.h"

#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequenceDiagnostics.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

namespace sw::editor
{
    namespace
    {
        struct SequenceDebuggerViewModelInternal
        {
            /** @brief 활성 씬의 시퀀스 플레이어를 모두 채웁니다(플레이 중이면 플레이 월드). */
            static void collectPlayers( vector<SequencePlayerComponent*>& outListPlayer )
            {
                outListPlayer.clear();
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pManager == nullptr )
                    return;
                for ( GameObject* pObject : pManager->getAllGameObjects() )
                {
                    SequencePlayerComponent* pPlayer = pObject != nullptr ? pObject->getComponent<SequencePlayerComponent>() : nullptr;
                    if ( pPlayer != nullptr && pPlayer->getOwner() != nullptr )
                        outListPlayer.push_back( pPlayer );
                }
            }

            static const utf8* getHistoryKindLabel( SequenceHistoryKind kind )
            {
                switch ( kind )
                {
                    case SequenceHistoryKind::Started:
                        return "Started";
                    case SequenceHistoryKind::Finished:
                        return "Finished";
                    case SequenceHistoryKind::Stopped:
                        return "Stopped";
                    case SequenceHistoryKind::Event:
                        return "Event";
                    case SequenceHistoryKind::CameraSignal:
                        return "Camera";
                    case SequenceHistoryKind::UnboundBinding:
                        return "Unbound";
                    case SequenceHistoryKind::Span:
                        return "Span";
                }
                return "?";
            }

            static const utf8* getSignalLabel( uint8 detail )
            {
                switch ( static_cast<SequenceCameraSignalKind>( detail ) )
                {
                    case SequenceCameraSignalKind::CutBegin:
                        return "cut";
                    case SequenceCameraSignalKind::CinematicBegin:
                        return "blend in";
                    case SequenceCameraSignalKind::CinematicEnd:
                        return "blend out";
                    case SequenceCameraSignalKind::CutPrewarm:
                        return "standby view";
                    case SequenceCameraSignalKind::Shake:
                        return "shake";
                }
                return "?";
            }
        };
    } // namespace

    SequenceDebuggerViewModel::SequenceDebuggerViewModel()
        : _listProvider{}
        , _watched{}
    {
    }

    void SequenceDebuggerViewModel::gather( SequenceDebuggerViewState& outState )
    {
        using Internal = SequenceDebuggerViewModelInternal;
        outState._listPlayer.clear();
        outState._listActiveItem.clear();
        outState._listBinding.clear();
        outState._listHistory.clear();
        outState._listProvider.clear();
        outState._watchedName  = "-";
        outState._watchedIndex = 0;
        outState._bHasWatched  = SW_FALSE;

        vector<SequencePlayerComponent*> listPlayer;
        Internal::collectPlayers( listPlayer );
        if ( listPlayer.empty() )
            return;
        // 고른 플레이어가 없거나 사라졌으면 첫 플레이어를 본다.
        uint32 watchedIndex = 0;
        for ( uint32 index = 0; index < static_cast<uint32>( listPlayer.size() ); ++index )
        {
            const GameObject*          pOwner = listPlayer[index]->getOwner();
            SequenceDebuggerPlayerRow& row    = outState._listPlayer.emplace_back();
            row._name                         = pOwner->getName().c_str();
            row._handle                       = pOwner->getHandle();
            row._objectID                     = static_cast<uint32>( pOwner->getObjectID() );
            if ( row._handle == _watched )
                watchedIndex = index;
        }
        const SequencePlayerComponent& player = *listPlayer[watchedIndex];
        const SequenceAsset&           asset  = player.getAsset();
        outState._watchedIndex                = watchedIndex;
        outState._watchedName                 = outState._listPlayer[watchedIndex]._name;
        outState._bHasWatched                 = SW_TRUE;
        outState._bPlaying                    = player.isPlaying() ? SW_TRUE : SW_FALSE;
        outState._currentFrame                = player.getCurrentFrame();
        outState._frameMin                    = asset._frameMin;
        outState._frameMax                    = asset._frameMax;

        vector<const SequenceTrackItem*> listActive;
        asset.collectActiveItems( player.getCurrentFrame(), listActive );
        for ( const SequenceTrackItem* pItem : listActive )
        {
            const SequenceBinding*                  pBinding = asset.findBinding( pItem->_binding );
            fixed_string<constant::buffer::kMax256> text;
            formatstring( text.data(), text.capacity(), "%# (%#) %#..%#", pItem->_name, pBinding != nullptr ? pBinding->_name.c_str() : "-", pItem->_start, pItem->_end );
            outState._listActiveItem.emplace_back( text.c_str() );
        }

        for ( const SequenceBinding& binding : asset._listBinding )
        {
            SequenceDebuggerBindingRow& row = outState._listBinding.emplace_back();
            row._id                         = binding._id;
            row._name                       = binding._name.c_str();
            const GameObject* const pBound  = player.findBoundObject( binding._id );
            if ( pBound != nullptr )
            {
                row._state      = SequenceDebuggerBindingState::Bound;
                row._objectName = pBound->getName().c_str();
            }
            else
            {
                row._state = asset.findSpawnItem( binding._id ) != nullptr ? SequenceDebuggerBindingState::NotSpawnedYet : SequenceDebuggerBindingState::Unbound;
            }
        }

        // 최근 줄이 앞이다.
        const SequencePlaybackHistory& history = player.getHistory();
        for ( uint32 index = history.getCount(); index > 0; --index )
        {
            const SequenceHistoryEntry& entry = history.getEntry( index - 1 );
            SequenceDebuggerHistoryRow& row   = outState._listHistory.emplace_back();
            row._frame                        = entry._frame;
            row._pKindLabel                   = Internal::getHistoryKindLabel( entry._kind );
            row._what                         = entry._name.c_str();
            if ( entry._kind == SequenceHistoryKind::CameraSignal )
                row._what += string( " - " ) + Internal::getSignalLabel( entry._detail );
        }

        // 키트의 해석 — 그 키트 모듈이 올라와 있을 때만 제공자가 목록에 있다.
        SequenceDiagnosticRegistry::collectProviders( _listProvider );
        string text;
        for ( const SequenceDiagnosticRegistration* pProvider : _listProvider )
        {
            text.clear();
            if ( pProvider->_pDescribe( player, text ) == false )
                continue;
            SequenceDebuggerProviderRow& row = outState._listProvider.emplace_back();
            row._name                        = pProvider->_pName;
            row._text                        = text;
        }
    }
} // namespace sw::editor
