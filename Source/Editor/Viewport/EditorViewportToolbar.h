#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    struct float2;
} // namespace sw

namespace sw::editor
{
    /** @brief 뷰포트 렌더 모드 */
    enum class ViewportRenderMode : uint8
    {
        Lit = 0,
        Unlit,
        Wireframe
    };

    /** @brief 뷰포트 툴바 설정 데이터 */
    struct ViewportToolbarSettings
    {
        float32            _gridSnapValue{ 1.0f };
        float32            _rotationSnapValue{ 15.0f };
        float32            _scaleSnapValue{ 0.1f };
        float32            _cameraSpeed{ 5.0f };
        int32              _requestedBookmarkSlot{ -1 };
        ViewportRenderMode _renderMode{ ViewportRenderMode::Lit };
        bool               _bGridSnap{ false };
        bool               _bRotationSnap{ false };
        bool               _bScaleSnap{ false };
        bool               _bShowStats{ true };
        bool               _bShowColliders{ true };
        bool               _bShowCameras{ true };
        bool               _bShowGrid{ true };
        bool               _bShowOrientationCube{ true };
        bool               _bShowRuler{ false };
        bool               _bIs2DMode{ false };
        bool               _bSurfaceSnap{ false };
    };

    /**
     * @class EditorViewportToolbar
     * @brief 뷰포트 상단 툴바와 기즈모 트랜스폼 바
     */
    class EditorViewportToolbar
    {
    public:
        /** @brief 뷰포트 상단 뷰모드/카메라 속도 툴바를 그립니다. */
        static void draw( ViewportToolbarSettings& settings, float32 viewportWidth );
        /** @brief 선택된 오브젝트의 Translate/Rotate/Scale 및 스냅 플로팅 바를 그립니다. */
        static void drawTransformBar( ViewportToolbarSettings& settings, const float2& anchorPos, bool bEnabled );
    };
} // namespace sw::editor
