#pragma once
#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Base/DataPath.h"

namespace sw
{
    /**
     * @brief 돌리는 게임의 프리셋(`Config/Game/<게임>.json`)입니다 — 팩을 찾는 팩 루트 하나만 가집니다.
     * @details 게임의 나머지 설정은 팩의 `data/gamesettings.xml` 하나가 원본이다(엔진이 읽는 칸은 `<Engine>` 요소 — `PackEngineSettings`, 시작 씬 등은
     *          `GameSettings`). Dev 는 실행할 때 고른 게임(`-game=`, 없으면 빌드의 활성 게임)의 파일을 읽는다(`GameSelection::getConfigPath`).
     *          Shipping 은 베이크한 게임의 파일을 생성 헤더로 베이크해 넣어 디스크의 Config/Game 을 요구하지 않습니다.
     *          팩 루트는 `DataPath` 라 데이터 읽기만 넣는다 — 파일 없이 만든 `GameConfig{}` 는 쓸 수 있는 값이 아니다(읽기 전에 쓰면 Debug 어서션).
     */
    REFLECT()
    struct SW_API GameConfig : IConfig
    {
        REFLECT_BODY();

        PROPERTY( Required )
        DataPath _packRoot; ///< 활성 게임 팩의 리소스 경로(`game/<팩 폴더>`) — 팩 마운트 · 게임 도메인 경로가 이것으로 풀린다

        /**
         * @brief 서비스 표의 게임 프리셋(`engine::getSubsystem<GameConfig>()`)을 돌려주는 전달 창구입니다 - 사본이 아니다.
         * @details 소유자는 `ConfigManager` 이고 값은 거기서 제자리로 바뀐다. 서비스 표 헤더(`EngineServices.h`)를 들일 수 없는 에디터 · 게임 프레임워크 호출처가 쓴다.
         *          그쪽이 `editor::` · `game::` 범위 창구(`findSubsystem<GameConfig>()`)로 옮기면 이 함수를 지운다(`CheckServiceAccess` 의 이전 대기 예외).
         */
        static const GameConfig& getActive();
    };
} // namespace sw
