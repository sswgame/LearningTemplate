/**
 * @file NavMeshRuntime.h
 * @brief 씬 내비게이션이 에이전트 종류마다 갖는 상태(`NavMeshRuntime`), 타일 하나를 워커에서 다시 베이크하는 일(`NavTileBakeJob`), 조각들이 함께 쓰는 판정(`NavMeshRuntime`)입니다.
 * @details `SceneNavigation` 과 그 조각(`NavTileRebaker` · `NavAgentCrowdSync` · `NavDebugView`)이 함께 읽는 내부 헤더입니다.
 *          공개 헤더 `SceneNavigation.h` 는 앞 선언만 쓰므로 이 헤더를 퍼뜨리지 않습니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Task/TaskManager.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavMeshSettings.h"
#include "Engine/Navigation/NavigationTypes.h"
#include "Engine/Navigation/PathRequestQueue.h"
#include "Engine/Navigation/Scene/NavPathSolver.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    class NavMeshSurfaceComponent;

    /**
     * @class NavMeshWriteAccess
     * @brief "지금 내비메시를 읽는 워커가 없다" 는 증표입니다 — 비동기 경로 질의를 수확했고(경로 줄의 `Fence`) 군중 진행이 끝났다. `SceneNavigation` 만 만듭니다.
     * @details 타일을 바꾸는 일(재베이크 결과 끼우기 · 장애물 캐시 짓기 · 링크 막기)이 이 증표를 인자로 받는다 — 진행 중 질의와 겹쳐 타일을 바꾸는 길은 컴파일되지 않는다.
     */
    class NavMeshWriteAccess
    {
        friend class SceneNavigation;

    private:
        explicit NavMeshWriteAccess( const PathRequestQueue<NavPathSolver>::Fence& /*fence*/ ) {}
    };
} // namespace sw

