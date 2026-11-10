#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/EditorNetSimSession.h"

#include "Core/Foundation/Constants.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/GUI/Panel/EditorPreviewViewport.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"

#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw::editor
{
    EditorNetSimSession::EditorNetSimSession()
        : _pSceneSource{}
        , _pGame{}
        , _pHarness{}
        , _listView{}
        , _stepPacer{}
    {
    }

    EditorNetSimSession::~EditorNetSimSession()
    {
        stop();
    }

    expected<void> EditorNetSimSession::start( const EditorNetSimSettings& settings )
    {
        stop();
        const NetSimGameEntry* pEntry = NetSimGameRegistry::findGame( settings._gameName );
        if ( pEntry == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "no in-process game has this name (NetSimGameRegistry)", settings._gameName } );
        if ( settings._clientCount < 1 || settings._clientCount > kMaxClients )
        {
            StringBuilder<constant::buffer::kMax128> detailBuilder;
            detailBuilder.appendFormat( "%# (1 to %#)", settings._clientCount, kMaxClients );
            return unexpected( Error{ ErrorKind::InvalidArgument, "the client count is out of range", detailBuilder.view() } );
        }
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "no scene manager" } );

        _pSceneSource = make_unique<NetSimPreviewSceneSource>( *pSceneManager );
        _pGame        = pEntry->_pfnCreate();
        if ( _pGame == nullptr )
        {
            stop();
            return unexpected( Error{ ErrorKind::ExternalFailure, "the game created nothing", settings._gameName } );
        }
        NetSimConfig config  = settings._harness;
        config._pSceneSource = _pSceneSource.get();
        _pHarness            = make_unique<NetSimHarness>();
        if ( _pHarness->initialize( pSceneManager->getEngineServices(), config, _pGame.get() ) == false )
        {
            stop();
            return unexpected( Error{ ErrorKind::ExternalFailure, "the harness did not start (see the log)" } );
        }
        for ( int32 index = 0; index < settings._clientCount; ++index )
        {
            if ( _pHarness->addClient( settings._conditions ) < 0 )
            {
                StringBuilder<constant::buffer::kMax128> detailBuilder;
                detailBuilder.appendFormat( "client %#", index + 1 );
                stop();
                return unexpected( Error{ ErrorKind::ExternalFailure, "a client did not start (see the log)", detailBuilder.view() } );
            }
        }
        SW_LOG_INFO_ID( "NetSim.PlayStarted", "In-process play started: %# with %# client(s)", settings._gameName, settings._clientCount );
        return {};
    }

    void EditorNetSimSession::stop()
    {
        // 뷰포트(렌더 타깃)를 먼저 놓고, 하니스가 씬을 공급자에 돌려준 뒤 공급자를 내린다.
        _listView.clear();
        if ( _pHarness != nullptr )
            _pHarness->shutdown();
        _pHarness.reset();
        _pGame.reset();
        _pSceneSource.reset();
        _stepPacer.reset();
    }

    uint32 EditorNetSimSession::update( float64 frameSeconds )
    {
        if ( _pHarness == nullptr )
            return 0;
        const uint32 stepCount = _stepPacer.consume( frameSeconds, _pHarness->getTickInterval(), kMaxStepsPerFrame );
        _pHarness->stepTicks( stepCount );
        return stepCount;
    }

    bool EditorNetSimSession::setInputFocus( int32 worldIndex )
    {
        return _pHarness != nullptr && _pHarness->setInputFocus( worldIndex );
    }

    int32 EditorNetSimSession::getInputFocus() const
    {
        return _pHarness != nullptr ? _pHarness->getInputFocus() : constant::invalid_index::kInt32;
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
        const optional<uint32>            sceneKey  = _pSceneSource->findSceneKey( worldIndex );
        if ( sceneKey.has_value() == false || pViewport->attachScene( pWorld->getScene(), *sceneKey ) == false )
        {
            SW_LOG_ERROR_ID( "EditorNetSimSession.ProcessPlayWorldNo", "In-process play: world %# has no preview scene key", worldIndex );
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

    SW_EDITOR_SUBSYSTEM( EditorNetSimSession, 9420 );
} // namespace sw::editor
