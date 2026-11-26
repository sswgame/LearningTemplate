/**
 * @file GameFramework/Client/Flow/GameSessionCommands.h
 * @brief 씬 흐름의 세션 명령(타이틀로)을 UI 전역 명령 표에 거는 일(`GameSessionCommands`)입니다. 구현은 게임 인스턴스의 클라이언트 전용 부분(GameInstanceClient.cpp, `IGameInstanceClientHook` 구현)에 있다.
 * @details 전용 서버는 이 소스를 짓지 않는다.
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameSceneFlow;
    class UIGlobalCommandRegistry;

    /** @struct GameSessionCommands @brief 씬 흐름의 세션 명령을 UI 전역 명령 표에 거는 일입니다. 창 없이 세우는 테스트가 표를 직접 준다. */
    struct SW_GF_API GameSessionCommands
    {
        /** @brief 타이틀 씬이 있으면 @p commands 에 `ReturnToTitle` 을 걸고 그 구독을 @p flow 에 맡깁니다(`GameSceneFlow::unregisterSessionCommands` 가 거둔다). */
        static void bind( GameSceneFlow& flow, UIGlobalCommandRegistry& commands );
    };
} // namespace sw
