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
        float32 _gridSnapValue{ 1.0f };
        float32 _rotationSnapValue{ 15.0f };
        float32 _scaleSnapValue{ 0.1f };
        float32 _cameraSpeed{ 5.0f };
        /**
         * @brief 켜진 컴포넌트 시각화 비트마스크 (`EditorViewportVisualizer` 표의 인덱스).
         * @details 예전에는 시각화마다 bool 하나(`_bShowColliders`·`_bShowCameras`)가 여기 있었고
         *          툴바에도 체크박스를 손으로 적었다. 지금은 표가 개수를 정하므로 시각화를 더해도
         *          이 구조체와 툴바는 그대로다. 초기값은 생성자가 표에서 받아 채운다.
         */
        uint32             _visualizerMask{ 0 };
        int32              _requestedBookmarkSlot{ -1 };
        ViewportRenderMode _renderMode{ ViewportRenderMode::Lit };
        bool               _bGridSnap{ false };
        bool               _bRotationSnap{ false };
        bool               _bScaleSnap{ false };
        bool               _bShowStats{ true };
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
