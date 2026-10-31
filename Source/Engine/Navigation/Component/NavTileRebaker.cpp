#include "pch.h"

#include "Engine/Navigation/Component/NavTileRebaker.h"

#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Navigation/Component/NavGeometryCollector.h"
#include "Engine/Navigation/Component/NavMeshObstacleComponent.h"
#include "Engine/Navigation/Component/NavMeshRuntime.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "SceneNavigation" );

    namespace
    {
        struct NavTileRebakerInternal
        {
            /** @brief 장애물 회전이 이보다 바뀌면 다시 뚫습니다(라디안). */
            static constexpr float32 kObstacleTurnThreshold = 0.1f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    NavTileRebaker::NavTileRebaker()
        : _listDirtyArea{}
        , _listObstacleArea{}
        , _rebakedTileCount{ 0 }
        , _dirtyLock{}
        , _bGeometryDirty{ false }
        , _bObstacleVolumeDirty{ false }
    {
    }

    void NavTileRebaker::clear()
    {
        _dirtyLock.lock();
        _listDirtyArea.clear();
        _bGeometryDirty = false;
        _dirtyLock.unlock();
        _listObstacleArea.clear();
        _bObstacleVolumeDirty = false;
    }

    void NavTileRebaker::invalidateArea( const AABB& bounds, bool bGeometryChanged )
    {
        if ( bounds.isValid() == false )
            return;
        _dirtyLock.lock();
        _listDirtyArea.push_back( bounds );
        _bGeometryDirty = _bGeometryDirty || bGeometryChanged;
        _dirtyLock.unlock();
    }

    uint32 NavTileRebaker::getPendingAreaCount() const
    {
        _dirtyLock.lock();
        const uint32 count = static_cast<uint32>( _listDirtyArea.size() );
        _dirtyLock.unlock();
        return count;
    }

    void NavTileRebaker::collectObstacleVolumes( const vector<NavMeshObstacleComponent*>& listObstacle, vector<NavConvexVolume>& outListVolume )
    {
        // 뚫린 장애물의 부피는 `updateObstacles` 가 만들어 둔 것이다(컴포넌트의 `_volume`). 베이크 워커가 읽는 사본이라 베낀다.
        outListVolume.clear();
        for ( const NavMeshObstacleComponent* pObstacle : listObstacle )
        {
            if ( pObstacle->_bCarved == SW_TRUE && pObstacle->_volume._listPoint.empty() == false )
                outListVolume.push_back( pObstacle->_volume );
        }
    }

    void NavTileRebaker::invalidateObstacleArea( const AABB& bounds )
    {
        if ( bounds.isValid() )
            _listObstacleArea.push_back( bounds );
    }

    void NavTileRebaker::forgetObstacle( NavMeshObstacleComponent& obstacle, const vector<unique_ptr<NavMeshRuntime>>& listRuntime )
    {
        if ( obstacle._bCarved == SW_TRUE )
        {
            invalidateObstacleArea( obstacle._carvedBounds );
            _bObstacleVolumeDirty = true;
        }
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
        {
            if ( pRuntime->usesObstacleCache() )
                pRuntime->removeCacheObstacle( &obstacle );
        }
        obstacle._bCarved = SW_FALSE;
    }

    void NavTileRebaker::updateObstacleCaches( const vector<unique_ptr<NavMeshRuntime>>& listRuntime, float32 deltaSeconds )
    {
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
        {
            if ( pRuntime->usesObstacleCache() )
                (void)pRuntime->_pNavMesh->updateCacheObstacles( deltaSeconds ); // 다시 지었으면 내비메시 번호가 오른다 — 경로 캐시가 그것을 본다
        }
    }

    void NavTileRebaker::addCarvedObstaclesToCache( NavMeshRuntime& runtime, const vector<NavMeshObstacleComponent*>& listObstacle )
    {
        for ( const NavMeshObstacleComponent* pObstacle : listObstacle )
        {
            NavCacheObstacleDesc desc;
            if ( pObstacle->_bCarved == SW_FALSE || pObstacle->makeCacheObstacle( desc ) == false )
                continue;
            const NavObstacleRef ref = runtime._pNavMesh->addCacheObstacle( desc );
            if ( ref != 0 )
                runtime._listCacheObstacle.push_back( NavCacheObstacleEntry{ pObstacle, ref } );
        }
    }

    void NavTileRebaker::updateObstacles( const vector<unique_ptr<NavMeshRuntime>>& listRuntime, const vector<NavMeshObstacleComponent*>& listObstacle, float32 moveThreshold )
    {
        using Internal = NavTileRebakerInternal;
        for ( NavMeshObstacleComponent* pObstacle : listObstacle )
        {
            if ( SceneNavigationUtil::isLive( *pObstacle ) == false )
            {
                if ( pObstacle->_bCarved == SW_TRUE )
                    forgetObstacle( *pObstacle, listRuntime );
                continue;
            }
            // 따르는 트랜스폼(칸의 변경 번호)도 모양도 그대로인 뚫린 장애물은 부피를 다시 만들지 않고 건너뛴다 — 정수 비교 하나다.
            const bool bVolumeChanged = pObstacle->refreshVolume();
            if ( bVolumeChanged == false && pObstacle->_bCarved == SW_TRUE )
                continue;
            const NavConvexVolume& volume = pObstacle->_volume;
            if ( volume._listPoint.size() < 2 )
                continue;
            const AABB    bounds   = volume.computeBounds();
            const float3  center   = ( bounds._min + bounds._max ) * 0.5f;
            const float3& first    = volume._listPoint[0];
            const float3& second   = volume._listPoint[1];
            const float32 yaw      = MathUtil::atan2( second._x - first._x, second._z - first._z );
            const bool    bMoved   = ( center - pObstacle->_carvedPosition ).getLength() > moveThreshold;
            const bool    bTurned  = MathUtil::abs( MathUtil::wrapAngle( yaw - pObstacle->_carvedYaw ) ) > Internal::kObstacleTurnThreshold;
            const bool    bChanged = pObstacle->_bCarved == SW_FALSE || pObstacle->_bShapeDirty == SW_TRUE || bMoved || bTurned;
            if ( bChanged == false )
                continue;
            if ( pObstacle->_bCarved == SW_TRUE )
                invalidateObstacleArea( pObstacle->_carvedBounds );
            invalidateObstacleArea( bounds );
            pObstacle->_carvedBounds   = bounds;
            pObstacle->_carvedPosition = center;
            pObstacle->_carvedYaw      = yaw;
            pObstacle->_bCarved        = SW_TRUE;
            pObstacle->_bShapeDirty    = SW_FALSE;
            _bObstacleVolumeDirty      = true;
            // 캐시 종류 — 옛 장애물을 빼고 새 자세로 넣는다(타일은 다음 `updateObstacleCaches` 가 층에서 다시 짓는다).
            NavCacheObstacleDesc desc;
            const bool           bHasDesc = pObstacle->makeCacheObstacle( desc );
            for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
            {
                if ( pRuntime->usesObstacleCache() == false )
                    continue;
                pRuntime->removeCacheObstacle( pObstacle );
                const NavObstacleRef ref = bHasDesc ? pRuntime->_pNavMesh->addCacheObstacle( desc ) : NavObstacleRef{ 0 };
                if ( ref != 0 )
                    pRuntime->_listCacheObstacle.push_back( NavCacheObstacleEntry{ pObstacle, ref } );
            }
        }
    }

    void NavTileRebaker::processDirtyAreas( const vector<unique_ptr<NavMeshRuntime>>& listRuntime, const vector<NavMeshObstacleComponent*>& listObstacle,
                                            GameObjectManager& manager, const NavMeshSettings& settings, const vector<INavGeometrySource*>& listSource,
                                            NavMeshArrival arrival )
    {
        vector<AABB> listArea;
        _dirtyLock.lock();
        listArea.swap( _listDirtyArea );
        const bool bGeometryDirty = _bGeometryDirty;
        _bGeometryDirty           = false;
        _dirtyLock.unlock();
        vector<AABB> listObstacleArea;
        listObstacleArea.swap( _listObstacleArea );
        if ( listRuntime.empty() )
        {
            _bObstacleVolumeDirty = false;
            return;
        }
        const bool bNothingChanged = listArea.empty() && listObstacleArea.empty() && _bObstacleVolumeDirty == false;
        if ( bNothingChanged )
            return;
        SW_PROFILE_SCOPE( "GT.Navigation.invalidate" );
        if ( _bObstacleVolumeDirty )
        {
            // 덧칠 부피는 다시 베이크 종류만 든다 — 캐시 종류는 장애물을 캐시로 받고 덧칠 부피를 받지 않는다(`INavMesh::bakeTile`).
            shared_ptr<vector<NavConvexVolume>> pVolume = make_shared<vector<NavConvexVolume>>();
            collectObstacleVolumes( listObstacle, *pVolume );
            for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
            {
                if ( pRuntime->usesObstacleCache() == false )
                    pRuntime->_pVolume = pVolume;
            }
            _bObstacleVolumeDirty = false;
        }
        if ( bGeometryDirty )
        {
            // 정적 기하가 바뀌었다(파괴 · 수정자) — 새 사본을 모은다. 베이크하는 중인 일은 옛 사본을 끝까지 읽는다.
            vector<shared_ptr<NavMeshGeometry>> listGeometry;
            listGeometry.reserve( listRuntime.size() );
            uint32 pendingMeshCount{ 0 };
            for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
            {
                listGeometry.push_back( make_shared<NavMeshGeometry>() );
                pendingMeshCount += NavGeometryCollector::collectGeometry( manager, pRuntime->_pSurface, settings, listSource, arrival, *listGeometry.back() );
            }
            if ( pendingMeshCount > 0 )
            {
                // 새로 놓인 메시를 아직 읽는 중이다 — 덜 모인 기하로 굽지 않는다. 옛 기하 · 타일을 그대로 두고 알린 것을 모두 다음 갱신으로 넘긴다.
                _dirtyLock.lock();
                _listDirtyArea.insert( _listDirtyArea.end(), listArea.begin(), listArea.end() );
                _bGeometryDirty = true;
                _dirtyLock.unlock();
                _listObstacleArea.insert( _listObstacleArea.end(), listObstacleArea.begin(), listObstacleArea.end() );
                return;
            }
            for ( size_t runtimeIndex = 0; runtimeIndex < listRuntime.size(); ++runtimeIndex )
            {
                NavMeshRuntime& runtime = *listRuntime[runtimeIndex];
                listGeometry[runtimeIndex]->rebuildSpatialIndex( MathUtil::max( 1.0f, runtime._pNavMesh->getTileGrid()._tileWorldSize * 0.25f ) );
                runtime._pGeometry = std::move( listGeometry[runtimeIndex] );
            }
        }
        vector<int2> listTile;
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
        {
            const bool bObstacleAreasApply = pRuntime->usesObstacleCache() == false;
            for ( const vector<AABB>* pListArea : { &listArea, &listObstacleArea } )
            {
                if ( pListArea == &listObstacleArea && bObstacleAreasApply == false )
                    continue;
                for ( const AABB& area : *pListArea )
                {
                    pRuntime->_pNavMesh->collectTilesOverlapping( area, listTile );
                    for ( const int2& tile : listTile )
                    {
                        pRuntime->addDirtyTile( tile );
                    }
                }
            }
        }
    }

    void NavTileRebaker::applyFinishedBakes( const vector<unique_ptr<NavMeshRuntime>>& listRuntime )
    {
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
        {
            vector<unique_ptr<NavTileBakeJob>>& listJob = pRuntime->_listJob;
            for ( size_t jobIndex = 0; jobIndex < listJob.size(); )
            {
                NavTileBakeJob& job = *listJob[jobIndex];
                if ( job._task.isCompleted() == false )
                {
                    ++jobIndex;
                    continue;
                }
                if ( job._bSucceeded && pRuntime->_pNavMesh->replaceTile( job._result ) )
                    ++_rebakedTileCount;
                else
                    SW_LOG_WARNING( "Navmesh '%#' tile (%#, %#) failed to rebake", pRuntime->_agentType.c_str(), job._tileX, job._tileZ );
                listJob[jobIndex] = std::move( listJob.back() );
                listJob.pop_back();
            }
        }
    }

    void NavTileRebaker::launchTileBakes( const vector<unique_ptr<NavMeshRuntime>>& listRuntime, uint32 maxConcurrent )
    {
        const uint32 jobLimit = MathUtil::max( 1u, maxConcurrent );
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
        {
            vector<int2>& listDirty = pRuntime->_listDirtyTile;
            for ( size_t dirtyIndex = 0; dirtyIndex < listDirty.size() && pRuntime->_listJob.size() < jobLimit; )
            {
                const int2 tile = listDirty[dirtyIndex];
                if ( pRuntime->isTileInFlight( tile ) )
                {
                    ++dirtyIndex;
                    continue;
                }
                listDirty.erase( listDirty.begin() + static_cast<ptrdiff_t>( dirtyIndex ) );
                unique_ptr<NavTileBakeJob> pJob = make_unique<NavTileBakeJob>();
                pJob->_pNavMesh                 = pRuntime->_pNavMesh.get();
                pJob->_pGeometry                = pRuntime->_pGeometry;
                pJob->_pVolume                  = pRuntime->_pVolume;
                pJob->_tileX                    = tile._x;
                pJob->_tileZ                    = tile._y;
                NavTileBakeJob* pRaw            = pJob.get();
                pRuntime->_listJob.push_back( std::move( pJob ) );
                // 워커가 없으면(테스트 · 도구) 지금 베이크한다 — 끼우기는 그래도 다음 갱신이다.
                if ( engine::areEngineServicesBound() )
                {
                    TaskManager& taskManager = engine::getSubsystem<TaskManager>();
                    pRaw->_task              = taskManager.emplaceTask( "NavTileBake", SW_DELEGATE_METHOD( TaskDelegate, &NavTileBakeJob::run, pRaw ) );
                    taskManager.addToStage( pRuntime->_bakeStage, pRaw->_task, "NavTileBake" );
                    pRaw->_task.submit();
                }
                else
                {
                    pRaw->run();
                }
            }
        }
    }

    void NavTileRebaker::waitForAllBakes( const vector<unique_ptr<NavMeshRuntime>>& listRuntime )
    {
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
        {
            pRuntime->waitBakes();
        }
    }
} // namespace sw
