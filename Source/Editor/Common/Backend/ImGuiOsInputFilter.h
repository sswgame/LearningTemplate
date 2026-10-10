/**
 * @file ImGuiOsInputFilter.h
 * @brief 무인 자동화 동안 플랫폼 백엔드가 ImGui 입력 큐에 넣는 사람의 장치 사건을 지웁니다.
 * @details 엔진 입력 층(`InputManager::shouldDropNativeEvent`)은 창 메시지를 막지만, ImGui 플랫폼 백엔드는 메시지 없이도 실제 커서를
 *          묻습니다(Win32 `ImGui_ImplWin32_NewFrame` 의 `GetCursorPos` · 커서 아래 뷰포트, X11 `XQueryPointer`). 그 호출을 이 범위로 감쌉니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw::editor
{
    /**
     * @class ScopedImGuiOsInputFilter
     * @brief 켜진 채 만들면, 범위 안에서 ImGui 입력 큐에 들어온 장치 사건(마우스 위치 · 단추 · 휠 · 커서 아래 뷰포트 · 키 · 글자)을 범위가 끝날 때 지웁니다.
     * @details 포커스 사건은 창 사건이라 남깁니다. 플랫폼 백엔드 호출만 감쌉니다 — 자동화 입력(`EditorSelfTestInput::flushIntoImGui`)은 범위 밖에서 넣습니다.
     *          ImGui 컨텍스트가 없으면 아무것도 하지 않습니다.
     */
    class ScopedImGuiOsInputFilter
    {
    public:
        explicit ScopedImGuiOsInputFilter( bool bActive );
        ~ScopedImGuiOsInputFilter();

        ScopedImGuiOsInputFilter( const ScopedImGuiOsInputFilter& )            = delete;
        ScopedImGuiOsInputFilter& operator=( const ScopedImGuiOsInputFilter& ) = delete;

    private:
        int32 _firstIndex; ///< 범위를 열 때의 큐 길이 — 이 자리부터 지운다. 꺼져 있으면 -1
    };
} // namespace sw::editor
