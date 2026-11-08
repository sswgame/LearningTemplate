/**
 * @file EditorAssetPlacement.h
 * @brief 씬 뷰에 애셋을 놓을 자리(표면 피킹 · 격자 스냅)를 구합니다. ImGui 에 의존하지 않아 `Viewport/Model/Tests` 가 화면 없이 본다.
 * @details 언리얼 뷰포트 배치(커서 아래 표면에 놓고 격자 스냅을 따른다)와 같은 자리다. 표면을 찾지 못하면 놓지 않는다 — 예전에는 원점에 놓았다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Ray.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Physics/IPhysicsQueryScene.h"

namespace sw
{
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 놓을 자리를 준 표면의 종류입니다. 고스트 글과 감시 값(`Editor.PlacementSurface`)이 같은 번호를 쓴다. */
    enum class EditorPlacementSurface : uint8
    {
        None,        ///< 표면을 찾지 못했다 — 놓지 않는다
        Collider,    ///< 정적 콜라이더 · 지형(편집 중 정적 물리 사본의 레이캐스트)
        Bounds,      ///< 콜라이더가 없는 오브젝트의 피킹 경계(메시 · 스프라이트 — `EditorViewportRayPick`)
        GroundPlane, ///< 바닥 평면(3D 는 Y = 0, 2D 는 Z = 0)
    };

    /** @brief 놓을 자리 하나입니다. */
    struct EditorPlacementHit
    {
        float3                 _position{};
        float3                 _normal{ 0.0f, 1.0f, 0.0f };
        float32                _distance{ 0.0f }; ///< 레이 원점에서의 거리
        EditorPlacementSurface _surface{ EditorPlacementSurface::None };
    };

    /** @brief 표면을 찾는 데 쓰는 입력입니다. 물리 사본이 없으면(2D · 물리 서비스 없음) 콜라이더 표면은 건너뛴다. */
    struct EditorPlacementQuery
    {
        const IPhysicsQueryScene3D* _pPhysics{ nullptr };
        const GameObjectManager*    _pManager{ nullptr };
        float32                     _maxDistance{ 0.0f }; ///< 이보다 먼 표면은 없는 것으로 본다(지평선 근처의 바닥 평면)
        float32                     _snapStep{ 0.0f };    ///< 0 보다 크면 바닥 두 축을 이 간격에 맞춘다(높이는 표면 그대로)
        uint8                       _b2DMode  : 1;        ///< 2D 는 Z = 0 평면만 본다(스프라이트는 평면에 놓인다)
        [[maybe_unused]] uint8      _reserved : 7;

        EditorPlacementQuery();
    };

    /**
     * @class EditorAssetPlacement
     * @brief 표면 찾기와 스냅입니다. 상태가 없어 정적 함수 모음입니다.
     * @details 3D 는 정적 콜라이더 · 오브젝트 경계 중 가까운 것, 둘 다 없으면 앞쪽 바닥 평면이다. 카메라 뒤의 평면 교점은 버린다(화면에 보이지 않는 자리에 놓지 않는다).
     *          2D 는 Z = 0 평면 하나다(직교 카메라라 뒤 교점도 화면에 있다).
     */
    class EditorAssetPlacement
    {
    public:
        /** @brief @p ray 아래의 놓을 자리를 찾습니다. 찾지 못하면 false 이고 @p outHit 의 표면은 `None` 입니다. */
        [[nodiscard]] static bool findSurface( const EditorPlacementQuery& query, const Ray& ray, EditorPlacementHit& outHit );
        /** @brief 바닥 두 축(3D 는 X · Z, 2D 는 X · Y)을 @p step 간격에 맞춥니다. 높이 축은 그대로 둡니다. @p step 이 0 이하면 그대로입니다. */
        [[nodiscard]] static float3 snapToGrid( const float3& position, float32 step, bool b2DMode );
        /** @brief 표면 종류의 영어 이름입니다(로그 · 고스트 글). */
        [[nodiscard]] static const utf8* getSurfaceName( EditorPlacementSurface surface );
        /**
         * @brief 놓기 전 회전 @p yaw 를 @p direction(+1 · -1) 쪽으로 @p step 만큼 돌린 값입니다(라디안, 0 ~ 2π 로 감는다). @p step 이 0 이하면 그대로입니다.
         * @details 걸음은 0 에서 센다 — 키를 네 번 누르면(직각 걸음) 처음 자세로 돌아온다. 감는 값이 2π 근처에서 떨리지 않게 걸음의 배수로 맞춘다.
         */
        [[nodiscard]] static float32 stepYaw( float32 yaw, float32 step, int32 direction );
    };
} // namespace sw::editor
