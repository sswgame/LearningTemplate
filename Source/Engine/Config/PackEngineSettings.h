/**
 * @file PackEngineSettings.h
 * @brief 게임 팩의 `data/gamesettings.xml` 안 `<Engine>` 요소입니다 — 엔진이 읽는 게임별 설정(창 제목 · 렌더 파이프라인 · 사용자 설정과 텔레메트리 스키마 · UI 규칙)입니다.
 * @details 게임 하나의 설정 원본은 팩의 그 파일 하나다. 요소마다 읽는 계층이 하나다 — `<Engine>` 은 이 구조체(엔진), 나머지 표준 칸은 `GameSettings`(GameFramework),
 *          `<Defaults>` 는 `ComponentDefaults` 가 읽는다. 호스트 프리셋(`GameConfig`)은 팩을 찾는 팩 루트만 가진다.
 *          유니티 Player Settings(제품 이름) · Graphics(렌더 파이프라인 에셋), 언리얼 `DefaultGame.ini` 의 프로젝트 설정 자리다.
 *
 *          쓰는 법:
 *            <GameSettings>
 *              <Engine _windowTitle="Nile City" _bUIPauseMenu="true" _userSettingsSchema="game/nilecity/data/nile.settings.xml" />
 *              <startMap>…</startMap>
 *            </GameSettings>
 */
#pragma once
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

namespace sw
{
    /**
     * @struct PackEngineSettings
     * @brief 팩 `gamesettings.xml` 의 `<Engine>` 요소입니다. 경로 칸은 도메인을 포함한 리소스 id(`game/<팩>/data/…`)입니다.
     * @details 경로 칸은 선택이다 — 적지 않으면 `DataPath` 가 "읽지 않음" 으로 남고, 그 뜻은 칸마다 적은 대로(엔진 기본 · 없음)다. 비었는지는 `isSet` 으로 묻는다
     *          (`get` 은 Debug 에서 어서션이 난다). 창 제목은 필수다. 모르는 속성 · 자식은 로드 오류다(`ConfigManager::readConfigXMLElement`).
     */
    REFLECT()
    struct SW_API PackEngineSettings
    {
        REFLECT_BODY();

        static constexpr const utf8* kPackRelativePath = "data/gamesettings.xml"; ///< 팩 루트 기준 설정 파일
        static constexpr const utf8* kRootElementName  = "GameSettings";          ///< 설정 파일의 루트 요소
        static constexpr const utf8* kElementName      = "Engine";                ///< 이 구조체가 읽는 요소 — `GameSettings` 는 건너뛴다

        PROPERTY( Required )
        string _windowTitle{}; ///< 주 창 제목(언리얼 ProjectName · 유니티 productName 자리). 창은 이 설정을 읽은 뒤에 만들어진다(`RHISubsystem`)

        /**
         * @brief 이 게임이 그리는 렌더 파이프라인 XML(예: `engine/pipeline/forward2dpipeline.xml`)입니다. 없으면 엔진 기본(포워드)입니다.
         * @details 명령줄(`-gv_renderPipeline` · `-gv_deferred`)이 이보다 우선합니다(`FrameRendererUtil::computePipelinePath`).
         */
        PROPERTY()
        DataPath _renderPipeline;

        PROPERTY()
        DataPath _userSettingsSchema; ///< 엔진 스키마에 덧붙이는 게임의 사용자 설정 스키마입니다. 없으면 덧붙이지 않습니다

        PROPERTY()
        DataPath _telemetrySchema; ///< 엔진 스키마에 덧붙이는 게임의 텔레메트리 사건 스키마입니다. 없으면 덧붙이지 않습니다

        PROPERTY()
        DataPath _uiScaleSettings; ///< 런타임 UI 배율 규칙입니다. 없으면 엔진 기본(`EngineDefaultAssets::_uiScaleSettings`)입니다

        PROPERTY()
        DataPath _uiThemes; ///< 런타임 UI 테마 목록입니다. 없으면 엔진 기본(`EngineDefaultAssets::_uiThemes`)입니다

        PROPERTY()
        DataPath _uiOptionsMenu; ///< 옵션 메뉴 문서입니다. 없으면 엔진 기본(`EngineDefaultAssets::_uiOptionsMenu`)입니다

        PROPERTY()
        DataPath _uiPauseMenu; ///< 일시정지 메뉴 문서입니다. 없으면 엔진 기본(`EngineDefaultAssets::_uiPauseMenu`)이고, `_bUIPauseMenu` 가 켜졌을 때만 씁니다

        /** @brief 화면이 없을 때 Esc · 패드 Start(`UI.Pause`)로 일시정지 메뉴를 연다. 게임 흐름(타이틀 · 경영 화면)이 Esc 를 따로 쓰면 끈다. */
        PROPERTY()
        bool _bUIPauseMenu{ false };

        /**
         * @brief 게임마다 다른 사용자 설정 기본값입니다(설정 id → 값). 엔진 스키마의 기본값을 덮어씁니다.
         * @details 플레이어가 바꾸지 않은 값만 따라갑니다. 모르는 id · 받을 수 없는 값은 기동 오류로 알립니다(`UserSettingsManager::setGameDefault`).
         */
        PROPERTY()
        map<string, string> _mapUserSettingDefault{};

        /**
         * @brief 팩 루트 @p packRoot 의 `data/gamesettings.xml` 에서 `<Engine>` 요소를 읽습니다.
         * @return 파일 · 요소가 없거나, 필수 칸이 빠졌거나, 모르는 칸이 있거나, (개발 빌드에서) 경로 칸이 가리키는 파일이 없으면 false 입니다(이유는 오류 로그). 대신 쓸 값은 없습니다.
         */
        [[nodiscard]] bool loadFromPack( const DataPath& packRoot );
        /** @brief `loadFromPack` 의 XML 문자열 버전입니다(테스트가 씁니다). @p sourceName 은 오류 로그에 씁니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );

        /** @brief 팩이 적은 @p packValue 가 있으면 그것, 없으면 엔진이 가진 @p engineValue 의 경로입니다(UI 규칙 칸 — 엔진 기본을 팩이 덮어쓴다). */
        [[nodiscard]] static const string& resolve( const DataPath& packValue, const DataPath& engineValue );
    };
} // namespace sw
