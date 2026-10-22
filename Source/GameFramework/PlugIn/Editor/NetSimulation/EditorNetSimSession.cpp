#include "pch.h"

#include "GameFramework/PlugIn/Editor/NetSimulation/EditorNetSimSession.h"

#include "Editor/Common/GUI/EditorPreviewViewport.h"

#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorNetSimSession" );

    namespace
    {
        /** @brief 지금 돌고 있는 세션(`EditorNetSimSession::findRunning`). `start` 가 채우고 `stop` 이 자기일 때만 비운다. */
        EditorNetSimSession* s_pRunningSession{ nullptr };
    } // namespace

    EditorNetSimSession* EditorNetSimSession::findRunning()
    {
        return s_pRunningSession;
    }

    EditorNetSimSession::EditorNetSimSession()
        : _pSceneSource{}
        , _pGame{}
        , _pHarness{}
        , _listView{}
        , _accumulatedSeconds{ 0.0 }
    {
    }

    EditorNetSimSession::~EditorNetSimSession()
    {
        stop();
    }

    bool EditorNetSimSession::start( const EditorNetSimSettings& settings, string& outError )
    {
        stop();
        const NetSimGameEntry* pEntry = NetSimGameRegistry::findGame( settings._gameName );
        if ( pEntry == nullptr )
        {
            outError = "no in-process game named '" + settings._gameName + "' (NetSimGameRegistry)";
            return false;
        }
        if ( settings._clientCount < 1 || settings._clientCount > kMaxClients )
        {
            outError = "client count must be 1 to " + to_string( kMaxClients );
            return false;
        }
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
        {
            outError = "no scene manager";
            return false;
        }

        _pSceneSource = make_unique<NetSimPreviewSceneSource>( *pSceneManager );
        _pGame        = pEntry->_pfnCreate();
        if ( _pGame == nullptr )
        {
            outError = "the game '" + settings._gameName + "' created nothing";
            stop();
            return false;
        }
        NetSimConfig config  = settings._harness;
        config._pSceneSource = _pSceneSource.get();
        _pHarness            = make_unique<NetSimHarness>();
        if ( _pHarness->initialize( pSceneManager->getEngineServices(), config, _pGame.get() ) == false )
        {
            outError = "the harness did not start (see the log)";
            stop();
            return false;
        }
        for ( int32 index = 0; index < settings._clientCount; ++index )
        {
            if ( _pHarness->addClient( settings._conditions ) < 0 )
            {
                outError = "client " + to_string( index + 1 ) + " did not start (see the log)";
                stop();
                return false;
            }
        }
        s_pRunningSession = this;
        SW_LOG_INFO( "In-process play started: %# with %# client(s)", settings._gameName, settings._clientCount );
        return true;
    }

    void EditorNetSimSession::stop()
    {
        // 뷰포트(렌더 타깃)를 먼저 놓고, 하니스가 씬을 공급자에 돌려준 뒤 공급자를 내린다.
        if ( s_pRunningSession == this )
            s_pRunningSession = nullptr;
        _listView.clear();
        if ( _pHarness != nullptr )
            _pHarness->shutdown();
        _pHarness.reset();
        _pGame.reset();
        _pSceneSource.reset();
        _accumulatedSeconds = 0.0;
    }

    uint32 EditorNetSimSession::update( float64 frameSeconds )
    {
        if ( _pHarness == nullptr )
            return 0;
        const float64 interval = _pHarness->getTickInterval();
        _accumulatedSeconds += frameSeconds;
        uint32 stepCount = 0;
        while ( _accumulatedSeconds >= interval && stepCount < kMaxStepsPerFrame )
        {
            _pHarness->step();
            _accumulatedSeconds -= interval;
            ++stepCount;
        }
        if ( stepCount == kMaxStepsPerFrame )
            _accumulatedSeconds = 0.0; // 중단점 뒤 밀린 시간을 몰아 돌지 않는다
        return stepCount;
    }

    bool EditorNetSimSession::setInputFocus( int32 worldIndex )
    {
        return _pHarness != nullptr && _pHarness->setInputFocus( worldIndex );
    }

    int32 EditorNetSimSession::getInputFocus() const
    {
        return _pHarness != nullptr ? _pHarness->getInputFocus() : -1;
    }

    void EditorNetSimSession::collectWorldIndices( vector<int32>& outListWorldIndex ) const
    {
        outListWorldIndex.clear();
        if ( _pHarness == nullptr )
            return;
        outListWorldIndex.push_back( 0 );
        vector<NetSimWorld*> listClient;
        _pHarness->collectClients( listClient );
        for ( const NetSimWorld* pWorld : listClient )
        {
            outListWorldIndex.push_back( pWorld->getWorldIndex() );
        }
    }

    EditorPreviewViewport* EditorNetSimSession::findViewport( int32 worldIndex )
    {
        if ( _pHarness == nullptr || _pSceneSource == nullptr )
            return nullptr;
        for ( WorldView& view : _listView )
        {
            if ( view._worldIndex == worldIndex )
                return view._pViewport.get();
        }
        NetSimWorld* pWorld = worldIndex == 0 ? &_pHarness->getServer() : _pHarness->findClient( worldIndex );
        if ( pWorld == nullptr )
            return nullptr;
        unique_ptr<EditorPreviewViewport> pViewport = make_unique<EditorPreviewViewport>();
        if ( pViewport->attachScene( pWorld->getScene(), _pSceneSource->findSceneKey( worldIndex ) ) == false )
        {
            SW_LOG_ERROR( "In-process play: world %# has no preview scene key", worldIndex );
            return nullptr;
        }
        // 월드 내용을 아는 게임이 카메라와 바닥을 알리면 그대로 잡고, 조명 환경(방향광 · 하늘 · 바닥)은 그 월드 씬에 더한다.
        NetSimPreviewFraming framing;
        if ( _pGame != nullptr && _pGame->describePreviewFraming( framing ) )
        {
            pViewport->setOrbit( framing._target, framing._distance, framing._yaw, framing._pitch );
            EditorPreviewEnvironment environment;
            environment._bFloor    = framing._floorSize > 0.0f;
            environment._floorSize = framing._floorSize;
            (void)EditorPreviewViewport::spawnEnvironment( pWorld->getScene(), environment ); // 만든 오브젝트 수는 쓰지 않는다 — 실패해도 씬은 키 라이트 없이 그려진다
        }
        _listView.push_back( WorldView{ worldIndex, std::move( pViewport ) } );
        return _listView.back()._pViewport.get();
    }
} // namespace sw::editor
