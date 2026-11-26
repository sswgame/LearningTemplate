/**
 * @file SceneViewQuery.h
 * @brief 게임 코드가 카메라 하나에 묻는 질의(점 · 경계가 절두체 안인가, 화면 크기, 월드 ↔ 화면 좌표)의 정본입니다.
 * @details 언리얼 `FSceneView`(`ViewFrustum` · `ProjectWorldToScreen` · `DeprojectScreenToWorld`)와 유니티 `GeometryUtility.TestPlanesAABB` ·
 *          `Camera.WorldToScreenPoint` · `Camera.ScreenPointToRay` 자리다. 절두체와 화면 크기 식은 렌더러 · LOD 와 같은 `LODView`(→ `Frustum`)를 그대로 쓴다.
 *          여기 있는 판정은 기하만 본다(`InFrustum`) — 가림이나 렌더러가 실제로 그렸는지(언리얼 `WasRecentlyRendered`, 유니티 `Renderer.isVisible`)는 보지 않는다.
 *          그래픽스 층이라 오브젝트를 모른다 — 오브젝트의 렌더 경계로 묻는 것은 `CameraComponent::isObjectInFrustum` 이다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/AABB.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/View/LODView.h"

namespace sw
{
    struct Ray;

    /**
     * @struct SceneViewQuery
     * @brief 뷰-투영 하나로 만든 카메라 질의입니다. `make` 로 만들고(역행렬을 한 번 구해 둔다) 같은 프레임 안에서 여러 번 묻습니다.
     * @details 화면 좌표는 UI 와 같은 규약이다 — 왼쪽 위가 (0, 0), 오른쪽 아래가 `screenSize`. 크기에 (1, 1) 을 넘기면 뷰포트 비율(유니티 `WorldToViewportPoint` 의 y 뒤집음)이다.
     */
    struct SW_API SceneViewQuery
    {
        LODView  _view{};            ///< 뷰-투영 · 눈 위치 · 절두체(렌더러 · LOD 와 같은 평면)
        float4x4 _inverseViewProj{}; ///< 화면 → 광선용 역행렬

        /** @brief 뷰-투영과 눈 위치로 만듭니다. */
        static SceneViewQuery make( const float4x4& viewProj, const float3& eyePosition );

        /** @brief 점이 절두체 안(경계 포함)이면 true 입니다. 가림은 보지 않습니다. */
        bool isPointInFrustum( const float3& point ) const { return _view._frustum.overlapsSphere( point, 0.0f ); }
        /** @brief 구가 절두체에 걸치면 true 입니다(GPU 컬링 `isVisible` 과 같은 식). 가림은 보지 않습니다. */
        bool isSphereInFrustum( const float3& center, float32 radius ) const { return _view._frustum.overlapsSphere( center, radius ); }
        /** @brief 월드 상자가 절두체에 걸치면 true 입니다(GPU 컬링 `isBoxVisible` 과 같은 식). 가림은 보지 않습니다. */
        bool isBoxInFrustum( const AABB& box ) const { return _view._frustum.overlapsBox( box._min, box._max ); }
        /** @brief 경계 구의 화면 크기(지름 / 화면 높이)입니다. 절두체 밖이면 0 — LOD · 추가 뷰 보임과 같은 식(`LODView::computeScreenSize`)입니다. */
        float32 computeScreenSize( const float3& center, float32 radius ) const { return LODView::computeScreenSize( _view, center, radius ); }

        /** @brief 월드 점의 화면 좌표입니다. 카메라 뒤(클립 w ≤ 0)면 false 입니다. 화면 밖 점은 true 이고 좌표가 [0, 크기] 밖입니다. */
        [[nodiscard]] bool worldToScreen( const float3& world, const float2& screenSize, float2& outScreen ) const
        {
            return projectWorldToScreen( _view._viewProj, world, screenSize, outScreen );
        }
        /** @brief 화면 점을 지나는 광선입니다(시작점은 가까운 면 위, 방향은 단위). 화면 크기가 비었거나 역투영이 퇴화하면 false 입니다. */
        [[nodiscard]] bool screenToWorldRay( const float2& screenPoint, const float2& screenSize, Ray& outRay ) const
        {
            return unprojectScreenToRay( _inverseViewProj, screenPoint, screenSize, outRay );
        }

        /** @brief `worldToScreen` 의 몸통입니다(행렬만 가진 쪽 — 마커 · 디버그 그리기). */
        [[nodiscard]] static bool projectWorldToScreen( const float4x4& viewProj, const float3& world, const float2& screenSize, float2& outScreen );
        /** @brief `screenToWorldRay` 의 몸통입니다(역행렬만 가진 쪽 — 월드 UI 포인터 · 에디터 피킹). 깊이 0(가까운 면) → 1(먼 면)을 잇습니다. */
        [[nodiscard]] static bool unprojectScreenToRay( const float4x4& inverseViewProj, const float2& screenPoint, const float2& screenSize, Ray& outRay );

        /** @brief 선분을 자를 때 근평면으로 보는 클립 w 입니다. 이보다 작은 점은 카메라 뒤입니다. */
        static constexpr float32 kSegmentNearW = 0.001f;
        /**
         * @brief 월드 선분을 근평면(`kSegmentNearW`)에서 잘라 화면 선분으로 옮깁니다. 점마다 투영하면 한쪽 끝이 카메라 뒤인 선분(카메라를 가로지르는 격자선)을 통째로 버립니다.
         * @param screenOrigin 화면(캔버스) 사각형의 왼쪽 위입니다. 화면 전체면 아래 오버로드를 씁니다.
         * @param screenSize 화면(캔버스) 사각형의 크기입니다. 왼쪽 위가 원점이고 아래로 +y 입니다.
         * @return 선분 전체가 카메라 뒤면 false 입니다.
         */
        [[nodiscard]] static bool projectSegment( const float4x4& viewProj, const float3& worldA, const float3& worldB, const float2& screenOrigin, const float2& screenSize,
                                                  float2& outScreenA, float2& outScreenB );
        /** @brief `projectSegment` 를 원점 (0, 0) 의 화면에 합니다. */
        [[nodiscard]] static bool projectSegment( const float4x4& viewProj, const float3& worldA, const float3& worldB, const float2& screenSize, float2& outScreenA, float2& outScreenB );
        /**
         * @brief 월드 상자가 화면에서 차지하는 사각형입니다. 열두 모서리를 `projectSegment` 로 근평면에서 잘라 옮긴 끝점들을 감쌉니다.
         * @details 꼭짓점만 투영하면 카메라 뒤로 넘어간 꼭짓점을 버려 카메라를 품거나 걸친 큰 물체의 사각형이 작게 잡힙니다(상자 선택이 놓친다).
         * @return 상자 전체가 카메라 뒤면 false 입니다.
         */
        [[nodiscard]] static bool projectBoxRect( const float4x4& viewProj, const AABB& box, const float2& screenOrigin, const float2& screenSize, float2& outMin, float2& outMax );
    };
} // namespace sw
