/**
 * @file ControlEvents.h
 * @brief 빙의 이벤트 — 플레이어 조종자가 폰을 바꿔 쥐면 "game" 채널(`GameEventUtil::send`)에 냅니다. HUD · 화면 전환이 듣습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Event/EventType.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 로컬 플레이어의 빙의가 바뀌었습니다(`PlayerControllerComponent`). 놓기만 했으면 `_pawn` 이 무효입니다. */
    struct SW_GF_API PossessionChangedEvent final : IEvent
    {
        GameObjectHandle _controller{};   ///< 플레이어 조종자의 오브젝트
        GameObjectHandle _previousPawn{}; ///< 앞서 쥐었던 폰의 오브젝트(없으면 무효)
        GameObjectHandle _pawn{};         ///< 지금 쥔 폰의 오브젝트(놓았으면 무효)
        uint32           _playerIndex{ 0 };
        SW_DECLARE_GAMEPLAY_EVENT( PossessionChangedEvent );
    };
} // namespace sw