namespace sw
{
    /** @brief 타일 하나를 워커에서 다시 베이크하는 일입니다. 입력은 공유 사본이라 그 사이 게임 스레드가 새 입력을 만들어도 이 일은 옛 것을 끝까지 읽는다. */
    struct NavTileBakeJob
    {
        const INavMeshTileStore*                  _pNavMesh{ nullptr }; ///< 베이크 워커는 타일 쪽만 본다 — 교체는 const 가 아니라 부를 수 없다
        shared_ptr<const NavMeshGeometry>         _pGeometry;
        shared_ptr<const vector<NavConvexVolume>> _pVolume;
        NavTileData                               _result;
        TaskHandle                                _task; ///< 비어 있으면 인라인으로 이미 돌았다(`isCompleted()` 가 true). 기다림은 줄의 `_bakeStage` 로 한다.
        int32                                     _tileX{ 0 };
        int32                                     _tileZ{ 0 };
        bool                                      _bSucceeded{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

        void run()
        {
            SW_MEMORY_SCOPE( Navigation );
            _bSucceeded = _pNavMesh->bakeTile( *_pGeometry, _pVolume.get(), _tileX, _tileZ, _result );
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 스트리밍 씬에서 쿠킹본 타일 하나의 자리 · 구간 · 단계입니다(`SceneNavigation::updateTileStreaming`). 씬 셀과 같은 원점 · 반경으로 올리고 내린다.
     * @details 읽기는 IO 스레드가 하고 결과(`Read`)는 완료 콜백이 쥔 사본과 나눈다 — 취소하면 이쪽만 놓는다. 요청 핸들은 결과와 따로 든다(완료 콜백이 결과를 쥐어 고리가 되지 않게).
     */
    struct NavStreamTile
    {
        enum class State : uint8
        {
            Unloaded, ///< 내비메시에 없다
            Loading,  ///< 구간을 읽는 중이다
            Loaded,   ///< 끼웠다
            Failed,   ///< 읽기 · 끼우기가 실패했다 — 다시 시도하지 않는다(프레임마다 같은 오류를 내지 않게)
        };
        /** @brief 워커가 채우는 읽기 결과입니다. `_bytes` 를 채운 뒤 `_state` 를 release 로 올린다(0 읽는 중 · 1 읽음 · 2 실패). */
        struct Read
        {
            atomic<uint8> _state{ 0 };
            vector<uint8> _bytes;
        };
        int32                _tileX{ 0 };
        int32                _tileZ{ 0 };
        NavTileRange         _range{};
        const vector<uint8>* _pMemoryBytes{ nullptr }; ///< 메모리 쿠킹본(테스트 · 도구 — `SceneNavigation::setCookedAsset`)의 바이트. 있으면 읽지 않고 끼운다
        shared_ptr<Read>     _pRead;
        AsyncReadHandle      _readHandle;
        State                _state{ State::Unloaded };
    };

    class NavMeshObstacleComponent;

    /** @brief 장애물 캐시(`NavObstacleMode::TileCache`)에 넣은 장애물 하나 — 어느 컴포넌트의 것인지와 캐시 번호입니다. */
    struct NavCacheObstacleEntry
    {
        const NavMeshObstacleComponent* _pObstacle{ nullptr };
        NavObstacleRef                  _ref{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 에이전트 종류 하나의 내비메시 · 군중 · 입력 · 재베이크 줄입니다. */
    struct NavMeshRuntime
    {
        hashed_string                             _agentType{};
        const NavMeshSurfaceComponent*            _pSurface{ nullptr };
        unique_ptr<INavMesh>                      _pNavMesh;
        unique_ptr<INavCrowd>                     _pCrowd;
        shared_ptr<const NavMeshGeometry>         _pGeometry;
        shared_ptr<const vector<NavConvexVolume>> _pVolume;
        vector<int2>                              _listDirtyTile;
        vector<unique_ptr<NavTileBakeJob>>        _listJob;
        /** @brief 이 줄이 낸 베이크 태스크 전부입니다(일마다 스테이지를 따로 만들지 않는다 — 언리얼 FGraphEventArray 처럼 주인 하나가 묶는다). 게임 스레드만 넣는다. */
        TaskStageHandle                _bakeStage;
        vector<NavCacheObstacleEntry>  _listCacheObstacle; ///< 장애물 캐시에 든 장애물(캐시 모드에서만 — 다시 베이크 모드는 `_pVolume` 이다)
        NavQueryFilter                 _filter{};
        NavMeshBakeStats               _bakeStats{};
        AABB                           _bounds{};
        float3                         _searchExtent{ 1.0f, 2.0f, 1.0f };
        float32                        _crowdDeltaTime{ 0.0f }; ///< 워커에 넘긴 군중 진행의 시간(`SceneNavigation::launchCrowdUpdates` 가 넘기기 전에 적는다)
        vector<NavStreamTile>          _listStreamTile;         ///< 스트리밍 씬에서 쿠킹본의 타일들(비면 모든 타일이 늘 끼워져 있다)
        shared_ptr<const NavMeshAsset> _pStreamAsset;           ///< 타일 자리 · 구간을 준 쿠킹본(메모리 쿠킹본이면 타일 바이트도 여기 있다)
        string                         _streamPath;             ///< 타일을 구간으로 읽는 쿠킹본의 리소스 id
        uint8                          _bFromCooked : 1;

        NavMeshRuntime()
            : _bFromCooked{ SW_FALSE }
        {
        }
        NavMeshRuntime( const NavMeshRuntime& )            = delete;
        NavMeshRuntime& operator=( const NavMeshRuntime& ) = delete;
        ~NavMeshRuntime()
        {
            // 일이 이 내비메시 · 입력을 읽고 있다 — 끝날 때까지 놓지 않는다.
            waitBakes();
        }

        /** @brief 낸 베이크가 모두 끝날 때까지 기다립니다. 남은 일이 있을 때만 매니저를 부른다 — 인라인 경로 · 엔진 종료(`waitAll`) 뒤에는 매니저가 없어도 된다. */
        void waitBakes() const
        {
            for ( const unique_ptr<NavTileBakeJob>& pJob : _listJob )
            {
                if ( pJob->_task.isCompleted() )
                    continue;
                engine::getSubsystem<TaskManager>().waitStage( _bakeStage, "NavMeshRuntime" );
                return;
            }
        }

        /**
         * @brief 군중을 `_crowdDeltaTime` 만큼 진행합니다 — 군중 진행 태스크의 본문(워커)입니다. 군중 자신과 내비메시(읽기)만 만진다.
         * @details 진행하는 동안 게임 스레드는 이 군중을 만지지 않는다 — 군중을 만지는 길은 모두 `SceneNavigation::waitCrowdUpdates` 를 먼저 지난다.
         */
        void updateCrowd()
        {
            SW_MEMORY_SCOPE( Navigation );
            _pCrowd->update( _crowdDeltaTime );
        }

        /** @brief 장애물을 장애물 캐시로 다루면 true 입니다(덧칠 부피 없이 베이크한다). */
        bool usesObstacleCache() const { return _pNavMesh != nullptr && _pNavMesh->getObstacleMode() == NavObstacleMode::TileCache; }

        /** @brief 장애물 하나의 캐시 장애물을 뺍니다(없으면 할 일이 없다). */
        void removeCacheObstacle( const NavMeshObstacleComponent* pObstacle )
        {
            for ( size_t index = 0; index < _listCacheObstacle.size(); ++index )
            {
                if ( _listCacheObstacle[index]._pObstacle != pObstacle )
                    continue;
                _pNavMesh->removeCacheObstacle( _listCacheObstacle[index]._ref );
                _listCacheObstacle[index] = _listCacheObstacle.back();
                _listCacheObstacle.pop_back();
                return;
            }
        }

        /** @brief 타일을 원점 둘레로 올리고 내리는 중인데 이 타일이 지금 내려가 있으면 true 입니다 — 다시 베이크 결과를 끼우지 않는다(내린 구역에 타일이 되살아나지 않게). */
        bool isTileStreamedOut( const int2& tile ) const
        {
            for ( const NavStreamTile& streamTile : _listStreamTile )
            {
                if ( streamTile._tileX == tile._x && streamTile._tileZ == tile._y )
                    return streamTile._state != NavStreamTile::State::Loaded;
            }
            return false;
        }
        bool isTileInFlight( const int2& tile ) const
        {
            for ( const unique_ptr<NavTileBakeJob>& pJob : _listJob )
            {
                if ( pJob->_tileX == tile._x && pJob->_tileZ == tile._y )
                    return true;
            }
            return false;
        }

        /** @brief 타일을 재베이크 줄에 넣습니다. 이미 줄에 있으면 넣지 않습니다. */
        void addDirtyTile( const int2& tile )
        {
            for ( const int2& existing : _listDirtyTile )
            {
                if ( existing._x == tile._x && existing._y == tile._y )
                    return;
            }
            _listDirtyTile.push_back( tile );
        }

        // 씬 내비게이션 조각들이 같은 뜻으로 쓰는 작은 판정입니다.
        /** @brief 플레이를 시작했고 켜져 있으며 지워지는 중이 아니고 주인이 계층에서 켜진 컴포넌트면 true 입니다(갱신이 다루는 대상). */
        static bool isLive( const Component& component )
        {
            const GameObject* pOwner = component.getOwner();
            return component.hasBegunPlay() && component.isActive() && component.isPendingDestroy() == false && pOwner != nullptr &&
                   pOwner->isActiveInHierarchy();
        }
    };
} // namespace sw
