/**
 * @file EditorPlacementDrag.h
 * @brief 씬 뷰 애셋 놓기의 Model 쪽입니다 — 끄는 동안 표면을 찾아 고스트 값(`EditorPlacementGhost`)을 내고, 놓으면 표면 위에 만들거나 놓을 수 없는 종류는 엽니다. ImGui 를 모릅니다.
 * @details View(`EditorViewportAssetPlacement`)는 끌기 페이로드 · 회전 키 · 고스트 그리기만 한다. 표면 찾기 · 스냅 계산은 `EditorAssetPlacement`.
 *          놓기는 명령 하나(`EditorAssetCommands::placeInViewport`)라 되돌리기 하나로 남는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Ray.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Physics/IPhysicsSceneFwd.h"

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

    /** @brief 이번 프레임의 놓기 판정입니다(고스트가 그린다). */
    struct EditorPlacementGhost
    {
        EditorPlacementHit _hit{};
        const utf8*        _pSurfaceName{ "" }; ///< 표면 종류의 영어 이름(고스트 글 — `EditorAssetPlacement::getSurfaceName`)
        bool               _bPlaceable{ false };
        bool               _bFound{ false };
    };

    /** @brief 놓기가 한 일입니다. */
    enum class EditorPlacementDropResult : uint8
    {
        Blocked, ///< 플레이 중이라 거절했다(이유는 경고 로그)
        Opened,  ///< 놓을 수 없는 종류라 열었다
        Refused, ///< 표면이 없어 놓지 않았다(이유는 경고 로그)
        Placed,  ///< 표면 위에 만들었다
    };

    /** @brief 배치가 마지막으로 한 일입니다. 감시 값(`Editor.Placement*`)이 읽는다. */
    struct EditorPlacementStats
    {
        optional<int32>        _ghostFrame;                              ///< 고스트를 마지막으로 그린 ImGui 프레임. 그린 적 없으면 비어 있다
        EditorPlacementSurface _surface{ EditorPlacementSurface::None }; ///< 마지막 고스트 · 놓기가 찾은 표면
        float3                 _lastPosition{};                          ///< 마지막으로 놓은 자리
        float32                _yaw{ 0.0f };                             ///< 끄는 동안 고른 회전(라디안). 끌기가 끝나면 0 으로 돌아간다
        float32                _lastYaw{ 0.0f };                         ///< 마지막으로 놓은 회전(라디안)
        uint32                 _placedCount{ 0 };                        ///< 표면 위에 만든 수
        uint32                 _refusedCount{ 0 };                       ///< 표면이 없어 놓지 않은 수
        uint32                 _openedCount{ 0 };                        ///< 놓을 수 없는 종류라 연 수
    };

    /**
     * @class EditorPlacementDrag
     * @brief 끌기 한 번의 놓기입니다. 끄는 동안만 정적 물리 사본을 들고(`ScenePhysics::createStaticScene3D`), 끌기가 끝나면 버린다.
     * @details 편집 중에는 플레이 물리 씬이 없다 — 사본은 끌기가 시작된 뒤 처음 캔버스 위에 왔을 때 한 번 만든다(라이트 프로브 베이크와 같은 길). 표면이 없으면 놓지 않는다(예전에는 원점에 놓았다).
     */
    class SW_EDITOR_API EditorPlacementDrag
    {
    public:
        EditorPlacementDrag();
        ~EditorPlacementDrag();
        EditorPlacementDrag( const EditorPlacementDrag& )            = delete;
        EditorPlacementDrag& operator=( const EditorPlacementDrag& ) = delete;

        /** @brief 끌기가 끝났다 — 사본을 버린다(다음 끌기는 그 사이의 편집을 담아 새로 만든다). */
        void end();
        /**
         * @brief @p assetPath 를 @p pRay(커서 레이, 캔버스 밖이면 nullptr) 아래에 놓을 자리를 찾습니다. 활성 씬이 없으면 false 입니다.
         * @param snapStep 0 보다 크면 바닥 두 축을 이 간격에 맞춘다.
         */
        [[nodiscard]] bool query( const string& assetPath, const Ray* pRay, bool b2DMode, float32 snapStep, EditorPlacementGhost& outGhost );
        /** @brief 놓였다 — 놓을 수 없는 종류는 열고, 플레이 중이면 거절하고, 표면이 있으면 @p yaw 로 돌려 만든다. */
        EditorPlacementDropResult drop( const string& assetPath, const EditorPlacementGhost& ghost, float32 yaw, bool b2DMode );
        /** @brief 놓기 결과를 통계에 적습니다(ImGui · 씬 없이 테스트한다). */
        static void recordDrop( EditorPlacementDropResult result, const EditorPlacementGhost& ghost, EditorPlacementStats& inoutStats );
        /**
         * @brief 놓기 전 회전 @p yaw 를 @p direction(+1 · -1) 쪽으로 @p step 만큼 돌린 값입니다(라디안, 0 ~ 2π 로 감는다). @p step 이 0 이하면 그대로입니다.
         * @details 걸음은 0 에서 센다 — 키를 네 번 누르면(직각 걸음) 처음 자세로 돌아온다. 감는 값이 2π 근처에서 떨리지 않게 걸음의 배수로 맞춘다.
         */
        [[nodiscard]] static float32 stepYaw( float32 yaw, float32 step, int32 direction );

    private:
        /** @brief 정적 물리 사본을 만듭니다(3D 만). 매니저가 바뀌었으면 새로 만든다. */
        void prepare( GameObjectManager* pManager, bool b2DMode );

    private:
        unique_ptr<IPhysicsScene3D> _pDragPhysics;  ///< 끄는 동안만 사는 정적 물리 사본(3D)
        const GameObjectManager*    _pDragManager;  ///< 사본을 만든 매니저. 씬이 바뀌면 다시 만든다
        bool                        _bDragPrepared; ///< NOLINT(sw-bool-flag): 이번 끌기에 사본을 이미 만들었다(물리 서비스가 없어 비었어도 다시 시도하지 않는다)
    };
} // namespace sw::editor
