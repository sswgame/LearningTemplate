/**
 * @file EditorStatusViewModel.h
 * @brief 상태줄 항목(에셋 임포트 · 프로브 베이크)과 토스트 알림 View 의 ViewModel 입니다. ImGui 를 모릅니다.
 * @details 항목 View 는 세션 · 핫 리로드 헤더를 보지 않는다(`docs/agents/EditorUI.md` MVVM) — 값으로 모으고, 취소 · 탐색기 열기는 명령으로 보낸다. 하는 일이 없으면 모으기가 false 다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 워커에서 도는 에셋 임포트의 수입니다. */
    struct EditorAssetImportStatus
    {
        uint32 _runningCount{ 0 };
        uint32 _queuedCount{ 0 };
    };

    /** @brief 프로브 베이크의 진행입니다. */
    struct EditorProbeBakeStatus
    {
        uint32  _done{ 0 };            ///< 지금 하는(라이트 프로브 볼륨, 1 부터) 또는 끝난(반사 프로브) 수
        uint32  _total{ 0 };           ///< 모두 몇 개인지
        float32 _itemProgress{ 0.0f }; ///< 지금 항목의 진행(0..1, 라이트 프로브만)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorStatusViewModel
     * @brief 상태줄 항목 · 알림이 보일 값과 명령입니다. 파일 머리말 참고.
     */
    struct EditorStatusViewModel
    {
        /** @brief 임포트가 돌고 있으면 그 수를 @p outStatus 에 담고 true 입니다. */
        [[nodiscard]] static bool gatherAssetImport( EditorAssetImportStatus& outStatus );
        /** @brief 라이트 프로브를 베이크하고 있으면 진행을 @p outStatus 에 담고 true 입니다. */
        [[nodiscard]] static bool gatherLightProbeBake( EditorProbeBakeStatus& outStatus );
        /** @brief 반사 프로브를 베이크하고 있으면 진행을 @p outStatus 에 담고 true 입니다. */
        [[nodiscard]] static bool gatherReflectionProbeBake( EditorProbeBakeStatus& outStatus );
        /** @brief 라이트 프로브 베이크를 취소합니다. 돌고 있지 않으면 아무것도 하지 않습니다. */
        static void cancelLightProbeBake();
        /** @brief 반사 프로브 베이크를 취소합니다. 돌고 있지 않으면 아무것도 하지 않습니다. */
        static void cancelReflectionProbeBake();
        /** @brief 절대 경로 @p absolutePath 를 파일 탐색기에서 고릅니다(토스트의 Show in Explorer). 실패는 명령이 알린다. */
        static void showInFileExplorer( string_view absolutePath );
    };
} // namespace sw::editor
