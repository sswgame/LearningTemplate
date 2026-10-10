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
        void draw( const void* pTextureID, const float2& canvasSize );

        /** @brief 뷰포트 렌더 모드/카메라 속도 툴바를 그립니다. */
        void drawViewportToolbar( float32 viewportWidth );
        /** @brief 기즈모 트랜스폼 플로팅 바를 그립니다. */
        void drawTransformBar( const float2& anchorPos, float32 maxWidth );

        /** @brief 뷰 행렬을 계산합니다. */
        void getViewMatrix( float32* pOutMatrix ) const;
        /** @brief 투영 행렬을 계산합니다. */
        void getProjectionMatrix( float32* pOutMatrix, float32 aspect ) const;

        /** @brief 현재 선택한 GameObject 가 화면에 들어오도록 카메라를 맞춥니다(F 키). */
        void frameSelected();

        const float3& getCameraPosition() const { return _cameraPos; }
        /** @brief 기즈모를 마지막으로 그린 ImGui 프레임 번호입니다(-1 = 아직 없음). 에디터 자체 시험(`sceneView.gridAndGizmoDraw`)이 읽습니다. */
        int32 getLastGizmoFrame() const { return _lastGizmoFrame; }
        /** @brief 그 프레임에 기즈모를 단 오브젝트 수입니다. */
        uint32 getLastGizmoObjectCount() const { return _lastGizmoObjectCount; }

    private:
        void processFlyInput( float32 deltaTime );
        void processOrbitInput();
        void processPicking( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera );
        void drawGizmo( const float32* pView, const float32* pProj, const float2& canvasPos, const float2& canvasSize );

        /** @brief 다중 선택 기즈모를 조작합니다(열거형은 ImGuizmo 에 묶이지 않도록 uint32 로 받습니다). */
        void manipulateGroupGizmo( const float32* pView, const float32* pProj, uint32 operation, uint32 gizmoMode,
                                   const vector<GameObject*>& listGizmo, bool bUseSnap, const float32* pSnap );
        /**
         * @brief 끊긴 기즈모 드래그를 정리합니다. 대상이 아직 있고 편집이 허용되면 그 대상에 되돌리기를 남기고, 아니면 버립니다.
         * @details 드래그 도중 선택이 비거나(Delete · 생성 되돌리기) 편집이 막히면(Play) `drawGizmo` 가 앞에서 돌아가, 추적 표시와
         *          A 의 스냅숏이 남았습니다. 다음에 B 를 고른 첫 프레임이 그 스냅숏을 **B 에** 커밋해 엉터리 "Gizmo Transform" 항목이 생기고
         *          redo 가 잘렸으며, 그것을 되돌리면 A 의 XML(컴포넌트 id 포함)이 B 에 들어갔습니다.
         */
        void endGizmoDrag();
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
        ViewportToolbarSettings _toolbarSettings;
        ObjectSnapshot          _gizmoUndoBefore;
        GameObjectHandle        _gizmoObject; ///< `_gizmoUndoBefore` 의 대상. 드래그가 여러 프레임을 넘기므로 핸들로 듭니다
        /**
         * @brief 이 프레임의 오브젝트 스냅샷 (용량 재사용). 시각화와 통계 오버레이가 함께 봅니다.
         * @details 값 반환 `getAllGameObjects()` 는 호출마다 씬 전체를 새로 할당·복사하므로, 통계 오버레이와
         *          디버그 시각화가 각자 부르지 않고 이것을 함께 봅니다.
         */
        vector<GameObject*>      _listSceneObject;
        vector<GameObjectHandle> _listGizmoObject; ///< 그룹 기즈모 대상. 드래그하는 동안 여러 프레임을 넘기므로 핸들로 듭니다
        vector<ObjectSnapshot>   _listGizmoUndo;
        vector<float4x4>         _listGizmoRelativeWorld;
        float32                  _arrGizmoGroupMatrix[16];
        int32                    _lastGizmoFrame;
        uint32                   _lastGizmoObjectCount;
        uint8                    _bRulerActive   : 1;
        uint8                    _bGizmoTracking : 1;
        [[maybe_unused]] uint8   _reservedGizmo  : 6;
    };
} // namespace sw::editor
