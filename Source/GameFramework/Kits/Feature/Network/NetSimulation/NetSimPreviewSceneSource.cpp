#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimPreviewSceneSource.h"

#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    NetSimPreviewSceneSource::NetSimPreviewSceneSource( SceneManager& sceneManager )
        : _pSceneManager{ &sceneManager }
        , _listLent{}
    {
    }

    NetSimPreviewSceneSource::~NetSimPreviewSceneSource()
    {
        // 하니스가 먼저 내려가 모두 돌려받았어야 한다. 남았으면 씬 매니저에서 내린다(씬이 남아 렌더되지 않게).
        SW_ASSERT( _listLent.empty() && "NetSimPreviewSceneSource destroyed before the harness returned its scenes" );
        for ( const Lent& lent : _listLent )
        {
            (void)_pSceneManager->destroyPreviewScene( lent._pScene );
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
        _listLent.push_back( Lent{ pScene, worldIndex, _pSceneManager->getPreviewSceneKey( pScene ) } );
        return pScene;
    }

    void NetSimPreviewSceneSource::releaseScene( Scene& scene )
    {
        for ( size_t index = 0; index < _listLent.size(); ++index )
        {
            if ( _listLent[index]._pScene != &scene )
                continue;
            (void)_pSceneManager->destroyPreviewScene( &scene );
            _listLent.erase( _listLent.begin() + static_cast<std::ptrdiff_t>( index ) );
            return;
        }
        SW_ASSERT( false && "NetSimPreviewSceneSource::releaseScene got a scene it did not lend" );
    }

    uint32 NetSimPreviewSceneSource::findSceneKey( int32 worldIndex ) const
    {
        for ( const Lent& lent : _listLent )
        {
            if ( lent._worldIndex == worldIndex )
                return lent._sceneKey;
        }
        return kInvalidSceneKey;
    }
} // namespace sw
