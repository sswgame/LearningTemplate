/**
 * @file EditorViewportGizmo.h
 * @brief 씬 뷰의 트랜스폼 기즈모(ImGuizmo)입니다. 단일 · 다중 선택 조작과 드래그 하나를 되돌리기 하나로 기록하는 일을 합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Workspace/EditorTransaction.h"

namespace sw
{
    class GameObject;
} // namespace sw

namespace sw::editor
{
    struct ViewportToolbarSettings;

    class EditorSelection;

    /**
     * @class EditorViewportGizmo
     * @brief 선택한 오브젝트에 기즈모를 그리고 조작을 입힙니다. 드래그가 끝나면 시작 전 스냅샷으로 되돌리기를 남깁니다.
     * @details `EditorViewportClient` 가 메인 선택으로, 미리보기 창(`EditorPreviewPanel`)이 창의 선택으로 값 멤버를 들고 부릅니다. 대상과 기록 범위는 넘긴 선택이
     *          정합니다 — 핸들을 그 선택의 씬에서 풀고(`EditorSelection::findObject`), 드래그 하나를 그 선택의 범위에 기록합니다(메인은 씬 명령, 미리보기는 창의 스택).
     *          헤더는 ImGuizmo 를 include 하지 않습니다.
     */
    class EditorViewportGizmo
    {
    public:
        EditorViewportGizmo();

        /**
         * @brief @p selection 의 오브젝트에 기즈모를 그리고 조작합니다. 편집이 막혔거나(Play) 선택이 비면 진행 중인 드래그를 정리하고 반환합니다.
         * @details 드래그 중 다른 선택이 넘어오면 앞 드래그를 그 선택의 범위에 정리한다. @p selection 은 이 기즈모보다 오래 살거나 그 전에 `endDrag` 가 불려야 한다
         *          (미리보기 창은 기즈모와 선택을 함께 든다). 표면 붙이기는 메인 선택에만 쓴다(활성 씬을 레이캐스트한다).
         */
        void draw( EditorSelection& selection, const float32* pView, const float32* pProj, const float2& canvasPos, const float2& canvasSize,
                   const ViewportToolbarSettings& settings );

        /** @brief 기즈모를 끄는 중이면 true 입니다. 호버 강조를 막는 데 씁니다. */
        bool isDragging() const { return _bTracking == SW_TRUE; }
        /** @brief 지난 `draw` 에서 마우스가 기즈모 위에 있었으면 true 입니다. 미리보기 뷰포트가 오빗 입력을 양보하는 데 씁니다. */
        bool isHovered() const { return _bHovered == SW_TRUE; }
        /** @brief 기즈모를 마지막으로 그린 ImGui 프레임 번호입니다. -1 이면 아직 그린 적이 없습니다. */
        int32 getLastDrawFrame() const { return _lastDrawFrame; }
        /** @brief 그 프레임에 기즈모를 단 오브젝트 수입니다. */
        uint32 getLastObjectCount() const { return _lastObjectCount; }

    private:
        /** @brief 다중 선택 기즈모를 조작합니다. 열거형은 ImGuizmo 에 의존하지 않도록 uint32 로 받습니다. */
        void manipulateGroup( const float32* pView, const float32* pProj, uint32 operation, uint32 gizmoMode, const vector<GameObject*>& listGizmo, bool bUseSnap,
                              const float32* pSnap );
        /**
         * @brief 끊긴 기즈모 드래그를 정리합니다. 대상이 아직 있고 편집이 허용되면 그 대상에 되돌리기를 남기고, 아니면 버립니다.
         * @details 드래그 도중 선택이 비거나(Delete, 생성 되돌리기) 편집이 막히면(Play) `draw` 가 앞에서 반환하므로, 추적 표시와
         *          A 의 스냅샷이 남는다. 정리하지 않으면 다음에 B 를 선택한 첫 프레임이 그 스냅샷을 B 에 커밋해서 엉뚱한 "Gizmo Transform" 항목이 생기고
         *          redo 가 잘리며, 그것을 되돌리면 A 의 XML(컴포넌트 id 포함)이 B 에 들어간다.
         */
        void endDrag();
        /** @brief 드래그 대상 선택의 범위에서 핸들을 풉니다. */
        GameObject* findTracked( GameObjectHandle handle ) const;
        /** @brief 드래그 하나를 대상 선택의 범위에 기록합니다. */
        void commitDrag( GameObject& obj, const ObjectSnapshot& before ) const;

    private:
        ObjectSnapshot           _undoBefore;
        GameObjectHandle         _object;     ///< `_undoBefore` 의 대상이다. 드래그가 여러 프레임에 걸치므로 핸들로 보관한다
        vector<GameObjectHandle> _listObject; ///< 그룹 기즈모의 대상이다. 드래그하는 동안 여러 프레임에 걸치므로 핸들로 보관한다
        vector<ObjectSnapshot>   _listUndo;
        vector<float4x4>         _listRelativeWorld;
        EditorSelection*         _pTarget; ///< 지금 드래그(또는 마지막으로 그린)의 대상 선택(빌림)
        float32                  _arrGroupMatrix[16];
        int32                    _lastDrawFrame;
        uint32                   _lastObjectCount;
        uint8                    _bTracking : 1;
        uint8                    _bHovered  : 1;
        [[maybe_unused]] uint8   _reserved  : 6;
    };
} // namespace sw::editor
