/**
 * @file HistoryViewModel.h
 * @brief 히스토리 창의 ViewModel — 에디터 범위의 되돌리기 기록(`CommandStack`)에서 단계 줄을 모으고(`gather`) 되돌리기 · 다시 실행 · 이동 · 지우기 명령을 받습니다. ImGui 를 모릅니다.
 * @details 명령은 기록 자체를 움직이는 일이라 새 단계를 남기지 않는다(언리얼 Undo History · 유니티 Undo History 창과 같다). `docs/agents/EditorUI.md` MVVM 계약.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 기록의 한 단계입니다(값만). 단계 0 은 처음 상태다. */
    struct HistoryStepRow
    {
        string _label;     ///< 명령 이름(비면 "Command")
        uint32 _step{ 0 }; ///< 1 부터
    };

    /** @brief 히스토리 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct HistoryViewState
    {
        vector<HistoryStepRow> _listStep;               ///< 명령 단계(처음 상태 제외)
        uint32                 _currentStep{ 0 };       ///< 지금 위치(0 = 처음 상태)
        uint8                  _bAvailable{ SW_FALSE }; ///< 되돌리기 기록이 있다(종료 중에는 먼저 풀린다)
        uint8                  _bCanUndo{ SW_FALSE };
        uint8                  _bCanRedo{ SW_FALSE };
    };

    /** @brief 히스토리 창의 ViewModel 입니다. */
    class HistoryViewModel
    {
    public:
        /** @brief 지금 되돌리기 기록에서 창이 그릴 값을 모읍니다(@p outState 를 비우고 채운다). */
        void gather( HistoryViewState& outState ) const;
        void undo();
        void redo();
        /** @brief 단계 @p step(0 = 처음 상태)으로 되돌리거나 다시 실행합니다. */
        void jumpTo( uint32 step );
        /** @brief 기록을 지웁니다(되돌릴 수 없다). */
        void clear();
    };
} // namespace sw::editor
