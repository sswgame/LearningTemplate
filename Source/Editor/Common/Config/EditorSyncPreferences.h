/**
 * @file EditorSyncPreferences.h
 * @brief 멀티플레이 창 Sync 탭의 환경설정 섹션(Editor/Multiplayer Sync)입니다. 등록은 `MultiplayerSyncTab.cpp` 가 `SW_EDITOR_SETTINGS` 로 합니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
{
    /** @brief Sync 탭 — 클라이언트에게 물은 질의의 기다림 상한입니다. */
    REFLECT()
    struct EditorSyncPreferences
    {
        REFLECT_BODY();

        PROPERTY( Min = 1.0, Tooltip = "Seconds the Sync tab waits for a client's answer before it shows a timeout error (a client answers an invalid question with an error line, which is never counted as an answer)" )
        float32 _clientReplyTimeoutSeconds{ 10.0f };
    };
} // namespace sw::editor
