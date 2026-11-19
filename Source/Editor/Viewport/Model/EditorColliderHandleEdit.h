/**
 * @file EditorColliderHandleEdit.h
 * @brief 콜라이더 크기 손잡이의 Model 쪽입니다 — 대상 강체의 손잡이를 값으로 모으고, 손잡이 도구(View)가 보낸 명령(잡기 · 끌기 · 놓기)을 적용해 끌기 하나를 되돌리기 하나로 남깁니다.
 * @details 트랜스폼 기즈모(`EditorGizmoEdit`)와 같은 모양이다 — 언리얼 컴포넌트 비주얼라이저가 손잡이 끌기를 트랜잭션(`FScopedTransaction`)으로 감싸는 자리. ImGui 를 모릅니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Ray.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Viewport/Model/EditorColliderHandle.h"
#include "Editor/Viewport/Model/EditorHandleDrag.h"

#include "Engine/Foundation/GameObjectHandle.h"

namespace sw
{
    struct ObjectSnapshot;
} // namespace sw

namespace sw::editor
{
    /** @brief 손잡이 도구가 이번 프레임에 그릴 값입니다(`EditorColliderHandleEdit::gather`). */
    struct EditorColliderHandleViewState
    {
        GameObjectHandle       _object{};            ///< 손잡이의 강체 오브젝트
        vector<ColliderHandle> _listHandle;          ///< 셰이프마다의 손잡이(월드)
        bool                   _bAvailable{ false }; ///< NOLINT(sw-bool-flag): 값 집합체 — 강체가 있고 편집이 허용되며 배율이 0 이 아니다
    };

    /** @brief 콜라이더 손잡이 명령 · 순서 — 공통 틀(`EditorHandleDrag.h`)에 손잡이 값 `ColliderHandle` 을 넣은 것이다. */
    using EditorColliderHandleCommandKind = EditorHandleDragCommandKind;
    using EditorColliderHandleCommand     = EditorHandleDragCommand<ColliderHandle>;
    using EditorColliderHandleDragTracker = EditorHandleDragTracker<ColliderHandle>;

    /**
     * @class EditorColliderHandleEdit
     * @brief 손잡이 끌기의 적용과 기록입니다(메인 씬 — 메인 선택의 강체).
     */
    class SW_EDITOR_API EditorColliderHandleEdit
    {
    public:
        EditorColliderHandleEdit();
        ~EditorColliderHandleEdit();
        EditorColliderHandleEdit( const EditorColliderHandleEdit& )            = delete;
        EditorColliderHandleEdit& operator=( const EditorColliderHandleEdit& ) = delete;

        /**
         * @brief 손잡이 대상(끄는 중이면 @p dragged, 아니면 메인 선택이 하나일 때 그 오브젝트)의 강체 손잡이를 @p outState 에 모읍니다.
         * @details 플레이 중이면 편집이 막혀 대상이 없다(`_bAvailable` false). 배율 0 이면 잡을 셰이프가 없다.
         */
        static void gather( GameObjectHandle dragged, EditorColliderHandleViewState& outState );
        /** @brief 명령들을 차례로 적용합니다. */
        void apply( const vector<EditorColliderHandleCommand>& listCommand );
        /** @brief 끌기를 마치고 되돌리기를 남긴 횟수입니다(감시 값 `Editor.ColliderHandleCommitCount`). */
        uint32 getCommitCount() const { return _commitCount; }

    private:
        void capture( GameObjectHandle object );
        void drag( const EditorColliderHandleCommand& command );
        void end( bool bCommit );

    private:
        unique_ptr<ObjectSnapshot> _pBefore;     ///< 끌기 전 스냅샷 — 형식은 .cpp 만 본다
        GameObjectHandle           _object;      ///< 스냅샷을 찍은 대상
        uint32                     _commitCount; ///< 되돌리기를 남긴 끌기 수
        bool                       _bChanged;    ///< NOLINT(sw-bool-flag): 잡은 뒤 셰이프가 바뀌었다 — 바뀌지 않은 끌기는 기록하지 않는다
    };

    /**
     * @struct EditorColliderHandleSession
     * @brief 손잡이 도구 한 벌(명령 순서 · 적용 · 이번 프레임 값)입니다. 도구는 시각화 등록 줄의 정적 함수라 에디터 범위의 상태 저장소가 소유한다(`get`).
     */
    struct SW_EDITOR_API EditorColliderHandleSession
    {
        EditorColliderHandleDragTracker     _tracker;
        EditorColliderHandleEdit            _edit;
        EditorColliderHandleViewState       _state;
        vector<EditorColliderHandleCommand> _listCommand;           ///< 이번 프레임에 낸 명령(재사용 버퍼)
        int32                               _frame{ 0 };            ///< 지난 `process` 의 ImGui 프레임(`_bCapturing` 이 거짓이면 보지 않는다)
        uint32                              _drawnHandleCount{ 0 }; ///< 그 프레임에 그린 손잡이 수(감시 값 `Editor.ColliderHandleCount`)
        bool                                _bCapturing{ false };   ///< NOLINT(sw-bool-flag): 그 프레임에 손잡이가 마우스를 가져갔다

        /** @brief 지금 에디터 범위의 손잡이 도구입니다. */
        static EditorColliderHandleSession& get();
    };
} // namespace sw::editor
