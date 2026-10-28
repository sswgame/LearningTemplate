#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Viewport/EditorDebugHUDView.h"
#include "Editor/Viewport/EditorViewportCamera.h"
#include "Editor/Viewport/EditorViewportCanvasOverlay.h"
#include "Editor/Viewport/EditorViewportGizmo.h"
#include "Editor/Viewport/EditorViewportOverlays.h"
#include "Editor/Viewport/EditorViewportPicker.h"
#include "Editor/Viewport/EditorViewportToolbar.h"
#include "Editor/Viewport/EditorVisualizerStats.h"

namespace sw
{
    class CameraComponent;
    class GameObject;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditorViewportClient
     * @brief 씬 뷰 캔버스 한 장을 조정하는 클라이언트입니다. 일은 책임마다의 값 멤버가 하고, 이 클래스는 순서와 입력 분배만 정합니다.
     * @details 카메라는 `EditorViewportCamera`, 피킹과 호버는 `EditorViewportPicker`, 기즈모는 `EditorViewportGizmo`, 격자 · 자 · 방향 큐브는
     *          `EditorViewportCanvasOverlay`, 애셋 드롭은 `EditorViewportAssetDrop` 입니다. 모두 비가상으로 직접 부릅니다.
     *          그리는 순서(격자 → 시각화 → 자 → 피킹 · 호버 → 기즈모 → HUD · 방향 큐브 → 드롭)는 ImGui 의 "마지막 항목" 과 ImGuizmo 상태에 기대므로 `draw` 한곳에 둡니다.
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
        /** @brief 격자가 마지막으로 그린 결과입니다. 프로브 `Editor.Grid*` 와 자체 테스트가 읽습니다. */
        const EditorGridStats& getGridStats() const { return _canvasOverlay.getGridStats(); }
        /** @brief 시각화(`debug_draw` · 물리 셰이프 · 내비메시)가 마지막으로 그린 양입니다. 프로브와 자체 테스트가 읽습니다. */
        const EditorVisualizerStats& getVisualizerStats() const { return _visualizerStats; }

        /** @brief 뷰 행렬을 계산합니다. */
        void getViewMatrix( float32* pOutMatrix ) const { _camera.getViewMatrix( pOutMatrix ); }
        /** @brief 투영 행렬을 계산합니다. */
        void getProjectionMatrix( float32* pOutMatrix, float32 aspect ) const { _camera.getProjectionMatrix( pOutMatrix, aspect ); }

        /** @brief 현재 선택한 GameObject 가 화면에 들어오도록 카메라를 맞춥니다(F 키). */
        void frameSelected() { _camera.frameSelected(); }
        /** @brief 에디터 카메라가 @p target 을 반지름 @p radius 의 물체처럼 화면에 담도록 현재 방향 그대로 물러납니다. `frameSelected` 의 위치 버전입니다. */
        void focusOnPoint( const float3& target, float32 radius ) { _camera.focusOnPoint( target, radius ); }

        const float3& getCameraPosition() const { return _camera.getPosition(); }
        /** @brief 씬 뷰 카메라의 궤도 거리(m)입니다. 프로브 `Editor.SceneViewOrbitDistance` 가 읽습니다. */
        float32 getCameraOrbitDistance() const { return _camera.getOrbitDistance(); }
        /** @brief 씬 뷰 카메라(위치 · 회전 · 직교 · 속도)를 새 에디터 세션이 시작하는 값으로 되돌립니다. 씬을 다시 열어도 카메라 값은 이어지므로 따로 되돌린다. */
        void resetCameraToDefault();
        /** @brief 씬 뷰 카메라를 위치 @p position, 회전 @p rotation(도, 피치 · 요 · 롤)에 놓습니다(개발 명령 `camera.set`). */
        void setCameraPose( const float3& position, const float3& rotation ) { _camera.setPose( position, rotation ); }
        /** @brief 기즈모를 마지막으로 그린 ImGui 프레임 번호입니다. -1 이면 아직 그린 적이 없습니다. 에디터 자체 테스트(`sceneView.gridAndGizmoDraw`)가 읽습니다. */
        int32 getLastGizmoFrame() const { return _gizmo.getLastDrawFrame(); }
        /** @brief 그 프레임에 기즈모를 단 오브젝트 수입니다. */
        uint32 getLastGizmoObjectCount() const { return _gizmo.getLastObjectCount(); }

