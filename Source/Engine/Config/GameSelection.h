#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    class CommandLineManager;

    /**
     * @class GameSelection
     * @brief 이 프로세스가 돌리는 게임입니다 — 이름과 그 게임의 프리셋 · 서버 설정 경로를 줍니다.
     * @details Dev: `-game=<이름>` 이 있으면 그 게임, 없으면 빌드의 활성 게임(`Bin/Modules/ActiveGame.txt`, CMake 가 `SW_ACTIVE_GAME` 으로 쓴다)입니다.
     *          활성 게임이 아니면 게임 모듈을 `Bin/Games/<이름>/` 에서 올린다(`ModuleLibraryUtil::setModuleSubdirectory`) — 같은 빌드가 `SW_GAMES` 로 지은 게임 팩입니다.
     *          Shipping: 빌드 시점에 베이크한 게임(`config::kShippingGameName`) 하나이고 `-game=` 을 보지 않습니다.
     *          언리얼이 프로젝트를 명령줄로 받고 엔진 바이너리를 나눠 쓰는 것과 같은 모양입니다. 게임 이름을 엔진 TU 에 베이크하지 않으므로 Dev 에서 활성 게임을 바꿔도
     *          엔진이 다시 컴파일되지 않습니다.
     * @note `select` 는 기동 스레드에서 한 번, 모듈 · 설정을 읽기 전에 부릅니다(코어 범위의 `CrashHandlerSubsystem`). 고르기 전에 묻는 쪽(테스트)은 기본 게임을 받습니다.
     */
    class SW_API GameSelection
    {
    public:
        /** @brief 명령줄로 게임을 고릅니다. 그 게임이 이 빌드에 없으면(이름 오타, `SW_GAMES` 에 없는 게임) 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool select( const CommandLineManager& commandLine );

        /** @brief 고른 게임 이름입니다. 고르기 전이면 기본 게임(Dev: 빌드의 활성 게임, Shipping: 베이크한 게임)입니다. */
        static string getName();
        /** @brief 고른 게임의 프리셋 경로(`Config/Game/<이름>.json`)입니다. */
        static string getConfigPath();
        /** @brief 고른 게임의 전용 서버 운영 설정 경로(`Config/Server/<이름>.json`)입니다. 파일이 없을 수 있다(서버가 없는 게임). */
        static string getServerConfigPath();
        /** @brief 게임 이름의 최대 길이(끝 0 포함)입니다. */
        static constexpr uint32 kNameCapacity = 64;

    private:
        /** @brief 고르지 않았을 때의 게임입니다 — Dev 는 빌드의 활성 게임(`Bin/Modules/ActiveGame.txt`), Shipping 은 베이크한 게임입니다. 없으면 빈 문자열입니다. */
        static string readDefaultName();
        /** @brief 고른 이름을 적어 둡니다(`select` 가 기동 스레드에서 한 번). */
        static void storeName( string_view name );
    };

    /**
     * @struct LaunchSelection
     * @brief 실행 인자로 고른 "이 프로세스가 처음 무엇으로 들어가는가" 입니다 — 지금은 개발 실행 인자 `-experience=<이름>` 하나입니다.
     * @details 주인은 `ContentRootSubsystem`(게임 선택과 같은 기동 단계)이고 코어 범위가 이 멤버를 서비스 칸으로 걸어 게임 모듈이 읽는다
     *          (`game::getSubsystem<const LaunchSelection>()`) — 명령줄(`CommandLineManager`)은 호스트 전용이라 게임은 이 값으로 받는다.
     *          언리얼이 URL 옵션 `?Experience=` 를 게임 모드가 받는 것과 같은 자리다.
     */
    struct SW_API LaunchSelection
    {
        string _experience; ///< 게임 인스턴스가 첫 씬 대신 들어갈 Experience 이름. 주지 않았거나 배포 구성이면 비었다

        /** @brief 명령줄에서 `-experience=` 를 읽습니다. 배포 구성은 맵의 기본 Experience 로만 들어가므로 보지 않습니다. */
        void read( const CommandLineManager& commandLine );

        SW_SERVICE_SLOT_CONST( LaunchSelection, Required );
    };
} // namespace sw
