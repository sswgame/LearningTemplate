#pragma once
#include "Core/Common/Types.h"

#include "Editor/Viewport/EditorVisualizerToggles.h"

namespace sw
{
    struct float2;
} // namespace sw

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
        EditorVisualizerToggles _visualizerToggles;
        int32                   _requestedBookmarkSlot{ -1 };
        // 뷰 모드(Lit/Unlit/Wireframe)는 여기 없다. 정본은 `FrameRenderer` 다. 같은 값을 두 곳에
        // 두면 반드시 어긋난다(커맨드라인 `-gv_viewMode` 나 코드가 바꾸면 툴바가 틀린 값을 보인다).
        // 툴바는 프레임마다 렌더러에서 읽어 표시하고, 고르면 렌더러에 쓴다.
        bool _bGridSnap{ false };
        bool _bRotationSnap{ false };
        bool _bScaleSnap{ false };
        bool _bShowStats{ true };
        bool _bShowGrid{ true };
        bool _bShowOrientationCube{ true };
        bool _bShowRuler{ false };
        bool _bIs2DMode{ false };
        bool _bSurfaceSnap{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorViewportToolbar
     * @brief 씬 뷰 오버레이 바들의 내용입니다(보기 · 표시 · 도구 · 트랜스폼). 바 창 · 자리는 `EditorViewportOverlays` 가 정합니다.
     * @details 바가 왼쪽 · 오른쪽에 붙으면 @p bVertical 이 참이고 항목을 세로로 쌓는다.
     */
    class EditorViewportToolbar
    {
    public:
        /** @brief 보기 바: 보기 모드 · 2D/3D · 카메라 속도. */
        static void drawViewBar( ViewportToolbarSettings& settings, bool bVertical );
        /** @brief 표시 바: 통계 · 격자 · 방향 큐브 · 시각화 토글 · 표면 스냅. */
        static void drawDisplayBar( ViewportToolbarSettings& settings, bool bVertical );
        /** @brief 도구 바: 카메라 북마크 · 정렬. */
        static void drawToolsBar( ViewportToolbarSettings& settings, bool bVertical );
        /** @brief 트랜스폼 바: 기즈모 이동 · 회전 · 크기 · 로컬 · 스냅. 고른 오브젝트가 없으면 막는다. */
        static void drawTransformBar( ViewportToolbarSettings& settings, bool bVertical, bool bEnabled );
        /** @brief 가로면 같은 줄에, 세로면 다음 줄에 둡니다. */
        static void nextItem( bool bVertical );
    };
} // namespace sw::editor
