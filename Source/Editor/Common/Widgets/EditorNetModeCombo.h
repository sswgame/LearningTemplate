/**
 * @file EditorNetModeCombo.h
 * @brief 툴바와 Multiplayer 창이 같이 그리는 Net Mode 고르기(Standalone · 전용 서버 · 리슨 호스트 + 플레이어 수)입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    class EditorNetPlaySession;

    /**
     * @struct EditorNetModeCombo
     * @brief Net Mode 드롭다운과 플레이어 수 입력입니다. 툴바(GUI 층)가 패널(Panels 층)을 include 하지 않도록 위젯 층에 둡니다.
     */
    struct SW_EDITOR_API EditorNetModeCombo
    {
        /** @brief 플레이어 수 입력의 상한입니다. */
        static constexpr int32 kMaxPlayerCount = 8;

        /** @brief 구성을 고르는 위젯을 그립니다. 세션이 도는 동안은 잠깁니다. 바꿨으면 true 입니다(바뀐 구성은 다음 Play 에 쓰인다). */
        static bool draw( EditorNetPlaySession& session );
    };
} // namespace sw::editor
