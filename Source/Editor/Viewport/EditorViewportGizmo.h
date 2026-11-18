/**
 * @file EditorViewportGizmo.h
 * @brief 씬 뷰의 트랜스폼 기즈모(ImGuizmo) View 입니다. 대상 스냅샷(`EditorGizmoViewState`)만 읽고, 조작은 명령(`EditorGizmoCommand`)으로 냅니다 — 적용과 되돌리기는 `EditorGizmoEdit`(Model)이 합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Viewport/Model/EditorGizmoEdit.h"

namespace sw::editor
{
    struct ViewportToolbarSettings;

    /**
     * @class EditorViewportGizmo
     * @brief 대상에 기즈모를 그리고 조작을 명령으로 냅니다. 드래그 중간 값(그룹 행렬 · 대상의 그룹 기준 상대 행렬)은 이 쪽이 들고, 바뀐 월드 행렬을 프레임마다 명령 하나로 보낸다.
     * @details `EditorViewportClient` 와 미리보기 기즈모(`EditorPreviewGizmo`)가 값 멤버로 들고, 모으기(`EditorGizmoEdit::gather`) → `draw` → 적용(`EditorGizmoEdit::apply( getCommands() )`) 순으로 부른다.
     *          헤더는 ImGuizmo 를 include 하지 않습니다.
     */
    class EditorViewportGizmo
    {
    public:
        EditorViewportGizmo();

        /**
         * @brief @p state 의 대상에 기즈모를 그리고 이번 프레임의 명령을 모읍니다(앞 프레임 명령은 비운다). 편집이 막혔거나(Play) 대상이 없으면 진행 중인 드래그를 끝내는 명령만 낸다.
         * @param pCenterMark 기즈모 가운데(화면 평면 이동 손잡이)를 적을 셀프테스트 마크 이름입니다. nullptr 이면 적지 않는다 — 한 프레임에 기즈모가 여럿(씬 뷰 · 미리보기 창)이라
         *        이름은 부르는 쪽이 정한다(시나리오가 `EditorDrag` 로 끈다).
         */
        void draw( const EditorGizmoViewState& state, const float32* pView, const float32* pProj, const float2& canvasPos, const float2& canvasSize,
                   const ViewportToolbarSettings& settings, const utf8* pCenterMark );
        /** @brief 지난 `draw` 가 낸 명령입니다(Model 이 적용한다). */
        const vector<EditorGizmoCommand>& getCommands() const { return _listCommand; }

        /** @brief 기즈모를 끄는 중이면 true 입니다. 호버 강조를 막는 데 씁니다. */
        bool isDragging() const { return _bTracking == SW_TRUE; }
        /** @brief 지난 `draw` 에서 마우스가 기즈모 위에 있었으면 true 입니다. 미리보기 뷰포트가 오빗 입력을 양보하는 데 씁니다. */
        bool isHovered() const { return _bHovered == SW_TRUE; }
        /**
         * @brief 지금 마우스가 지난 프레임에 그린 기즈모 위거나 끄는 중이면 true 입니다. 피킹처럼 기즈모보다 먼저 도는 쪽이 봅니다.
         * @details ImGuizmo::IsOver 는 지금 마우스로 답하지만 그리지 않는 프레임에도 마지막 행렬로 답한다 - 선택을 푼 뒤 그 자리의 클릭을 가로채므로 그렸는지를 함께 본다.
         */
        [[nodiscard]] bool isMouseOverDrawnGizmo() const;
        /** @brief 기즈모를 마지막으로 그린 ImGui 프레임 번호입니다. -1 이면 아직 그린 적이 없습니다. */
        int32 getLastDrawFrame() const { return _lastDrawFrame; }
        /** @brief 그 프레임에 기즈모를 단 오브젝트 수입니다. */
        uint32 getLastObjectCount() const { return _lastObjectCount; }

    private:
        /** @brief 다중 대상 기즈모 — 무게중심의 그룹 행렬을 끌고, 누른 순간의 상대 행렬로 대상마다 월드 행렬을 낸다. 열거형은 ImGuizmo 에 의존하지 않도록 uint32 로 받습니다. */
        void manipulateGroup( const EditorGizmoViewState& state, const float32* pView, const float32* pProj, uint32 operation, uint32 gizmoMode, const float32* pSnap );
        /** @brief 대상 하나의 기즈모입니다. */
        void manipulateSingle( const EditorGizmoTarget& target, const float32* pView, const float32* pProj, uint32 operation, uint32 gizmoMode, const float32* pSnap,
                               bool bSurfaceSnap );
        /** @brief ImGuizmo 사용 상태로 끌기 명령 · 놓기 명령을 냅니다. @p listWorld 는 이번 프레임에 바뀐 대상 행렬(없으면 빈 목록)이다. */
        void trackUse( vector<EditorGizmoTarget>&& listWorld, bool bSurfaceSnap );
        /** @brief 진행 중인 드래그를 끝내는 명령을 내고 중간 값을 비웁니다. */
        void endDrag();
        /** @brief 기즈모 가운데(대상 하나면 그 위치, 여럿이면 무게중심 — `manipulateGroup` 과 같은 점)의 화면 자리를 마크 @p pMark 로 적습니다. 카메라 뒤면 적지 않는다. */
        static void recordCenterMark( const utf8* pMark, const vector<EditorGizmoTarget>& listTarget, const float32* pView, const float32* pProj, const float2& canvasPos,
                                      const float2& canvasSize );

    private:
        vector<EditorGizmoCommand> _listCommand;        ///< 이번 프레임에 낸 명령
        vector<EditorGizmoTarget>  _listDragTarget;     ///< 그룹 드래그의 대상과 그룹 기준 상대 행렬(누른 순간) — 드래그 중간 값
        float32                    _arrGroupMatrix[16]; ///< 그룹 기즈모 행렬(ImGuizmo 가 고친다)
        int32                      _lastDrawFrame;
        uint32                     _lastObjectCount;
        uint8                      _bTracking : 1;
        uint8                      _bHovered  : 1;
        [[maybe_unused]] uint8     _reserved  : 6;
    };
} // namespace sw::editor
