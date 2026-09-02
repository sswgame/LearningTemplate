#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Viewport/EditorViewportToolbar.h"

#include "Engine/Object/GameObject/GameObjectPtr.h"

namespace sw
{
    class CameraComponent;
    class IRHIDevice;
} // namespace sw

struct ImDrawList;

namespace sw::editor
{
    /** @brief 에디터 카메라 제어 모드 */
    enum class CameraControlMode : uint8
    {
        Fly = 0, ///< WASD + RMB 회전 (Unreal 스타일)
        Orbit,   ///< Alt + LMB 회전, Alt + RMB 줌 (Maya/Unity 스타일)
        Ortho2D  ///< 2D 휠 줌 & MMB 패닝
    };

    /**
     * @class EditorViewportClient
     * @brief 뷰포트 캔버스 렌더링, 카메라 조작, 기즈모 및 상단 툴바를 통합 관리하는 클라이언트
     */
    class EditorViewportClient
    {
    public:
        EditorViewportClient();
        ~EditorViewportClient() = default;

        /** @brief 뷰포트 프레임 틱 및 입력 처리 */
        void update( float32 deltaTime, bool bWindowFocused, bool bWindowHovered );

        /** @brief 뷰포트 UI 및 ImGuizmo 렌더링 */
        void draw( const void* pTextureId, const float2& canvasSize );

        /** @brief 뷰포트 렌더 모드/카메라 속도 툴바를 그립니다. */
        void drawViewportToolbar( float32 viewportWidth );
        /** @brief 기즈모 트랜스폼 플로팅 바를 그립니다. */
        void drawTransformBar( const float2& anchorPos );

        /** @brief View Matrix 계산 */
        void getViewMatrix( float32* pOutMatrix ) const;
        /** @brief Projection Matrix 계산 */
        void getProjectionMatrix( float32* pOutMatrix, float32 aspect ) const;

        /** @brief 카메라 위치 및 회전 설정 */
        void setCameraPosition( const float3& pos ) { _cameraPos = pos; }
        void setCameraRotation( const float3& rot ) { _cameraRot = rot; }
        void setOrbitTarget( const float3& target ) { _orbitTarget = target; }
        void setCameraMode( CameraControlMode mode ) { _cameraMode = mode; }

        /** @brief 현재 선택된 GameObject로 카메라를 프레이밍(F key)합니다. */
        void frameSelected();

        const float3&            getCameraPosition() const { return _cameraPos; }
        const float3&            getCameraRotation() const { return _cameraRot; }
        CameraControlMode        getCameraMode() const { return _cameraMode; }
        ViewportToolbarSettings& getToolbarSettings() { return _toolbarSettings; }

    private:
        void processFlyInput( float32 deltaTime );
        void processOrbitInput();
        void processPicking( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera );
        void drawGizmo( const float32* pView, const float32* pProj, const float2& canvasPos, const float2& canvasSize );
        void drawStatsOverlay( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize );
        void drawOrientationCube( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize );
        void drawAdaptiveGrid( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize,
                               const float32* pView, const float32* pProj );
        void processRulerTool( ImDrawList* pDrawList, const float2& canvasPos, const float2& canvasSize,
                               const float32* pView, const float32* pProj );
        void handleViewportAssetDrop( const utf8* pAssetPath, const float2& canvasPos, const float2& canvasSize,
                                      const float32* pView, const float32* pProj );

    private:
        float3                  _cameraPos;
        float3                  _cameraRot; // Pitch, Yaw, Roll
        float3                  _orbitTarget;
        float3                  _rulerStartWorld;
        float3                  _rulerEndWorld;
        float32                 _orbitDistance;
        float32                 _fovY;
        float32                 _nearZ;
        float32                 _farZ;
        CameraControlMode       _cameraMode;
        ViewportToolbarSettings _toolbarSettings;
        string                  _gizmoUndoBeforeXml;
        vector<GameObjectPtr>   _listGizmoObject;
        vector<string>          _listGizmoUndoXml;
        vector<float4x4>        _listGizmoRelativeWorld;
        float32                 _arrGizmoGroupMatrix[16];
        uint8                   _bRulerActive   : 1;
        uint8                   _bGizmoTracking : 1;
        [[maybe_unused]] uint8  _reservedGizmo  : 6;
    };
} // namespace sw::editor
