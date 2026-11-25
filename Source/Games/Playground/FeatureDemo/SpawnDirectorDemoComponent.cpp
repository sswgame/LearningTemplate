#include "pch.h"

#include "Games/Playground/FeatureDemo/SpawnDirectorDemoComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    namespace
    {
        struct SpawnDirectorDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "FDSP" );
            static constexpr uint32 kStateVersion = 0;
        };
    } // namespace

    SpawnDirectorDemoComponent::SpawnDirectorDemoComponent()
        : _spawnTable{}
        , _spawnPrefab{}
        , _seed{ 1 }
        , _spawnedTotal{ 0 }
        , _despawnedTotal{ 0 }
        , _aliveCount{ 0 }
        , _budget{ 0.0f }
        , _pTable{}
        , _director{}
        , _listView{}
    {
    }

    SpawnDirectorDemoComponent::~SpawnDirectorDemoComponent() = default;

    bool SpawnDirectorDemoComponent::startGame()
    {
        if ( acquireRequired( _spawnTable, _pTable ) == false )
            return false;
        _director.initialize( _pTable.get(), _seed );
        refreshState();
        return true;
    }

    void SpawnDirectorDemoComponent::refreshState()
    {
        _aliveCount = _director.getTotalAliveCount();
        _budget     = _director.getBudget();
    }

    void SpawnDirectorDemoComponent::updateGame( float32 deltaTime )
    {
        // 사라진 것(파괴 · 죽음)을 먼저 알린다 — 그 자리가 이번 갱신의 동시 상한에 든다.
        for ( SpawnedView& view : _listView )
        {
            if ( findGameObject( view._object ) != nullptr )
                continue;
            if ( _director.notifyDespawned( view._spawnID ) )
                ++_despawnedTotal;
            view._spawnID = 0;
        }
        VectorUtil::removeIf( _listView, []( const SpawnedView& view )
        { return view._spawnID == 0; } );

        _spawnedTotal += _director.update( deltaTime );
        _director.discardEvents(); // 세우는 일은 살아 있는 목록에서 한다(불러온 판과 같은 길)
        refreshState();
        if ( hasPendingSpawn() )
            scheduleFlush();
    }

    bool SpawnDirectorDemoComponent::hasPendingSpawn() const
    {
        return _listView.size() < static_cast<size_t>( _director.getTotalAliveCount() );
    }

    void SpawnDirectorDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        (void)bRespawnViews; // 처음이든 걷은 뒤든 살아 있는 것 가운데 세우지 않은 것을 세운다
        vector<uint32> listAlive;
        _director.collectAliveSpawnIDs( listAlive );
        for ( const uint32 spawnID : listAlive )
        {
            if ( VectorUtil::containsBy<&SpawnedView::_spawnID>( _listView, spawnID ) )
                continue;
            fixed_string<constant::buffer::kMax64> name;
            formatstring( name.data(), name.capacity(), "Spawn.%#", spawnID );
            const GameObject* pObject = spawnPrefab( manager, _spawnPrefab.path(), name.c_str() );
            if ( pObject != nullptr )
                _listView.push_back( SpawnedView{ pObject->getHandle(), spawnID } );
        }
    }

    void SpawnDirectorDemoComponent::onViewsDespawned()
    {
        _listView.clear(); // 걷은 것은 죽은 것이 아니다 — 다음 플러시가 다시 세운다
    }

    void SpawnDirectorDemoComponent::serializeGameState( StateIO& io )
    {
        io.header( SpawnDirectorDemoComponentInternal::kStateTag, SpawnDirectorDemoComponentInternal::kStateVersion );
        if ( io.isWriting() )
        {
            io.value( _director );
            return;
        }
        SpawnDirector director;
        director.initialize( _pTable.get(), _seed );
        io.value( director );
        if ( io.isOk() == false )
            return;
        _director = std::move( director );
    }
} // namespace sw
