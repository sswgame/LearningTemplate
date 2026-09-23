#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Viewport/EditorViewportToolbar.h"

namespace sw
{
    class CameraComponent;
    class GameObject;
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
     * @brief 뷰포트 캔버스 렌더링, 카메라 조작, 기즈모, 상단 툴바를 함께 관리하는 클라이언트입니다.
     */
    class EditorViewportClient
    {
    public:
        EditorViewportClient();
        ~EditorViewportClient() = default;

        /** @brief 뷰포트를 한 프레임 진행하고 입력을 처리합니다. */
        void update( float32 deltaTime, bool bWindowFocused, bool bWindowHovered );

        /** @brief 뷰포트 UI 와 ImGuizmo 를 그립니다. */
        void draw( const void* pTextureId, const float2& canvasSize );

        /** @brief 뷰포트 렌더 모드/카메라 속도 툴바를 그립니다. */
        void drawViewportToolbar( float32 viewportWidth );
        /** @brief 기즈모 트랜스폼 플로팅 바를 그립니다. */
        void drawTransformBar( const float2& anchorPos );

        /** @brief 뷰 행렬을 계산합니다. */
        void getViewMatrix( float32* pOutMatrix ) const;
        /** @brief 투영 행렬을 계산합니다. */
        void getProjectionMatrix( float32* pOutMatrix, float32 aspect ) const;

        /** @brief 현재 선택한 GameObject 가 화면에 들어오도록 카메라를 맞춥니다(F 키). */
        void frameSelected();

        const float3& getCameraPosition() const { return _cameraPos; }

    private:
        void processFlyInput( float32 deltaTime );
        void processOrbitInput();
        void processPicking( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera );
        void drawGizmo( const float32* pView, const float32* pProj, const float2& canvasPos, const float2& canvasSize );

        /** @brief 다중 선택 기즈모를 조작합니다(열거형은 ImGuizmo 에 묶이지 않도록 uint32 로 받습니다). */
        void manipulateGroupGizmo( const float32* pView, const float32* pProj, uint32 operation, uint32 gizmoMode,
                                   const vector<GameObject*>& listGizmo, bool bUseSnap, const float32* pSnap );
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
        EditorObjectSnapshot    _gizmoUndoBefore;
        /**
         * @brief 이 프레임의 오브젝트 스냅샷 (용량 재사용). 시각화와 통계 오버레이가 함께 봅니다.
         * @details 값 반환 `getAllGameObjects()` 는 호출마다 씬 전체를 새로 할당·복사합니다.
         *          예전에는 통계 오버레이가 **개수만 알려고** 한 번, 디버그 시각화가 한 번 그렇게
         *          불러서 프레임마다 씬을 두 번 복사했습니다.
         */
        vector<GameObject*>          _listSceneObject;
        vector<GameObjectHandle>     _listGizmoObject; ///< 그룹 기즈모 대상. 드래그하는 동안 여러 프레임을 넘기므로 핸들로 듭니다
        vector<EditorObjectSnapshot> _listGizmoUndo;
        vector<float4x4>             _listGizmoRelativeWorld;
        float32                      _arrGizmoGroupMatrix[16];
        uint8                        _bRulerActive   : 1;
        uint8                        _bGizmoTracking : 1;
        [[maybe_unused]] uint8       _reservedGizmo  : 6;
    };
} // namespace sw::editor
