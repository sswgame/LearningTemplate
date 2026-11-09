#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/Model/MultiplayerInProcessViewModel.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/EditorNetSimSession.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"

namespace sw::editor
{
    static_assert( MultiplayerInProcessViewModel::kMaxClients == EditorNetSimSession::kMaxClients, "the view model's client limit follows the session" );

    namespace
    {
        struct MultiplayerInProcessViewModelInternal
        {
            /** @brief 이 확장의 에디터 서브시스템입니다(필수 — 확장이 올라와 있으면 있다). */
            static EditorNetSimSession& getSession() { return editor::getSubsystem<EditorNetSimSession>(); }
        };
    } // namespace

    MultiplayerInProcessViewModel::MultiplayerInProcessViewModel()
        : _lastError{}
    {
    }

    void MultiplayerInProcessViewModel::gather( MultiplayerInProcessViewState& outState ) const
    {
        const vector<const NetSimGameEntry*>& listGame = NetSimGameRegistry::getGames();
        outState._listGameName.resize( listGame.size() );
        for ( size_t index = 0; index < listGame.size(); ++index )
        {
            outState._listGameName[index] = listGame[index]->_name;
        }
        const EditorNetSimSession& session = MultiplayerInProcessViewModelInternal::getSession();
        outState._lastError                = _lastError;
        outState._bRunning                 = session.isRunning() ? SW_TRUE : SW_FALSE;
        outState._tick                     = session.isRunning() ? static_cast<uint64>( session.getHarness()->getTick() ) : 0;
        gatherWorlds( outState );
    }

    void MultiplayerInProcessViewModel::gatherWorlds( MultiplayerInProcessViewState& outState ) const
    {
        const EditorNetSimSession& session = MultiplayerInProcessViewModelInternal::getSession();
        outState._inputFocus               = session.getInputFocus();
        session.collectWorldIndices( outState._listWorldIndex );
    }

    void MultiplayerInProcessViewModel::start( int32 gameIndex, int32 clientCount )
    {
        const vector<const NetSimGameEntry*>& listGame = NetSimGameRegistry::getGames();
        if ( gameIndex < 0 || static_cast<size_t>( gameIndex ) >= listGame.size() )
            return;
        EditorNetSimSession& session = MultiplayerInProcessViewModelInternal::getSession();
        EditorNetSimSettings settings;
        settings._gameName    = listGame[static_cast<size_t>( gameIndex )]->_name;
        settings._clientCount = clientCount;
        _lastError.clear();
        if ( const expected<void> started = session.start( settings ); started.has_value() )
            (void)session.setInputFocus( 1 ); // 첫 클라이언트가 입력을 받으며 시작한다
        else
            _lastError = string( started.error().message() ) + " (" + string( started.error().detail() ) + ")";
    }

    void MultiplayerInProcessViewModel::stop()
    {
        MultiplayerInProcessViewModelInternal::getSession().stop();
    }

    void MultiplayerInProcessViewModel::advance( float64 frameSeconds )
    {
        (void)MultiplayerInProcessViewModelInternal::getSession().update( frameSeconds );
    }

    void MultiplayerInProcessViewModel::recordGameInputFocused()
    {
        editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>().recordGameInputSurfaceFocused();
    }

    void MultiplayerInProcessViewModel::setInputFocus( int32 worldIndex )
    {
        (void)MultiplayerInProcessViewModelInternal::getSession().setInputFocus( worldIndex );
    }

    EditorPreviewViewport* MultiplayerInProcessViewModel::findViewport( int32 worldIndex )
    {
        return MultiplayerInProcessViewModelInternal::getSession().findViewport( worldIndex );
    }
} // namespace sw::editor
