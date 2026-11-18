/**
 * @file EditorViewportClientScene.h
 * @brief 씬 뷰 클라이언트(`EditorViewportClient`, View)가 씬 · 작업 공간에 닿는 창구입니다 — 씬 뷰 카메라 값 모으기, 시각화 입력 채우기, 기즈모 모으기 · 적용, 단축키 명령. ImGui 를 모릅니다.
 * @details 언리얼 `FEditorViewportClient` 가 뷰 값(`FSceneView`)을 받아 그리고 편집은 모드 · 트랜잭션에 넘기는 가름이다. 클라이언트는 이 값과 명령만 쓰고 씬 헤더를 보지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    struct EditorGizmoCommand;
    struct EditorGizmoViewState;
    struct EditorViewportVisualizerArgs;

    class EditorGizmoEdit;
    class EditorViewportCamera;

    /** @brief 씬 뷰 이미지를 그린 카메라(에디터 카메라)의 이번 프레임 값입니다. */
    struct EditorViewportFrame
    {
        float4x4 _view{};
        float4x4 _projection{};
        float4x4 _viewProj{};
        uint64   _cameraObjectID{ 0 }; ///< 에디터 카메라 오브젝트(상자 선택이 고르지 않는다)
        bool     _bFlat2D{ false };    ///< NOLINT(sw-bool-flag): 값 집합체 — 직교 카메라가 월드 Z 축을 본다(`EditorVisualizerGeometryUtil::isFlat2DView`)
    };

    /**
     * @struct EditorViewportClientScene
     * @brief 씬 뷰 클라이언트의 Model 함수 모음입니다(메인 스레드, 상태 없음).
     */
    struct SW_EDITOR_API EditorViewportClientScene
    {
        /**
         * @brief 씬 뷰 카메라 값을 모읍니다. 카메라는 호스트가 씬 뷰를 그리는 카메라와 같다(`ImGuiEditor::getSceneViewCamera`) — 오버레이는 이 카메라로 투영해야 이미지와 겹친다.
         * @return 활성 씬이 없어 카메라가 없으면 false 입니다.
         */
        [[nodiscard]] static bool gatherFrame( float32 aspect, EditorViewportFrame& outFrame );
        /** @brief 뷰포트 카메라 값(@p camera)을 씬 뷰 카메라에 씁니다. Play 중에도 씬 뷰는 이 카메라를 쓴다. */
        static void applyCamera( EditorViewportCamera& camera );
        /** @brief 시각화 모으기 입력의 씬 쪽(활성 카메라 · 카메라 · 콜라이더 레지스트리 · 디버그 도형 큐 · 오브젝트 매니저)을 채웁니다. 씬 전체를 복사하지 않고 레지스트리를 가리킨다. */
        static void fillVisualizerScene( EditorViewportVisualizerArgs& inoutArgs );
        /** @brief 씬 변환을 이번 프레임 값으로 맞춥니다(기즈모 모으기 전에). */
        static void flushSceneTransforms();
        /** @brief 메인 선택의 기즈모 대상을 모읍니다(`EditorGizmoEdit::gather`). */
        static void gatherGizmo( EditorGizmoViewState& outState );
        /** @brief 기즈모 명령을 메인 선택에 적용합니다(`EditorGizmoEdit::apply`). */
        static void applyGizmo( EditorGizmoEdit& edit, const vector<EditorGizmoCommand>& listCommand );
        /** @brief 씬 뷰가 포커스면 인스펙터 · 전역 명령이 다시 메인 선택을 겨눈다 — 미리보기 창이 걸어 둔 것을 푼다(`editor::getFocusedSelection`). */
        static void focusMainSelection();
        /** @brief 워크스페이스 기즈모 조작 번호(0 이동 · 1 회전 · 2 크기)입니다. */
        [[nodiscard]] static int32 getGizmoOperation();
        static void                setGizmoOperation( int32 operation );
        /** @brief 씬 뷰 패널을 최대화합니다(다른 패널을 닫는다). 됐으면 true 입니다. */
        [[nodiscard]] static bool maximizeSceneView();
        /** @brief 최대화 직전 배치를 되돌립니다. 그 배치가 없으면 false 입니다. */
        [[nodiscard]] static bool restoreMaximizedLayout();
    };
} // namespace sw::editor
