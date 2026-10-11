#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Viewport/EditorDebugHUDView.h"
#include "Editor/Viewport/EditorViewportOverlays.h"
#include "Editor/Viewport/EditorViewportToolbar.h"

namespace sw
{
    class CameraComponent;
    class Component;
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

        /** @brief 씬 뷰 오버레이 바(보기, 표시, 도구, 트랜스폼)를 뷰포트 이미지 위에 그립니다. 이미지를 그린 뒤 호출합니다. */
        void drawOverlays( const float2& canvasMin, const float2& canvasSize );
        /** @brief 씬 뷰 툴바의 Overlays 메뉴를 그립니다. */
        void                          drawOverlaysMenu() { _overlays.drawOverlaysMenu(); }
        EditorViewportOverlays&       getOverlays() { return _overlays; }
        const EditorViewportOverlays& getOverlays() const { return _overlays; }
        /** @brief 씬 뷰 HUD 박스를 반환합니다. 프로브가 읽습니다. */
        const EditorDebugHUDView& getHUDView() const { return _hudView; }

        /** @brief 뷰 행렬을 계산합니다. */
        void getViewMatrix( float32* pOutMatrix ) const;
        /** @brief 투영 행렬을 계산합니다. */
        void getProjectionMatrix( float32* pOutMatrix, float32 aspect ) const;

        /** @brief 현재 선택한 GameObject 가 화면에 들어오도록 카메라를 맞춥니다(F 키). */
        void frameSelected();
        /** @brief 에디터 카메라가 @p target 을 반지름 @p radius 의 물체처럼 화면에 담도록 현재 방향 그대로 물러납니다. `frameSelected` 의 위치 버전입니다. */
        void focusOnPoint( const float3& target, float32 radius );

        const float3& getCameraPosition() const { return _cameraPos; }
        /** @brief 기즈모를 마지막으로 그린 ImGui 프레임 번호입니다. -1 이면 아직 그린 적이 없습니다. 에디터 자체 테스트(`sceneView.gridAndGizmoDraw`)가 읽습니다. */
        int32 getLastGizmoFrame() const { return _lastGizmoFrame; }
        /** @brief 그 프레임에 기즈모를 단 오브젝트 수입니다. */
        uint32 getLastGizmoObjectCount() const { return _lastGizmoObjectCount; }

        /** @brief 툴바 설정을 반환합니다. 스냅, 시각화 켬과 끔 등이 있습니다. */
        const ViewportToolbarSettings& getToolbarSettings() const { return _toolbarSettings; }
        /** @brief 직교 보기이면 true 입니다. 키패드 7 은 위, 1 은 앞, 3 은 오른쪽이고, 5 는 직교와 원근을 전환합니다. */
        bool isOrthographicView() const { return _bOrthographicView == SW_TRUE; }
        /** @brief 씬 뷰를 최대화했으면 true 입니다(Shift+Space). */
        bool isMaximized() const { return _bMaximized == SW_TRUE; }
        /** @brief 마우스 아래의 오브젝트 id 입니다. 호버 강조에 쓰며, 없거나 막혀 있으면 0 입니다. */
        uint64 getHoveredObjectID() const { return _hoveredObjectID; }

    private:
        void processFlyInput( float32 deltaTime );
        void processOrbitInput();
        /** @brief 비행 중이 아닐 때의 단축키를 처리합니다. W, E, R 은 기즈모 모드(Q 와 Space 는 순환), 키패드는 직교 보기, Shift+Space 는 최대화입니다. */
        void processShortcutKeys();
        /** @brief 직교 보기로 바꿉니다. @p pitch 와 @p yaw 는 도(°) 단위입니다. */
        void setOrthographicView( float32 pitch, float32 yaw );
        /** @brief 씬 뷰 최대화를 켜고 끕니다. 다른 패널을 닫았다가 직전 배치를 복원하며, 유니티의 Shift+Space 에 해당합니다. */
        void toggleMaximize();
        void processPicking( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, bool bCanvasClicked );
        /** @brief 마우스 아래 오브젝트를 찾습니다. 빌보드를 먼저 보고 레이 피킹을 하며, 잠그거나 숨긴 오브젝트는 빈 곳으로 처리합니다. 클릭 선택과 호버가 함께 씁니다. */
        bool findObjectUnderMouse( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, GameObject*& pOutObject, Component*& pOutComponent ) const;
        /** @brief 호버 오브젝트를 정합니다. 마우스가 움직였을 때만 피킹하며 프레임당 많아야 한 번입니다. */
        void processHover( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, bool bCanvasHovered );
        void drawGizmo( const float32* pView, const float32* pProj, const float2& canvasPos, const float2& canvasSize );

