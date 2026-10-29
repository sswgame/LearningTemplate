#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/Undo/ObjectUndoUtil.h"

namespace sw
{
    class CommandStack;
    class GameObject;
} // namespace sw

namespace sw::editor
{
    using EditorDocumentRestoreDelegate = Delegate<void( string_view )>;
    using EditorDocumentCaptureDelegate = Delegate<string()>;

    /**
     * @class EditorTransaction
     * @brief GameObject 편집을 Undo/Redo 에 기록하는 트랜잭션 관리자입니다.
     * @details 오브젝트 편집(수정 · 생성 · 삭제)은 데이터 명령(`ObjectUndoUtil`)으로 기록해 에디터 모듈 핫 리로드를 넘깁니다. 열린 문서 편집
     *          (`recordDocumentText`)은 패널 버퍼에 묶인 코드 단계라 기록을 맡길 때 버립니다.
     */
    class SW_EDITOR_API EditorTransaction
    {
    public:
        /** @brief 오브젝트 수명 편집의 방향입니다(엔진의 `ObjectLifetimeEdit`). */
        using ObjectLifetimeEdit = sw::ObjectLifetimeEdit;

        /** @brief 복합 트랜잭션을 시작합니다. */
        static void beginTransaction( string_view label = "" );
        /** @brief 복합 트랜잭션을 커밋하고 종료합니다. */
        static void endTransaction();
        /** @brief 복합 트랜잭션을 취소하고 버립니다. */
        static void cancelTransaction();

        /** @brief 오브젝트 하나의 수정 전후 상태를 Undo/Redo 에 기록합니다. 전후 XML 이 같으면 기록하지 않습니다. */
        static void recordModify( GameObject* pObj, const ObjectSnapshot& before, const ObjectSnapshot& after,
                                  string_view label = "Modify GameObject" );

        /** @brief 게임오브젝트 생성을 Undo/Redo에 등록합니다. */
        static void recordCreation( GameObject* pObj, string_view label = "Create GameObject" );

        /** @brief 게임오브젝트 삭제를 Undo/Redo에 등록합니다 (삭제 전 스냅샷 보존). */
        static void recordDestruction( GameObject* pObj, string_view label = "Delete GameObject" );

        /** @brief 현재 게임오브젝트의 전체 상태를 XML 스냅샷으로 캡처합니다(런타임 id 포함). nullptr 이면 빈 스냅샷입니다. */
        static ObjectSnapshot captureSnapshot( const GameObject* pObj );

        /** @brief 생성·삭제를 한 절차로 기록합니다. 두 절차를 만들고 @p edit 이 순서를 정합니다. */
        static void recordObjectLifetime( GameObject* pObj, string_view label, ObjectLifetimeEdit edit );

        /**
         * @brief 되돌리기 · 다시 하기 단계를 스택에 올립니다. 대상이 씬 · 파일이면 데이터 단계(`SW_UNDO_STEP` — 리로드를 넘는다), 패널 인스턴스에만 있으면
         *        코드 단계(`UndoStep::makeTransient` — 기록을 맡길 때 버린다)다.
         */
        static void push( UndoStep undo, UndoStep redo, string_view label, string_view coalesceKey = {} );
        /** @brief 문서 변경 구간만 Undo/Redo에 등록합니다. capture가 있으면 현재 본문에서 복원합니다. */
        static void recordDocumentText( string_view beforeText, string_view afterText, string_view label,
                                        const EditorDocumentRestoreDelegate& restore, string_view coalesceKey = {} );
        static void recordDocumentText( string_view beforeText, string_view afterText, string_view label,
                                        const EditorDocumentRestoreDelegate& restore, const EditorDocumentCaptureDelegate& capture,
                                        string_view coalesceKey = {} );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorObjectEditBindings
     * @brief 데이터 명령의 알림(`CommandStack::ObjectEditNotice`)을 이 모듈이 받게 거는 주인입니다 — 선택을 맞추고 씬을 dirty 로 표시합니다.
     * @details 스택의 알림 칸은 주인이 하나다(`DelegateSlot`). 이 서브시스템(에디터 범위)이 스택마다의 구독을 쥐고, 범위가 내려가면 함께 놓는다.
     *          에디터가 설 때 전역 스택에, 오브젝트 편집을 기록할 때 그 스택(미리보기 창의 스택 포함)에 겁니다. 모듈 언로드 훑기가 칸을 비웠으면 다시 겁니다 —
     *          리로드 전에 기록한 명령도 새 모듈의 처리기로 알린다. 에디터 컨텍스트 없이 도는 테스트는 커맨드 스택 서비스와 함께 이것을 범위에 둔다.
     */
    class SW_EDITOR_API EditorObjectEditBindings final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorObjectEditBindings";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        void                         shutdownSubsystem() override { _listSubscription.clear(); }

        /** @brief @p stack 의 알림 칸이 비어 있으면 이 모듈의 처리기를 겁니다(이미 걸려 있으면 아무것도 하지 않는다). */
        void ensureBound( CommandStack& stack );

    private:
        vector<Subscription> _listSubscription; ///< 스택마다의 구독(스택이 먼저 사라지면 무효가 되고 다음 걸 때 걷힌다)
    };
} // namespace sw::editor
