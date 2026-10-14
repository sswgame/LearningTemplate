#include "pch.h"

#include "Engine/Object/Component/Navigation/NavTileRebaker.h"

#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Object/Component/Navigation/NavGeometryCollector.h"
#include "Engine/Object/Component/Navigation/NavMeshObstacleComponent.h"
#include "Engine/Object/Component/Navigation/NavMeshRuntime.h"
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
        outListVolume.clear();
        for ( const NavMeshObstacleComponent* pObstacle : listObstacle )
        {
            NavConvexVolume volume;
            if ( pObstacle->_bCarved == SW_TRUE && pObstacle->makeVolume( volume ) )
                outListVolume.push_back( std::move( volume ) );
        }
    }

    void NavTileRebaker::updateObstacles( const vector<NavMeshObstacleComponent*>& listObstacle, float32 moveThreshold )
    {
        using Internal = NavTileRebakerInternal;
        for ( NavMeshObstacleComponent* pObstacle : listObstacle )
        {
            if ( SceneNavigationUtil::isLive( *pObstacle ) == false )
            {
                if ( pObstacle->_bCarved == SW_TRUE )
                {
                    invalidateArea( pObstacle->_carvedBounds, false );
                    pObstacle->_bCarved   = SW_FALSE;
                    _bObstacleVolumeDirty = true;
                }
                continue;
            }
            NavConvexVolume volume;
            if ( pObstacle->makeVolume( volume ) == false || volume._listPoint.size() < 2 )
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
                invalidateArea( pObstacle->_carvedBounds, false );
            invalidateArea( bounds, false );
            pObstacle->_carvedBounds   = bounds;
            pObstacle->_carvedPosition = center;
            pObstacle->_carvedYaw      = yaw;
            pObstacle->_bCarved        = SW_TRUE;
            pObstacle->_bShapeDirty    = SW_FALSE;
            _bObstacleVolumeDirty      = true;
        }
    }

    void NavTileRebaker::processDirtyAreas( const vector<unique_ptr<NavMeshRuntime>>& listRuntime, const vector<NavMeshObstacleComponent*>& listObstacle,
                                            GameObjectManager& manager, const NavMeshSettings& settings, const vector<INavGeometrySource*>& listSource )
    {
        vector<AABB> listArea;
        _dirtyLock.lock();
        listArea.swap( _listDirtyArea );
        const bool bGeometryDirty = _bGeometryDirty;
        _bGeometryDirty           = false;
        _dirtyLock.unlock();
        if ( listRuntime.empty() )
        {
            _bObstacleVolumeDirty = false;
            return;
        }
        if ( listArea.empty() && _bObstacleVolumeDirty == false )
            return;
        SW_PROFILE_SCOPE( "GT.Navigation.invalidate" );
        if ( _bObstacleVolumeDirty )
        {
            shared_ptr<vector<NavConvexVolume>> pVolume = make_shared<vector<NavConvexVolume>>();
            collectObstacleVolumes( listObstacle, *pVolume );
            for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
            {
                pRuntime->_pVolume = pVolume;
            }
            _bObstacleVolumeDirty = false;
        }
        if ( bGeometryDirty )
        {
            // 정적 기하가 바뀌었다(파괴 · 수정자) — 새 사본을 모은다. 베이크하는 중인 일은 옛 사본을 끝까지 읽는다.
            for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
            {
                shared_ptr<NavMeshGeometry> pGeometry = make_shared<NavMeshGeometry>();
                NavGeometryCollector::collectGeometry( manager, pRuntime->_pSurface, settings, listSource, *pGeometry );
                pGeometry->rebuildSpatialIndex( MathUtil::max( 1.0f, pRuntime->_pNavMesh->getTileGrid()._tileWorldSize * 0.25f ) );
                pRuntime->_pGeometry = std::move( pGeometry );
            }
        }
        vector<int2> listTile;
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
        {
            for ( const AABB& area : listArea )
            {
                pRuntime->_pNavMesh->collectTilesOverlapping( area, listTile );
                for ( const int2& tile : listTile )
                {
                    pRuntime->addDirtyTile( tile );
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
                    pRaw->_stage             = taskManager.createStage();
                    pRaw->_stage.addTask( pRaw->_task );
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
            for ( const unique_ptr<NavTileBakeJob>& pJob : pRuntime->_listJob )
            {
                pJob->waitDone();
            }
        }
    }
} // namespace sw
