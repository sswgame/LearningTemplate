#pragma once
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Config/PackCookRuleTypes.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw
{
    /**
     * @brief 돌리는 게임의 프리셋(`Config/Game/<게임>.json`)입니다 — 팩을 찾는 팩 루트와, 팩 쿠커가 엔진 규칙 뒤에 합치는 그 게임의 쿠킹 규칙을 가집니다.
     * @details 게임의 나머지 설정은 팩의 `data/gamesettings.xml` 하나가 원본이다(엔진이 읽는 칸은 `<Engine>` 요소 — `PackEngineSettings`, 시작 씬 등은
     *          `GameSettings`). Dev 는 실행할 때 고른 게임(`-game=`, 없으면 빌드의 활성 게임)의 파일을 읽는다(`GameSelection::getConfigPath`).
     *          Shipping 은 베이크한 게임의 파일을 생성 헤더로 베이크해 넣어 디스크의 Config/Game 을 요구하지 않습니다.
     *          팩 루트는 `DataPath` 라 데이터 읽기만 넣는다 — 파일 없이 만든 `GameConfig{}` 는 쓸 수 있는 값이 아니다(읽기 전에 쓰면 Debug 어서션).
     */
    REFLECT()
    struct SW_API GameConfig : IConfig
    {
        REFLECT_BODY();

        /** @brief 게임 프리셋 파일들이 있는 폴더입니다(프로젝트 루트 기준). 팩 쿠커는 굽는 게임 팩마다 여기서 그 게임의 쿠킹 규칙을 찾는다. */
        static constexpr const utf8* kConfigFolder = "Config/Game";

        PROPERTY( Required )
        DataPath _packRoot; ///< 활성 게임 팩의 리소스 경로(`game/<팩 폴더>`) — 팩 마운트 · 게임 도메인 경로가 이것으로 풀린다

        PROPERTY()
        GameCookRules _cookRules; ///< 팩 쿠커가 엔진 쿠킹 규칙 뒤에 합치는 이 게임의 규칙(`PackCookRules::mergeGameRules`). 쿠킹만 읽는다
        // 정적 창구는 두지 않는다 — 소유자는 `ConfigManager` 이고 쓰는 쪽이 범위 창구(`engine::` · `editor::` · `game::getSubsystem<const GameConfig>()`)로 찾는다.

        SW_SERVICE_SLOT_CONST( GameConfig, Required );
    };
} // namespace sw
