/**
 * @file NavigationWatches.cpp
 * @brief 내비게이션의 자동화 감시 값(경로 · 에이전트 · 스트리밍 타일)입니다.
 * @details 기능 .cpp 끝에 있던 등록 블록을 그대로 옮겼다 — 배포 엔진에 컴파일되지 않게 개발 도구가 짓는다. 본문은 기능 형식의 공개 조회만 읽는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Navigation/Component/NavMeshLinkComponent.h"
#include "Engine/Navigation/Component/NavMeshObstacleComponent.h"
#include "Engine/Navigation/Component/NavMeshSurfaceComponent.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/Scene/NavAgentCrowdSync.h"
#include "Engine/Navigation/Scene/NavGeometryCollector.h"
#include "Engine/Navigation/Scene/NavMeshRuntime.h"
#include "Engine/Navigation/Scene/NavStreamRoute.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Subsystem/SceneSubsystemRegistry.h"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

// Engine/Navigation/Scene/SceneNavigation.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct SceneNavigationWatchesInternal
        {
            /**
             * @brief 질의 대상(시작)에서 `key` 이름의 오브젝트(끝)까지 첫 베이크된 에이전트 종류의 경로를 찾습니다. 못 찾으면 까닭을 적고 false 입니다.
             * @details 시나리오가 "이 장애물이 길을 막아 돌아가는지 · 이 링크로 건너는지" 를 값으로 본다(언리얼 Navigation Test Actor 의 자리).
             */
            static bool findQueryPath( const AutomationWatchQuery& query, AutomationWatchValue& outValue, NavPath& outPath )
            {
                const GameObject*     pStart  = query.findTargetObject();
                const GameObject*     pEnd    = query._pManager != nullptr ? query._pManager->findGameObjectByName( hashed_string{ query._key } ) : nullptr;
                const SceneComponent* pFrom   = pStart != nullptr ? pStart->getPrimarySceneComponent() : nullptr;
                const SceneComponent* pTo     = pEnd != nullptr ? pEnd->getPrimarySceneComponent() : nullptr;
                const bool            bNoEnds = pFrom == nullptr || pTo == nullptr;
                if ( bNoEnds )
                {
                    outValue.setText( pStart == nullptr ? query.getTargetProblem() : "no end object named by key (with a scene component)" );
                    return false;
                }
                const SceneNavigation& navigation = query._pManager->getSubsystem<SceneNavigation>();
                vector<hashed_string>  listAgentType;
                navigation.collectBakedAgentTypes( listAgentType );
                if ( listAgentType.empty() )
                {
                    outValue.setText( "no navmesh baked yet" );
                    return false;
                }
                // 대상이 내비 에이전트면 그 종류의 경로다(행위자 종류마다 영역 비용이 다르다 — 화염 정령은 용암을 지난다). 아니면 첫 베이크된 종류다.
                const NavMeshAgentComponent* pAgent    = pStart->getComponent<NavMeshAgentComponent>();
                const hashed_string          agentType = pAgent != nullptr ? pAgent->getAgentType() : listAgentType.front();
                if ( pAgent != nullptr && navigation.findNavMesh( agentType ) == nullptr )
                {
                    outValue.setText( "the target's agent type has no navmesh baked yet" );
                    return false;
                }
                // 동기 질의를 남긴다 — 감시 값은 읽는 자리에서 값을 내야 하고 단계 하나에 질의 하나다(대상 수에 비례하지 않는다).
                // 비동기 요청과 같은 계층 경로다 — 스트리밍 씬에서 오르지 않은 타일 너머 끝도 대략 그래프가 이으면 끝까지 가는 길이다.
                (void)navigation.findStreamingPath( agentType, pFrom->getWorldPosition(), pTo->getWorldPosition(), outPath );
                if ( outPath.isEndReachable() == false )
                {
                    outValue.setText( "no complete path" );
                    return false;
                }
                return true;
            }

            /** @brief 감시 값 `Nav.PathLength` 입니다. 대상에서 `key` 오브젝트까지 다듬은 경로의 길이(m)이고, 끝까지 가는 경로가 없으면 값이 없습니다. */
            [[nodiscard]] static bool readPathLength( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                NavPath path;
                if ( findQueryPath( query, outValue, path ) == false )
                    return false;
                outValue.setNumber( static_cast<float64>( path.computeRouteLength() ) );
                return true;
            }

            /** @brief 감시 값 `Nav.AreaCost` 입니다. key `<에이전트 종류>:<영역>` 의 비용 배율이고, 통행 금지면 -1 입니다(지역 프로필의 내비 비용 모듈이 정한 값). */
            [[nodiscard]] static bool readAreaCost( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const size_t colon = query._key.find( ':' );
                if ( query._pManager == nullptr || colon == string_view::npos )
                {
                    outValue.setText( "key must be <agent type>:<area>" );
                    return false;
                }
                float32             cost     = 0.0f;
                bool                bAllowed = false;
                const hashed_string agentType{ query._key.substr( 0, colon ) };
                const hashed_string area{ query._key.substr( colon + 1 ) };
                if ( query._pManager->getSubsystem<SceneNavigation>().findAgentAreaCost( agentType, area, cost, bAllowed ) == false )
                {
                    outValue.setText( "navigation settings not read yet, or unknown agent type or area" );
                    return false;
                }
                outValue.setNumber( bAllowed ? static_cast<float64>( cost ) : -1.0 );
                return true;
            }

            /** @brief 감시 값 `Nav.CompletedPathRequests` — 수확한 비동기 경로 요청 수(누적)입니다. */
            [[nodiscard]] static bool readCompletedPathRequests( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<SceneNavigation>().getCompletedPathRequestCount() );
                return true;
            }

            /** @brief 감시 값 `Nav.AgentCount` — 씬에 등록된 내비메시 에이전트 수입니다. */
            [[nodiscard]] static bool readAgentCount( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<SceneNavigation>().getAgentCount() );
                return true;
            }

            /** @brief 감시 값 `Nav.AgentStatus` — 대상의 내비메시 에이전트 이동 상태 글(`Idle` · `Moving` · `Arrived` · `Failed`)입니다. 에이전트가 없으면 값이 없습니다. */
            [[nodiscard]] static bool readAgentStatus( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const NavMeshAgentComponent* pAgent = query.findTargetComponent<NavMeshAgentComponent>( "navmesh agent", outValue );
                if ( pAgent == nullptr )
                    return false;
                switch ( pAgent->getMoveStatus() )
                {
                    case NavMoveStatus::Idle:
                    {
                        outValue.setText( "Idle" );
                        break;
                    }
                    case NavMoveStatus::Moving:
                    {
                        outValue.setText( "Moving" );
                        break;
                    }
                    case NavMoveStatus::Arrived:
                    {
                        outValue.setText( "Arrived" );
                        break;
                    }
                    case NavMoveStatus::Failed:
                    {
                        outValue.setText( "Failed" );
                        break;
                    }
                }
                return true;
            }

            /** @brief 마련한 종류 모두의 스트리밍 길찾기 상태에서 @p member 를 더합니다. */
            template <typename TValue>
            [[nodiscard]] static bool sumStreamStats( const GameObjectManager* pManager, TValue NavStreamStats::* member, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                vector<NavStreamStats> listStats;
                pManager->getSubsystem<SceneNavigation>().collectStreamStats( listStats );
                float64 sum = 0.0;
                for ( const NavStreamStats& stats : listStats )
                {
                    sum += static_cast<float64>( stats.*member );
                }
                outValue = sum;
                return true;
            }

            /** @brief 감시 값 `Nav.RouteReplanCount` — 먼 경로(중간 목표 · 가상 이동)를 새로 정한 수(누적, 모든 종류)입니다. */
            [[nodiscard]] static bool readRouteReplanCount( const GameObjectManager* pManager, float64& outValue ) { return sumStreamStats( pManager, &NavStreamStats::_replanCount, outValue ); }
            /** @brief 감시 값 `Nav.StreamLoadedTiles` — 끼운 스트리밍 타일(내비 데이터 청크) 수입니다. */
            [[nodiscard]] static bool readStreamLoadedTiles( const GameObjectManager* pManager, float64& outValue ) { return sumStreamStats( pManager, &NavStreamStats::_loadedTileCount, outValue ); }
            /** @brief 감시 값 `Nav.VirtualAgents` — 내려간 타일 위를 가상 이동하는 에이전트 수입니다. */
            [[nodiscard]] static bool readVirtualAgents( const GameObjectManager* pManager, float64& outValue ) { return sumStreamStats( pManager, &NavStreamStats::_virtualAgentCount, outValue ); }

            /** @brief 감시 값 `Nav.PathLinkCount` 입니다. 그 경로가 건너는 오프 메시 링크 수입니다. */
            [[nodiscard]] static bool readPathLinkCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                NavPath path;
                if ( findQueryPath( query, outValue, path ) == false )
                    return false;
                outValue.setNumber( static_cast<float64>( path._listLinkCrossing.size() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( navCompletedPathRequests, "Nav.CompletedPathRequests", "Async navmesh path requests harvested so far (cancelled ones are not counted)",
                         &SceneNavigationWatchesInternal::readCompletedPathRequests );
    SW_AUTOMATION_WATCH( navAgentCount, "Nav.AgentCount", "NavMesh agents registered in the scene", &SceneNavigationWatchesInternal::readAgentCount );
    SW_AUTOMATION_QUERY_WATCH( navAgentStatus, "Nav.AgentStatus", "Move status of the target's navmesh agent: Idle, Moving, Arrived or Failed", &SceneNavigationWatchesInternal::readAgentStatus, true, false );
    SW_AUTOMATION_QUERY_WATCH( navAreaCost, "Nav.AreaCost", "Path cost multiplier of a navigation area for an agent type (key = <agent type>:<area>; empty agent = default); -1 when the area is closed to it",
                               &SceneNavigationWatchesInternal::readAreaCost, false, true );
    SW_AUTOMATION_QUERY_WATCH( navPathLength, "Nav.PathLength", "Length (m) of the navmesh path from the target to the object named by key (coarse points past streamed-out tiles included); absent without one reaching it",
                               &SceneNavigationWatchesInternal::readPathLength, true, true );
    SW_AUTOMATION_WATCH( navRouteReplanCount, "Nav.RouteReplanCount", "Long routes over streamed-out tiles planned so far (intermediate targets and virtual moves, all agent types)",
                         &SceneNavigationWatchesInternal::readRouteReplanCount );
    SW_AUTOMATION_WATCH( navStreamLoadedTiles, "Nav.StreamLoadedTiles", "Cooked navmesh tiles (navigation data chunks) streamed in around the streaming sources", &SceneNavigationWatchesInternal::readStreamLoadedTiles );
    SW_AUTOMATION_WATCH( navVirtualAgents, "Nav.VirtualAgents", "Navmesh agents moving virtually over streamed-out tiles along the coarse cluster graph", &SceneNavigationWatchesInternal::readVirtualAgents );
    SW_AUTOMATION_QUERY_WATCH( navPathLinkCount, "Nav.PathLinkCount", "Off-mesh links the fine part of the navmesh path from the target to the object named by key crosses (links past streamed-out tiles are not counted)",
                               &SceneNavigationWatchesInternal::readPathLinkCount, true, true );
} // namespace sw
