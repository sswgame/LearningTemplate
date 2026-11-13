#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/Editor/NetSimPreviewSceneSource.h"

#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    NetSimPreviewSceneSource::NetSimPreviewSceneSource( SceneManager& sceneManager )
        : _pSceneManager{ &sceneManager }
        , _listBorrowed{}
    {
    }

    NetSimPreviewSceneSource::~NetSimPreviewSceneSource()
    {
        // 하니스가 먼저 내려가 모두 돌려받았어야 한다. 남았으면 씬 매니저에서 내린다(씬이 남아 렌더되지 않게).
        SW_ASSERT( _listBorrowed.empty() && "NetSimPreviewSceneSource destroyed before the harness returned its scenes" );
        for ( const BorrowedScene& borrowed : _listBorrowed )
        {
            (void)_pSceneManager->destroyPreviewScene( borrowed._pScene );
        }
    }

    Scene* NetSimPreviewSceneSource::acquireScene( NetSimRole role, int32 worldIndex )
    {
        ScenePreviewPolicy policy{};
        policy._tick        = ScenePreviewTick::External; // 하니스가 고정 스텝으로 매니저를 직접 틱한다 — 씬 매니저가 한 번 더 틱하면 두 배로 간다
        policy._bPhysics    = true;
        policy._bAudio      = false;
        policy._bPlay       = false; // 하니스가 세션을 만든 뒤 `beginPlay` 를 부른다
        const string name   = string( role == NetSimRole::Server ? "NetSimServer" : "NetSimClient" ) + to_string( worldIndex );
        Scene*       pScene = _pSceneManager->createPreviewScene( name, policy );
        if ( pScene == nullptr )
            return nullptr;
        _listBorrowed.push_back( BorrowedScene{ pScene, worldIndex, _pSceneManager->getPreviewSceneKey( pScene ) } );
        return pScene;
    }

    void NetSimPreviewSceneSource::releaseScene( Scene& scene )
    {
        for ( size_t index = 0; index < _listBorrowed.size(); ++index )
        {
            if ( _listBorrowed[index]._pScene != &scene )
                continue;
            (void)_pSceneManager->destroyPreviewScene( &scene );
            _listBorrowed.erase( _listBorrowed.begin() + static_cast<std::ptrdiff_t>( index ) );
            return;
        }
        SW_ASSERT( false && "NetSimPreviewSceneSource::releaseScene got a scene it did not lend" );
    }

    optional<uint32> NetSimPreviewSceneSource::findSceneKey( int32 worldIndex ) const
    {
        for ( const BorrowedScene& borrowed : _listBorrowed )
        {
            if ( borrowed._worldIndex == worldIndex )
                return borrowed._sceneKey;
        }
        return nullopt;
    }
} // namespace sw
