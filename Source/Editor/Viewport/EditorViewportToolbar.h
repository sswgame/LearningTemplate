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
     * @brief 뷰포트 상단 툴바와 기즈모 트랜스폼 바입니다.
     */
    class EditorViewportToolbar
    {
    public:
        /** @brief 뷰포트 상단 뷰모드/카메라 속도 툴바를 그립니다. */
        static void draw( ViewportToolbarSettings& settings, float32 viewportWidth );
        /** @brief 선택된 오브젝트의 Translate/Rotate/Scale 및 스냅 플로팅 바를 그립니다. @p maxWidth 보다 넓으면 가로 스크롤로 넘깁니다. */
        static void drawTransformBar( ViewportToolbarSettings& settings, const float2& anchorPos, float32 maxWidth, bool bEnabled );
    };
} // namespace sw::editor
