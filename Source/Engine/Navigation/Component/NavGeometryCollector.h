/**
 * @file NavGeometryCollector.h
 * @brief 씬에서 내비메시 베이크 입력(삼각형 · 충돌 모양)을 모으고 베이크 경계를 정합니다.
 * @details 런타임 베이크(`SceneNavigation`)와 쿠킹(`SceneNavigationCooker`)이 같은 함수를 지나므로 두 길의 입력 해시가 같습니다.
 *          어떤 오브젝트를 넣고 뺄지(수정자 · 제외 태그 · 움직이는 계층)와 메시 · 강체를 읽는 법이 바뀔 때만 이 파일이 바뀝니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"

namespace sw
{
    struct NavMeshSettings;

    class GameObjectManager;
    class INavGeometrySource;
    class NavMeshGeometry;
    class NavMeshSurfaceComponent;

    /** @brief 기하 모으기가 아직 읽는 중인 메시를 어떻게 하는가입니다(`NavGeometryCollector::collectGeometry`). */
    enum class NavMeshArrival : uint8
    {
        Wait = 0, ///< 그 자리에서 읽는다 — 결과를 곧바로 쓰는 쿠킹 · 도구(`SceneNavigation::ensureNavMesh` · `flushTileBakes`)
        Defer,    ///< 기다리지 않고 센다 — 부르는 쪽이 다음 갱신에 다시 모은다(런타임 갱신, 게임 스레드가 디스크를 기다리지 않는다)
    };

    /** @class NavGeometryCollector @brief 파일 머리말 참고. 상태가 없는 정적 함수 묶음입니다. */
    class SW_API NavGeometryCollector
    {
    public:
        /**
         * @brief 씬의 베이크 기하를 모읍니다 — 표면의 규칙(파일 `NavMeshSurfaceComponent.h` 머리말). 표면이 없으면 기본 규칙(메시 + Static 강체)입니다.
         * @details 기하를 대신 내는 소스가 오브젝트를 맡으면 그것만 씁니다. 여럿이 맡으면 @p listSource 의 첫 소스입니다(등록 순서).
         *          메시 컴포넌트의 메시를 아직 읽는 중이면 @p arrival 이 정한다 — `Wait` 는 그 자리에서 읽고, `Defer` 는 넣지 않고 센다(기하가 덜 모였다).
         *          읽지 못한 메시(로드 실패)는 두 방식 모두 넣지 않는다 — 그리지 않는 메시는 걷는 면도 아니다.
         * @return `Defer` 에서 아직 읽는 중이라 넣지 못한 메시 수입니다. 0 이 아니면 기하가 덜 모였으므로 부르는 쪽이 쓰지 않고 다음 갱신에 다시 모은다. `Wait` 는 늘 0 입니다.
         */
        [[nodiscard]] static uint32 collectGeometry( GameObjectManager& manager, const NavMeshSurfaceComponent* pSurface, const NavMeshSettings& settings,
                                                     const vector<INavGeometrySource*>& listSource, NavMeshArrival arrival, NavMeshGeometry& outGeometry );
        /** @brief 베이크 경계 — 표면의 상자(있으면)와 기하 경계의 겹침입니다. */
        static AABB computeBakeBounds( const NavMeshSurfaceComponent* pSurface, const NavMeshGeometry& geometry );
        /** @brief 기하 경계를 표면 상자로 자릅니다. 둘이 겹치지 않으면 표면 상자입니다. */
        static AABB clipToSurfaceBox( const AABB& geometryBounds, const AABB& surfaceBox );
    };
} // namespace sw
