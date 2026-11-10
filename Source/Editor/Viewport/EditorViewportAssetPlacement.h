/**
 * @file EditorViewportAssetPlacement.h
 * @brief 씬 뷰 캔버스의 애셋 배치입니다 — 끄는 동안 고스트(놓일 자리 · 표면 · 거절 이유), 놓으면 표면 위에 만들기, 놓을 수 없는 종류는 열기.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Editor/Viewport/Model/EditorAssetPlacement.h"

#include "Engine/Physics/IPhysicsScene.h"

struct ImDrawList;

namespace sw
{
    struct float4x4;

    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    struct ViewportToolbarSettings;

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
     * @class EditorViewportAssetPlacement
     * @brief 씬 뷰에 끌어 온 애셋을 표면 위에 놓습니다. 끄는 동안만 정적 물리 사본을 들고(`ScenePhysics::createStaticScene3D`), 끌기가 끝나면 버린다.
     * @details 편집 중에는 플레이 물리 씬이 없다 — 사본은 끌기가 시작될 때 한 번 만든다(라이트 프로브 베이크와 같은 길). 표면 찾기 · 스냅은 `EditorAssetPlacement`.
     *          표면이 없으면 놓지 않고 이유를 로그에 남긴다(예전에는 원점에 놓았다). 놓을 수 없는 종류(씬 · 머티리얼 …)는 고스트가 "놓으면 연다" 고 알리고 놓을 때 연다.
     *          Play 중에는 배치를 거절한다(`EditorPlaySession::refuseSceneEdit`). 놓을 자리 강조는 이 캔버스 하나뿐이고 미리보기 씬 선택을 건드리지 않는다.
     *          끄는 동안 회전 키로 위 축 둘레를 돌려 놓을 수 있다(하우징 · 건설 게임의 놓기 전 회전 — 언리얼 · 유니티는 놓은 뒤 기즈모로 돌린다). 고스트가 앞쪽 화살표와 각도를 보인다.
     */
    class EditorViewportAssetPlacement
    {
    public:
        EditorViewportAssetPlacement();
        ~EditorViewportAssetPlacement();
        EditorViewportAssetPlacement( const EditorViewportAssetPlacement& )            = delete;
        EditorViewportAssetPlacement& operator=( const EditorViewportAssetPlacement& ) = delete;

        /**
         * @brief 이번 프레임의 끌기를 처리합니다. 캔버스 위로 끌려 온 애셋은 고스트를 그리고, 놓였으면 만들거나 엽니다. 캔버스 이미지를 그린 창 안에서 부른다.
         * @param viewProj 캔버스 이미지를 그린 카메라의 뷰-투영 행렬입니다.
         */
        void process( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float4x4& viewProj, const ViewportToolbarSettings& settings );
        /** @brief 마지막으로 한 일입니다. */
        const EditorPlacementStats& getStats() const { return _stats; }

    private:
        /** @brief 끌기가 시작된 뒤 처음 캔버스 위에 왔을 때 정적 물리 사본을 만듭니다(3D 만). 매니저가 바뀌었으면 새로 만든다. */
        void prepareDragPhysics( GameObjectManager* pManager, bool b2DMode );
        /** @brief 놓일 자리 고리 · 법선 · 글을 그립니다. */
        void drawGhost( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize, const float4x4& viewProj, const string& assetPath, bool bPlaceable,
                        bool bFound, const EditorPlacementHit& hit ) const;
        /** @brief 회전 키(입력 맵 `viewport.placementRotate` · `viewport.placementRotateBack`)를 읽어 끄는 동안의 회전(`_stats._yaw`)을 돌립니다. */
        void updateDragYaw();

    private:
        unique_ptr<IPhysicsScene3D> _pDragPhysics; ///< 끄는 동안만 사는 정적 물리 사본(3D). 끌기가 끝나면 비운다
        const GameObjectManager*    _pDragManager; ///< 사본을 만든 매니저. 씬이 바뀌면 다시 만든다
        EditorPlacementStats        _stats;
        uint8                       _bDragPrepared : 1; ///< 이번 끌기에 사본을 이미 만들었다(물리 서비스가 없어 비었어도 다시 시도하지 않는다)
        [[maybe_unused]] uint8      _reserved      : 7;
    };
} // namespace sw::editor
