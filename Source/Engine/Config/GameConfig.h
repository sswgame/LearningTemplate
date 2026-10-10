#pragma once
#include "Core/Container/map.h"
#include "Core/Container/string.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief 활성 게임의 프리셋(`Config/Game/<SW_ACTIVE_GAME>.json`)입니다 — 팩을 마운트하기 전에 알아야 하는 것(팩 루트 · 스키마)만. 시작 씬은 팩의 `data/gamesettings.xml` 하나다.
     * @details 빌드가 `SW_ACTIVE_GAME` 으로 프리셋 파일을 고른다(`config::kFileRuntimeGameConfig`). Shipping 은 그 파일을 생성 헤더로 구워 넣어
     *          디스크의 Config/Game 을 요구하지 않습니다. 게임마다 하나라, 게임을 바꿔도 다른 게임의 팩 루트를 읽지 않습니다.
     */
    REFLECT()
    struct SW_API GameConfig : IConfig
    {
        REFLECT_BODY();

        PROPERTY()
        string _packRoot{}; ///< 활성 게임 팩의 리소스 경로(`game/<팩 폴더>`) — 팩 마운트 · 게임 도메인 경로가 이것으로 풀린다

        PROPERTY()
        string _windowTitle{ "SWEngine" }; ///< 주 창 제목 — 게임마다 다르다(언리얼 ProjectName 자리). 창은 팩을 마운트하기 전에 생긴다

        /** @brief 엔진 스키마에 덧붙이는 게임의 사용자 설정 스키마(팩 상대 경로, 예: `data/usersettings.settings.xml`)입니다. 비면 없습니다. */
        PROPERTY()
        string _userSettingsSchema{};

        /** @brief 엔진 스키마에 덧붙이는 게임의 텔레메트리 사건 스키마(팩 상대 경로, 예: `data/mygame.telemetry.xml`)입니다. 비면 없습니다. */
        PROPERTY()
        string _telemetrySchema{};

        /** @brief 엔진 기본(`engine/ui/uiscale.xml`) 대신 쓸 런타임 UI 배율 규칙(팩 상대 경로, 예: `data/uiscale.xml`)입니다. 비면 엔진 기본입니다. */
        PROPERTY()
        string _uiScaleSettings{};

        /** @brief 엔진 기본(`engine/ui/uithemes.xml`) 대신 쓸 런타임 UI 테마 목록(팩 상대 경로, 예: `data/uithemes.xml`)입니다. 비면 엔진 기본입니다. */
        PROPERTY()
        string _uiThemes{};

        /** @brief 엔진 기본(`engine/ui/options.ui.xml`) 대신 쓸 옵션 메뉴 문서(팩 상대 경로, 예: `ui/options.ui.xml`)입니다. 비면 엔진 기본입니다. */
        PROPERTY()
        string _uiOptionsMenu{};

        /** @brief 엔진 기본(`engine/ui/pause.ui.xml`) 대신 쓸 일시정지 메뉴 문서(팩 상대 경로)입니다. 비면 엔진 기본입니다(`_bUIPauseMenu` 가 켜졌을 때만 쓴다). */
        PROPERTY()
        string _uiPauseMenu{};

        /** @brief 화면이 없을 때 Esc · 패드 Start(`UI.Pause`)로 일시정지 메뉴를 연다. 게임 흐름(타이틀 · 경영 화면)이 Esc 를 따로 쓰면 끈다. */
        PROPERTY()
        bool _bUIPauseMenu{ false };

        /**
         * @brief 게임마다 다른 사용자 설정 기본값입니다(설정 id → 값). 엔진 스키마의 기본값을 덮어씁니다.
         * @details 플레이어가 바꾸지 않은 값만 따라갑니다. 모르는 id · 받을 수 없는 값은 기동 오류로 알립니다(`UserSettingsManager::setGameDefault`).
         */
        PROPERTY()
        map<string, string> _mapUserSettingDefault{};

        /** @brief App/EngineLoop 가 읽은 활성 값을 GameInstance 부트스트랩에 전달합니다. */
        static void              setActive( const GameConfig& config );
        static const GameConfig& getActive();
    };
} // namespace sw
