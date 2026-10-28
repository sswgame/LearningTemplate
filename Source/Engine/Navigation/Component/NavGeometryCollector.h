/**
 * @file NavGeometryCollector.h
 * @brief 씬에서 내비메시 베이크 입력(삼각형 · 충돌 모양)을 모으고 베이크 경계를 정합니다.
 * @details 런타임 베이크(`SceneNavigation`)와 쿠킹(`SceneNavigationCooker`)이 같은 함수를 지나므로 두 길의 입력 해시가 같습니다.
 *          어떤 오브젝트를 넣고 뺄지(수정자 · 제외 태그 · 움직이는 계층)와 메시 · 강체를 읽는 법이 바뀔 때만 이 파일이 바뀝니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/AABB.h"

namespace sw
{
    struct NavMeshSettings;

    class GameObjectManager;
    class INavGeometrySource;
    class NavMeshGeometry;
    class NavMeshSurfaceComponent;

    /** @class NavGeometryCollector @brief 파일 머리말 참고. 상태가 없는 정적 함수 묶음입니다. */
    class SW_API NavGeometryCollector
    {
    public:
        /**
         * @brief 씬의 베이크 기하를 모읍니다 — 표면의 규칙(파일 `NavMeshSurfaceComponent.h` 머리말). 표면이 없으면 기본 규칙(메시 + Static 강체)입니다.
         * @details 기하를 대신 내는 소스가 오브젝트를 맡으면 그것만 씁니다. 여럿이 맡으면 @p listSource 의 첫 소스입니다(등록 순서).
         */
        static void collectGeometry( GameObjectManager& manager, const NavMeshSurfaceComponent* pSurface, const NavMeshSettings& settings,
                                     const vector<INavGeometrySource*>& listSource, NavMeshGeometry& outGeometry );
        /** @brief 베이크 경계 — 표면의 상자(있으면)와 기하 경계의 겹침입니다. */
        static AABB computeBakeBounds( const NavMeshSurfaceComponent* pSurface, const NavMeshGeometry& geometry );
        /** @brief 기하 경계를 표면 상자로 자릅니다. 둘이 겹치지 않으면 표면 상자입니다. */
        static AABB clipToSurfaceBox( const AABB& geometryBounds, const AABB& surfaceBox );
    };
} // namespace sw
