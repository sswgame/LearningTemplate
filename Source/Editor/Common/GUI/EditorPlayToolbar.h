/**
 * @file EditorPlayToolbar.h
 * @brief 메뉴 막대 아래 에디터 상단 툴바 — Play · Simulate · Pause · Step · Stop · 카메라에서 시작 · 시간 배율 · 자동 플레이입니다.
 * @details 유니티 · 언리얼처럼 재생 단추는 어느 뷰를 열어 두든 같은 자리에 있습니다. 씬 뷰 · 게임 뷰 패널에는 없습니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor
{
    /**
     * @class EditorPlayToolbar
     * @brief 에디터 상단 툴바입니다. 메인 뷰포트 위쪽 가장자리에 붙어 도크 영역을 그만큼 줄입니다(`ImGui::BeginViewportSideBar`).
     */
    class EditorPlayToolbar
    {
    public:
        /** @brief 툴바를 그립니다. 메뉴 막대 뒤 · 도크스페이스 앞에서 부릅니다. */
        static void draw();
        /** @brief 마지막 프레임에 자동 플레이 단추를 그렸으면 true 입니다(에디터 자체 시험 `toolbar.autoplayButton` 이 읽습니다). */
        static bool wasAutoplayButtonDrawn();
    };
} // namespace sw::editor
