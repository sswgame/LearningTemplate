/**
 * @file UserSettingsViewModel.h
 * @brief 사용자 설정 창의 ViewModel — 호스트의 사용자 설정 매니저(`UserSettingsManager`)를 찾습니다. ImGui 를 모릅니다.
 * @details 패널(`UserSettingsPanel`)은 작업 공간 헤더(`EditorService`)를 보지 않는다(`docs/agents/EditorUI.md` MVVM 계약). 설정 값은 게임 메뉴가 쓰는 바인딩 API 그대로이고,
 *          플레이어 옵션은 문서가 아니라 되돌리기에 남지 않는다(적용 · 되돌리기 단추가 그 일을 한다 — 언리얼 Game User Settings 와 같다).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    class UserSettingsManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 사용자 설정 창의 ViewModel 입니다. 파일 머리말 참고. */
    class UserSettingsViewModel
    {
    public:
        /** @brief 사용자 설정 매니저입니다. 에디터가 서 있을 때(그리기)만 부른다 — 없으면 멈춘다. */
        static UserSettingsManager& getSettings();
        /** @brief 사용자 설정 매니저입니다. 없으면(설정 없이 도는 테스트 · 종료 뒤) nullptr 입니다. */
        static const UserSettingsManager* findSettings();
    };
} // namespace sw::editor