        /** @brief 다중 선택 기즈모를 조작합니다. 열거형은 ImGuizmo 에 의존하지 않도록 uint32 로 받습니다. */
        void manipulateGroupGizmo( const float32* pView, const float32* pProj, uint32 operation, uint32 gizmoMode,
                                   const vector<GameObject*>& listGizmo, bool bUseSnap, const float32* pSnap );
        /**
         * @brief 끊긴 기즈모 드래그를 정리합니다. 대상이 아직 있고 편집이 허용되면 그 대상에 되돌리기를 남기고, 아니면 버립니다.
         * @details 드래그 도중 선택이 비거나(Delete, 생성 되돌리기) 편집이 막히면(Play) `drawGizmo` 가 앞에서 반환하므로, 추적 표시와
         *          A 의 스냅샷이 남는다. 정리하지 않으면 다음에 B 를 선택한 첫 프레임이 그 스냅샷을 B 에 커밋해서 엉뚱한 "Gizmo Transform" 항목이 생기고
         *          redo 가 잘리며, 그것을 되돌리면 A 의 XML(컴포넌트 id 포함)이 B 에 들어간다.
         */
        void endGizmoDrag();
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
        float3                  _lastAppliedCameraPos; ///< 지난 프레임에 에디터 카메라에 설정한 로컬 위치. 외부(bugitgo, teleport)가 옮겼는지 구분하는 데 쓴다
        uint64                  _lastAppliedCameraID;  ///< 그 카메라 컴포넌트 id. 0 이면 아직 설정하지 않았다. 씬이 바뀌어 새 카메라가 되었으면 받아들이지 않는다
        float32                 _orbitDistance;
        float32                 _fovY;
        float32                 _nearZ;
        float32                 _farZ;
        ViewportToolbarSettings _toolbarSettings;
        EditorViewportOverlays  _overlays;     ///< 씬 뷰 위의 오버레이 바 목록
        EditorDebugHUDView      _hudView;      ///< 오른쪽 위의 HUD 박스
        float32                 _hudBoxHeight; ///< 지난 프레임의 HUD 박스 높이. 방향 큐브를 그 아래에 둔다
        ObjectSnapshot          _gizmoUndoBefore;
        GameObjectHandle        _gizmoObject; ///< `_gizmoUndoBefore` 의 대상이다. 드래그가 여러 프레임에 걸치므로 핸들로 보관한다
        /**
         * @brief 이 프레임의 오브젝트 스냅샷입니다. 용량을 재사용하며, 시각화와 통계 오버레이가 함께 읽습니다.
         * @details 값을 반환하는 `getAllGameObjects()` 는 호출마다 씬 전체를 새로 할당하고 복사한다. 그래서 통계 오버레이와
         *          디버그 시각화가 각자 호출하지 않고 이 스냅샷을 함께 읽는다.
         */
        vector<GameObject*>      _listSceneObject;
        vector<GameObjectHandle> _listGizmoObject; ///< 그룹 기즈모의 대상이다. 드래그하는 동안 여러 프레임에 걸치므로 핸들로 보관한다
        vector<ObjectSnapshot>   _listGizmoUndo;
        vector<float4x4>         _listGizmoRelativeWorld;
        float32                  _arrGizmoGroupMatrix[16];
        int32                    _lastGizmoFrame;
        uint32                   _lastGizmoObjectCount;
        float32                  _orthoHeight;          ///< 직교 보기의 화면 높이(월드 단위). 휠로 바꾼다
        uint64                   _hoveredObjectID;      ///< 마우스 아래 오브젝트. 0 이면 없다
        float2                   _hoverMousePos;        ///< 마지막으로 호버 피킹한 마우스 위치. 그대로이면 다시 찾지 않는다
        float64                  _lastHoverPickSeconds; ///< 마지막 호버 피킹에 걸린 시간(초)
        float64                  _lastHoverPickTime;    ///< 그 피킹을 한 ImGui 시각(초)
        uint8                    _bRulerActive      : 1;
        uint8                    _bGizmoTracking    : 1;
        uint8                    _bOrthographicView : 1;
        uint8                    _bMaximized        : 1;
        [[maybe_unused]] uint8   _reservedGizmo     : 4;
    };
} // namespace sw::editor
