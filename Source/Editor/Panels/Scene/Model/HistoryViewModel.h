/**
 * @file HistoryViewModel.h
 * @brief 히스토리 창의 ViewModel — 되돌리기 스택의 지금 위치 · 명령 이름을 모으고(`gather`), 실행 취소 · 다시 실행 · 지우기 · 그 시점으로 가기를 받습니다. ImGui 를 모릅니다.
 * @details 패널(`HistoryPanel`)은 작업 공간 헤더(`CommandStack` · `EditorService`)를 보지 않는다(`docs/agents/EditorUI.md` MVVM 계약, 언리얼 Undo History 의 트랜잭션 목록과 Slate 행이 나뉜 것과 같다).
 *          명령은 되돌리기 스택 자체를 움직이는 일이라 스스로 기록을 남기지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 히스토리 창이 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct HistoryViewState
    {
        vector<string> _listLabel{};         ///< 명령 이름(1 번 단계부터). 이름이 없는 명령은 "Command"
        size_t         _currentIndex{ 0 };   ///< 지금 위치(0 은 처음 상태, n 은 n 번 단계까지 실행)
        bool           _bAvailable{ false }; ///< 되돌리기 스택이 있다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool           _bCanUndo{ false };   ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool           _bCanRedo{ false };   ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /** @brief 히스토리 창의 ViewModel 입니다. 파일 머리말 참고. 상태가 없어 패널이 값으로 든다. */
    class HistoryViewModel
    {
    public:
        /**
         * @brief 되돌리기 스택의 지금 값을 @p outState 에 채웁니다(벡터는 재사용한다). 스택이 없으면 `_bAvailable` 이 false 입니다.
         * @details 스택은 선택 서비스다 — EditorModule 보다 오래 살아 종료할 때 결합이 먼저 풀린다.
         */
        void gather( HistoryViewState& outState ) const;

        void undo();
        void redo();
        /** @brief 기록을 모두 지웁니다. */
        void clear();
        /** @brief @p stepIndex 시점(0 은 처음 상태)으로 되돌리거나 다시 실행합니다. */
        void jumpTo( size_t stepIndex );
    };
} // namespace sw::editor