        /** @brief 툴바 설정을 반환합니다. 스냅, 시각화 켬과 끔 등이 있습니다. */
        const ViewportToolbarSettings& getToolbarSettings() const { return _toolbarSettings; }
        /** @brief 직교 보기이면 true 입니다. 키패드 7 은 위, 1 은 앞, 3 은 오른쪽이고, 5 는 직교와 원근을 전환합니다. */
        bool isOrthographicView() const { return _camera.isOrthographicView(); }
        /** @brief 씬 뷰를 최대화했으면 true 입니다(Shift+Space). */
        bool isMaximized() const { return _bMaximized == SW_TRUE; }
        /** @brief 마우스 아래의 오브젝트 id 입니다. 호버 강조에 쓰며, 없거나 막혀 있으면 0 입니다. */
        uint64 getHoveredObjectID() const { return _picker.getHoveredObjectID(); }

    private:
        /** @brief 비행 중이 아닐 때의 단축키를 처리합니다. W, E, R 은 기즈모 모드(Q 와 Space 는 순환), 키패드는 직교 보기, Shift+Space 는 최대화입니다. */
        void processShortcutKeys();
        /** @brief 씬 뷰 최대화를 켜고 끕니다. 다른 패널을 닫았다가 직전 배치를 복원하며, 유니티의 Shift+Space 에 해당합니다. */
        void toggleMaximize();
        /**
         * @brief 우클릭 메뉴를 엽니다. 캔버스를 오른쪽 단추로 누르고 끌지 않고 놓았을 때, 등록된 Viewport 항목이 있을 때만 엽니다.
         * @details 오른쪽 단추를 끌면 비행 · 궤도 줌이므로 메뉴를 열지 않는다(언리얼 뷰포트와 같다). 연 순간의 맞은 오브젝트와 바닥 교점을 담아 둔다.
         */
        void processContextMenu( const float2& canvasPos, const float2& canvasSize, CameraComponent* pCamera, bool bCanvasHovered );

    private:
        ViewportToolbarSettings     _toolbarSettings;
        float32                     _canvasHeight;             ///< 지난 `draw` 의 캔버스 높이(픽셀). 아직 그리지 않았으면 0 이고 팬은 쉰다
        EditorViewportCamera        _camera;                   ///< 씬 뷰 카메라 값과 비행 · 궤도 · 북마크 입력
        EditorViewportPicker        _picker;                   ///< 클릭 선택과 호버
        EditorViewportGizmo         _gizmo;                    ///< 트랜스폼 기즈모와 그 되돌리기
        EditorViewportCanvasOverlay _canvasOverlay;            ///< 격자, 자, 방향 큐브
        EditorViewportOverlays      _overlays;                 ///< 씬 뷰 위의 오버레이 바 목록
        EditorDebugHUDView          _hudView;                  ///< 오른쪽 위의 HUD 박스
        EditorVisualizerStats       _visualizerStats;          ///< 시각화가 마지막으로 그린 양(그리기 인자로 시각화에 넘긴다)
        uint64                      _contextMenuObjectID;      ///< 우클릭 메뉴를 연 순간 맞은 오브젝트 id. 0 이면 빈 곳. 메뉴가 열린 동안 프레임마다 다시 찾는다
        float3                      _contextMenuWorldPosition; ///< 우클릭 메뉴를 연 순간의 바닥 평면 교점
        bool                        _bContextMenuHasWorldPosition;
        uint8                       _bMaximized : 1;
        [[maybe_unused]] uint8      _reserved   : 7;
    };
} // namespace sw::editor
