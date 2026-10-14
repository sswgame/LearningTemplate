/**
 * @file NavMeshRuntime.h
 * @brief 씬 내비게이션이 에이전트 종류마다 갖는 상태(`NavMeshRuntime`), 타일 하나를 워커에서 다시 베이크하는 일(`NavTileBakeJob`), 조각들이 함께 쓰는 판정(`SceneNavigationUtil`)입니다.
 * @details `SceneNavigation` 과 그 조각(`NavTileRebaker` · `NavAgentCrowdSync` · `NavDebugView`)이 함께 읽는 내부 헤더입니다.
 *          공개 헤더 `SceneNavigation.h` 는 앞 선언만 쓰므로 이 헤더를 퍼뜨리지 않습니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/AABB.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"
#include "Core/Task/TaskManager.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/NavigationTypes.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    class NavMeshSurfaceComponent;

    /** @brief 씬 내비게이션 조각들이 같은 뜻으로 쓰는 작은 판정입니다. */
    struct SceneNavigationUtil
    {
        /** @brief 플레이를 시작했고 켜져 있으며 지워지는 중이 아니고 주인이 계층에서 켜진 컴포넌트면 true 입니다(갱신이 다루는 대상). */
        static bool isLive( const Component& component )
        {
            const GameObject* pOwner = component.getOwner();
            return component.hasBegunPlay() && component.isActive() && component.isPendingDestroy() == false && pOwner != nullptr &&
                   pOwner->isActiveInHierarchy();
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 타일 하나를 워커에서 다시 베이크하는 일입니다. 입력은 공유 사본이라 그 사이 게임 스레드가 새 입력을 만들어도 이 일은 옛 것을 끝까지 읽는다. */
    struct NavTileBakeJob
    {
        const INavMesh*                           _pNavMesh{ nullptr };
        shared_ptr<const NavMeshGeometry>         _pGeometry;
        shared_ptr<const vector<NavConvexVolume>> _pVolume;
        NavTileData                               _result;
        TaskHandle                                _task;  ///< 비어 있으면 인라인으로 이미 돌았다(`isCompleted()` 가 true).
        TaskStageHandle                           _stage; ///< `_task` 하나만 담는다 — 기다리는 동안 다른 일을 돕는다.
        int32                                     _tileX{ 0 };
        int32                                     _tileZ{ 0 };
        bool                                      _bSucceeded{ false };

        void run()
        {
            SW_MEMORY_SCOPE( Navigation );
            _bSucceeded = _pNavMesh->bakeTile( *_pGeometry, _pVolume.get(), _tileX, _tileZ, _result );
        }

        /** @brief 끝나지 않았을 때만 매니저를 부른다 — 인라인 경로 · 엔진 종료(`waitAll`) 뒤에는 매니저가 없어도 된다. */
        void waitDone() const
        {
            if ( _task.isCompleted() == false )
                engine::getSubsystem<TaskManager>().waitStage( _stage );
        }
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
        NavQueryFilter                            _filter{};
        NavMeshBakeStats                          _bakeStats{};
        AABB                                      _bounds{};
        float3                                    _searchExtent{ 1.0f, 2.0f, 1.0f };
        bool                                      _bFromCooked{ false };

        NavMeshRuntime()                                   = default;
        NavMeshRuntime( const NavMeshRuntime& )            = delete;
        NavMeshRuntime& operator=( const NavMeshRuntime& ) = delete;
        ~NavMeshRuntime()
        {
            // 일이 이 내비메시 · 입력을 읽고 있다 — 끝날 때까지 놓지 않는다.
            for ( const unique_ptr<NavTileBakeJob>& pJob : _listJob )
            {
                pJob->waitDone();
            }
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
    };
} // namespace sw
