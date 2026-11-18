/**
 * @file EditorGizmoEdit.h
 * @brief 트랜스폼 기즈모의 Model 쪽입니다 — 대상 스냅샷(핸들 · 월드 행렬)을 모으고, 기즈모(View)가 보낸 명령(잡기 · 끌기 · 놓기)을 씬에 적용해 드래그 하나를 되돌리기 하나로 남깁니다. ImGui 를 모릅니다.
 * @details 언리얼 기즈모가 끄는 동안 값을 UI 쪽에 들고 트랜잭션(`FScopedTransaction`)으로 적용하는 자리다. View(`EditorViewportGizmo`)는 ImGuizmo 로 조작하며 드래그 중간 값(그룹 행렬 · 상대 행렬)을
 *          들고, 프레임마다 바뀐 월드 행렬을 명령 하나로 보낸다. 적용 · 기록은 메인 스레드의 이 쪽이 한다(부르는 쪽이 View 그리기 뒤에 `apply`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorExports.h"

#include "Engine/Foundation/GameObjectHandle.h"

namespace sw::editor
{
    struct EditorGizmoEditUndo;

    class EditorSelection;

    /** @brief 기즈모 대상 하나 — 핸들과 월드 행렬입니다. */
    struct EditorGizmoTarget
    {
        GameObjectHandle _object{};
        float4x4         _world{};
    };

    /** @brief 기즈모가 이번 프레임에 그릴 값입니다(`EditorGizmoEdit::gather`). */
    struct EditorGizmoViewState
    {
        vector<EditorGizmoTarget> _listTarget;             ///< 선택의 최상위 집합 가운데 씬 컴포넌트가 있는 것(자식은 부모를 따라 움직인다 — 유니티 `SelectionMode.TopLevel`)
        int32                     _operation{ 0 };         ///< 워크스페이스 조작 번호(0 이동 · 1 회전 · 2 크기)
        bool                      _bLocalSpace{ false };   ///< NOLINT(sw-bool-flag): 값 집합체 — 로컬 좌표계
        bool                      _bEditsAllowed{ false }; ///< NOLINT(sw-bool-flag): 값 집합체 — 씬 편집이 허용된다(플레이 중이면 막힌다)
        bool                      _bPreviewScope{ false }; ///< NOLINT(sw-bool-flag): 값 집합체 — 미리보기 창의 선택이다(표면 붙이기를 하지 않는다)
    };

    /** @brief 기즈모가 보내는 명령의 종류입니다. */
    enum class EditorGizmoCommandKind : uint8
    {
        Capture, ///< 기즈모를 눌렀다 — `_listTarget` 의 시작 전 스냅샷을 찍는다(되돌리기의 "전")
        Drag,    ///< 끄는 중이다 — 상호작용을 열고, `_listTarget` 이 있으면 그 월드 행렬을 입힌다(하나면 엔진 분해 · 표면 붙이기, 여럿이면 월드 행렬 그대로)
        End,     ///< 놓았거나 끊겼다 — 편집이 허용되면 살아 있는 대상마다 되돌리기를 남기고 닫는다
    };

    /** @brief 기즈모 명령 하나입니다(값만 든다). */
    struct EditorGizmoCommand
    {
        EditorGizmoCommandKind    _kind{ EditorGizmoCommandKind::End };
        vector<EditorGizmoTarget> _listTarget;
        bool                      _bSurfaceSnap{ false }; ///< NOLINT(sw-bool-flag): 값 집합체 — 대상 하나의 이동을 아래 표면에 붙인다
    };

    /**
     * @class EditorGizmoDragTracker
     * @brief 기즈모 View 가 내는 명령의 순서(잡기 → 끌기들 → 놓기)를 정합니다. ImGuizmo 의 누름 · 사용 상태만 받아 ImGui · 씬 없이 테스트한다.
     * @details 끄는 중에 대상이 바뀌면(선택이 바뀌었거나 그룹에서 하나로) 앞 드래그를 놓기로 닫고 새 대상을 잡는다 — 앞 스냅샷이 새 대상에 커밋되거나 새 대상의 끌기가 기록 없이 남지 않는다.
     */
    class SW_EDITOR_API EditorGizmoDragTracker
    {
    public:
        /**
         * @brief 대상 하나의 프레임입니다. @p bPressed 는 기즈모 위를 이번 프레임에 눌렀는지, @p bUsing 은 ImGuizmo 가 끄는 중인지, @p pMovedWorld 는 이번 프레임에 바뀐 월드 행렬(없으면 nullptr)이다.
         */
        void stepSingle( const EditorGizmoTarget& target, bool bPressed, bool bUsing, const float4x4* pMovedWorld, bool bSurfaceSnap, vector<EditorGizmoCommand>& outListCommand );
        /** @brief 그룹을 잡습니다(누른 순간). 앞 드래그는 닫는다. */
        void captureGroup( const vector<EditorGizmoTarget>& listTarget, vector<EditorGizmoCommand>& outListCommand );
        /** @brief 그룹의 프레임입니다 — @p listMovedWorld 는 이번 프레임에 바뀐 대상 행렬(없으면 빈 목록)이다. */
        void stepGroup( bool bUsing, vector<EditorGizmoTarget>&& listMovedWorld, vector<EditorGizmoCommand>& outListCommand );
        /** @brief 진행 중인 드래그(또는 잡아 둔 것)가 있으면 놓기 명령을 냅니다. */
        void end( vector<EditorGizmoCommand>& outListCommand );
        bool isTracking() const { return _bTracking; }
        /** @brief 지금 잡은 대상 하나입니다(그룹이거나 없으면 무효). */
        GameObjectHandle getTrackedObject() const { return _tracked; }

    private:
        void use( bool bUsing, vector<EditorGizmoTarget>&& listMovedWorld, bool bSurfaceSnap, vector<EditorGizmoCommand>& outListCommand );

    private:
        GameObjectHandle _tracked{};          ///< 잡은 대상 하나(그룹이면 무효)
        bool             _bTracking{ false }; ///< NOLINT(sw-bool-flag): 끄는 중이다
        bool             _bCaptured{ false }; ///< NOLINT(sw-bool-flag): 잡기 명령을 냈고 아직 닫지 않았다
    };

    /**
     * @class EditorGizmoEdit
     * @brief 기즈모 드래그의 적용과 기록입니다. 드래그 하나(잡기 → 끌기들 → 놓기)를 그 선택의 범위(메인은 씬 명령, 미리보기는 창의 스택)에 되돌리기 하나로 남깁니다.
     */
    class SW_EDITOR_API EditorGizmoEdit
    {
    public:
        EditorGizmoEdit();
        ~EditorGizmoEdit();
        EditorGizmoEdit( const EditorGizmoEdit& )            = delete;
        EditorGizmoEdit& operator=( const EditorGizmoEdit& ) = delete;

        /** @brief @p selection 의 기즈모 대상 · 조작 설정 · 편집 허용을 @p outState 에 모읍니다. 부르는 쪽이 먼저 씬 변환을 이번 프레임 값으로 맞춘다(`flushSceneTransforms`). */
        static void gather( const EditorSelection& selection, EditorGizmoViewState& outState );
        /**
         * @brief 기즈모가 보낸 명령들을 차례로 적용합니다. 지난 명령의 선택과 다른 @p selection 이면 앞 드래그를 그 선택의 범위에 먼저 정리한다.
         * @details @p selection 은 이 객체보다 오래 살거나 그 전에 `endDrag` 가 불려야 한다(미리보기 창은 기즈모와 선택을 함께 가진다).
         */
        void apply( EditorSelection& selection, const vector<EditorGizmoCommand>& listCommand );
        /**
         * @brief 진행 중인 드래그를 정리합니다 — 편집이 허용되고 대상이 살아 있으면 스냅샷을 찍은 그 대상에만 되돌리기를 남기고, 아니면 버린다.
         * @details 정리하지 않으면 다음에 다른 것을 고른 첫 드래그가 앞 스냅샷을 커밋해 엉뚱한 "Gizmo Transform" 이 생기고 다시 하기가 잘린다.
         */
        void endDrag();

    private:
        void openStackInteraction();
        void applyWorlds( const EditorGizmoCommand& command ) const;

    private:
        unique_ptr<EditorGizmoEditUndo> _pUndo;      ///< 시작 전 스냅샷과 열린 상호작용 — 되돌리기 형식은 .cpp 만 본다
        vector<GameObjectHandle>        _listObject; ///< 스냅샷을 찍은 대상(`_pUndo->_listBefore` 와 같은 순서)
        EditorSelection*                _pTarget;    ///< 지금 드래그의 선택(빌림)
        bool                            _bDragged;   ///< NOLINT(sw-bool-flag): 잡은 뒤 끌기 명령을 받았다 — 끌지 않은 잡기는 기록하지 않는다(예전처럼 기즈모가 잡은 드래그만 남긴다)
    };
} // namespace sw::editor
