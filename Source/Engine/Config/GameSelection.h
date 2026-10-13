#pragma once
#include "Core/Container/string.h"

namespace sw
{
    class CommandLineManager;

    /**
     * @class GameSelection
     * @brief 이 프로세스가 돌리는 게임입니다 — 이름과 그 게임의 프리셋 · 서버 설정 경로를 줍니다.
     * @details Dev: `-game=<이름>` 이 있으면 그 게임, 없으면 빌드의 활성 게임(`Bin/Modules/ActiveGame.txt`, CMake 가 `SW_ACTIVE_GAME` 으로 쓴다)입니다.
     *          활성 게임이 아니면 게임 모듈을 `Bin/Games/<이름>/` 에서 올린다(`ModuleImageUtil::setModuleSubdirectory`) — 같은 빌드가 `SW_GAMES` 로 지은 게임 팩입니다.
     *          Shipping: 빌드 시점에 구운 게임(`config::kShippingGameName`) 하나이고 `-game=` 을 보지 않습니다.
     *          언리얼이 프로젝트를 명령줄로 받고 엔진 바이너리를 나눠 쓰는 것과 같은 모양입니다. 게임 이름을 엔진 TU 에 굽지 않으므로 Dev 에서 활성 게임을 바꿔도
     *          엔진이 다시 컴파일되지 않습니다.
     * @note `select` 는 기동 스레드에서 한 번, 모듈 · 설정을 읽기 전에 부릅니다(`EngineBootstrap`). 고르기 전에 묻는 쪽(테스트)은 기본 게임을 받습니다.
     */
    class SW_API GameSelection
    {
    public:
        /** @brief 명령줄로 게임을 고릅니다. 그 게임이 이 빌드에 없으면(이름 오타, `SW_GAMES` 에 없는 게임) 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool select( const CommandLineManager& commandLine );

        /** @brief 고른 게임 이름입니다. 고르기 전이면 기본 게임(Dev: 빌드의 활성 게임, Shipping: 구운 게임)입니다. */
        static string getName();
        /** @brief 고른 게임의 프리셋 경로(`Config/Game/<이름>.json`)입니다. */
        static string getConfigPath();
        /** @brief 고른 게임의 전용 서버 운영 설정 경로(`Config/Server/<이름>.json`)입니다. 파일이 없을 수 있다(서버가 없는 게임). */
        static string getServerConfigPath();
    };
} // namespace sw
