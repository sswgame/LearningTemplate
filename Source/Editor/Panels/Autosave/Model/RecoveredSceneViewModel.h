/**
 * @file RecoveredSceneViewModel.h
 * @brief 복구 창의 ViewModel — 자동 저장 복구 후보를 줄 값으로 모으고(`gather`) 복원 · 폐기 명령을 받습니다. ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 후보와 그 일은 `EditorAutosaveCommands` 가 든다. 복원은 사본을 새 씬처럼 열고(되돌리기 기록은 빈 채로 시작한다), 폐기는 사본 파일을 지운다 —
 *          둘 다 열린 씬 문서를 고치는 편집이 아니라 되돌리기에 남지 않는다(언리얼 Restore Packages 대화상자와 같다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 복구 후보 한 줄입니다(값만). */
    struct RecoveredSceneRow
    {
        string _sceneID;        ///< 원래 씬의 리소스 id(저장한 적 없는 새 씬은 표시 이름)
        string _savedAge;       ///< 사본을 쓴 지 얼마나 됐는지("4 min ago", 모르면 "-")
        uint32 _copyCount{ 0 }; ///< 남은 세대 수
        string _refusal;        ///< 복원할 수 없는 이유(비면 복원할 수 있다)
    };

    /** @brief 문서 패널의 복구 후보 한 줄입니다(값만 — 감사 E-1). */
    struct RecoveredDocumentRow
    {
        string _documentPath; ///< 문서의 리소스 id
        string _savedAge;     ///< 사본을 쓴 지 얼마나 됐는지("4 min ago", 모르면 "-")
        string _refusal;      ///< 복원할 수 없는 이유(비면 복원할 수 있다)
    };

    /** @brief 복구 창이 한 프레임 그릴 값입니다. 줄 번호가 명령의 대상이다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct RecoveredSceneViewState
    {
        vector<RecoveredSceneRow>    _listRow{};
        vector<RecoveredDocumentRow> _listDocumentRow{}; ///< 문서 패널의 사본(씬 줄 아래에 따로 보인다)
    };

    /** @brief 복구 창의 ViewModel 입니다. 상태가 없다(후보는 `EditorAutosaveCommands` 가 든다). */
    class RecoveredSceneViewModel
    {
    public:
        /** @brief 복구 후보를 @p outState 에 채웁니다(비우고 채운다). */
        static void gather( RecoveredSceneViewState& outState );
        /** @brief 줄 @p rowIndex 의 사본 복원을 요청합니다(`EditorAutosaveCommands::requestRestore` — 여는 일은 게임 단계, 거절 이유는 그때 알린다). */
        static void restore( uint32 rowIndex );
        /** @brief 줄 @p rowIndex 의 사본을 지웁니다. */
        static void discard( uint32 rowIndex );
        /** @brief 문서 줄 @p rowIndex 의 사본을 복원합니다 — 문서를 열고 사본 글을 저장하지 않은 편집으로 적용한다. 거절 이유는 `EditorAutosaveCommands::restoreDocument` 가 알린다. */
        static void restoreDocument( uint32 rowIndex );
        /** @brief 문서 줄 @p rowIndex 의 사본을 지웁니다. */
        static void discardDocument( uint32 rowIndex );
        /** @brief 사본을 쓴 시각 @p savedTicks 가 지금 @p nowTicks 에서 얼마나 지났는지 사람이 읽는 글로 냅니다(`FileUtil::getCurrentFileWriteTime` 눈금). 시각을 모르면 "-" 입니다. */
        static string formatSavedAge( int64 savedTicks, int64 nowTicks );
    };
} // namespace sw::editor
