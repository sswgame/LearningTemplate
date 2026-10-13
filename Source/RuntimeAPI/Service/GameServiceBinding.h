/**
 * @file GameServiceBinding.h
 * @brief 게임 모듈의 서비스 조회를 App 이 넘긴 호스트 서비스 범위에 연결하는 선언과 그 export 매크로입니다.
 * @details 정의는 GameFramework 가 가집니다(`GameFramework/Base/Foundation/Framework/GameService.cpp`).
 *          `Export/GameModuleExports.h` 의 진입점 매크로가 이 선언만 보고 부르므로 RuntimeAPI 는 GameFramework 를 include 하지 않습니다.
 */
#pragma once
#include "Core/Subsystem/SubsystemScope.h"

#include "RuntimeAPI/Export/GameFrameworkAPI.h"

/** @brief 게임 서비스 함수의 내보내기 매크로입니다. GameFramework 와 같은 규칙이라 `SW_GF_API` 를 그대로 쓴다. */
#define SW_GAMESERVICE_API SW_GF_API

namespace sw
{
    namespace game
    {
        // 진입점 매크로(RuntimeAPI)가 부르는 경계 함수라 RuntimeAPI 의 다른 경계처럼 C 링크다(이름이 컴파일러 맹글링에 묶이지 않는다).
        extern "C"
        {
            /** @brief App 이 넘긴 호스트 서비스 범위를 게임 서비스 조회에 연결합니다. 범위는 `unbindGameService` 까지 살아 있어야 합니다(호스트가 소유). */
            SW_GAMESERVICE_API void bindGameService( const ISubsystemLocator& hostServices );
            /** @brief 게임 서비스 로케이터의 연결을 끊습니다. 모듈을 언로드하기 전에 부릅니다. */
            SW_GAMESERVICE_API void unbindGameService();
        }
    } // namespace game
} // namespace sw
