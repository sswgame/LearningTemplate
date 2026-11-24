#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Viewport/Model/EditorVisualizerToggles.h"

#include "Engine/Graphics/View/RenderViewMode.h"

namespace sw::editor
{
    /**
     * @struct ViewportToolbarState
     * @brief 씬 뷰 오버레이 바가 이번 프레임에 보일 편집 상태 값입니다 — 카메라 북마크 칸, 씬 뷰 보기 모드, 고른 오브젝트 수.
     * @details 메인 스레드가 작업 공간 · 선택 · 렌더러를 읽어 채우고(`gather`, Model), 바(View)는 이 값만 본다. 바꾸는 것은 설정의 요청 칸(`_requestedBookmarkSlot` ·
     *          `_requestedViewMode`)에 적고 뷰포트 클라이언트가 실행한다.
     */
    struct SW_EDITOR_API ViewportToolbarState
    {
        /** @brief 북마크 칸 하나입니다(`CameraBookmark` 에서 표시에 쓰는 값만). */
        struct BookmarkRow
        {
            string _name;
            float3 _position{};
            bool   _bValid{ false };
        };

        /** @brief 북마크 칸 수(Ctrl+1 … 9)입니다. */
        static constexpr uint32 kBookmarkSlotCount = 9;

        BookmarkRow              _arrBookmark[kBookmarkSlotCount];
        optional<RenderViewMode> _sceneViewMode;            ///< 씬 뷰 보기 모드(렌더러 상태가 원본). 렌더러가 없는 호스트면 비어 있다
        uint32                   _selectedObjectCount{ 0 }; ///< 메인 씬 선택의 오브젝트 수(트랜스폼 바 · 정렬 단추)

        /** @brief 작업 공간의 북마크 · 렌더러의 보기 모드 · 메인 선택 수를 @p outState 에 채웁니다(`Model/ViewportToolbarState.cpp`). */
        static void gather( ViewportToolbarState& outState );
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 뷰포트 툴바 설정 데이터 */
    struct ViewportToolbarSettings
    {
        float32 _gridSnapValue{ 1.0f };
        float32 _rotationSnapValue{ 15.0f };
        float32 _scaleSnapValue{ 0.1f };
        float32 _cameraSpeed{ 5.0f };
        /** @brief 컴포넌트 시각화의 켬/끔입니다. 등록 줄의 기본값 위에 바꾼 것만 id 로 보관합니다. */
        EditorVisualizerToggles  _visualizerToggles;
        optional<uint32>         _requestedBookmarkSlot; ///< 툴바가 부른 북마크(카메라가 불러오면 비운다)
        optional<RenderViewMode> _requestedViewMode;     ///< 툴바가 고른 씬 뷰 보기 모드(`applyViewModeRequest` 가 렌더러에 쓰고 비운다)
        // 뷰 모드(Lit/Unlit/Wireframe)는 여기 없다. 원본은 `FrameRenderer` 다. 같은 값을 두 곳에
        // 두면 반드시 어긋난다(커맨드라인 `-gv_viewMode` 나 코드가 바꾸면 툴바가 틀린 값을 보인다).
        // 툴바는 프레임마다 렌더러에서 읽어 표시하고, 고르면 렌더러에 쓴다.
        bool _bGridSnap{ false };
        bool _bRotationSnap{ false };
        bool _bScaleSnap{ false };
        bool _bShowHUD{ false }; ///< 씬 뷰의 HUD 상자(`EditorDebugHUDView`)
        bool _bShowGrid{ true };
        bool _bShowOrientationCube{ true };
        bool _bShowRuler{ false };
        bool _bIs2DMode{ false };
        bool _bSurfaceSnap{ false };

        // 씬 뷰 오버레이 바들의 내용입니다(보기 · 표시 · 도구 · 트랜스폼). 바 창 · 위치는 `EditorViewportOverlays` 가 정합니다.
        // 바가 왼쪽 · 오른쪽에 붙으면 @p bVertical 이 참이고 항목을 세로로 쌓는다.
        /** @brief 보기 바: 보기 모드 · 2D/3D · 카메라 속도. */
        static void drawViewBar( ViewportToolbarSettings& settings, bool bVertical, const ViewportToolbarState& state );
        /** @brief 표시 바: 통계 · 격자 · 방향 큐브 · 시각화 토글 · 표면 스냅. */
        static void drawDisplayBar( ViewportToolbarSettings& settings, bool bVertical );
        /** @brief 도구 바: 카메라 북마크 · 정렬. */
        static void drawToolsBar( ViewportToolbarSettings& settings, bool bVertical, const ViewportToolbarState& state );
        /** @brief 트랜스폼 바: 기즈모 이동 · 회전 · 크기 · 로컬 · 스냅. 고른 오브젝트가 없으면 막는다. */
        static void drawTransformBar( ViewportToolbarSettings& settings, bool bVertical, bool bEnabled );
        /** @brief 가로면 같은 줄에, 세로면 다음 줄에 둡니다. */
        static void nextItem( bool bVertical );
        /** @brief 툴바가 고른 보기 모드(`_requestedViewMode`)를 렌더러에 쓰고 비웁니다(Model — 렌더러가 없으면 버린다). */
        void applyViewModeRequest();
    };
} // namespace sw::editor
