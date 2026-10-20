/**
 * @file GameSettings.h
 * @brief 게임플레이 씬 흐름, 다국어, 입력, 세이브 부트스트랩과 커스텀 게임 데이터를 정의합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"

#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    // ------------------------------------------------------------------------------
    // 1) GameSettings — 씬 흐름 · 입력 · 다국어 · 세이브 부트스트랩과 범용 커스텀 설정
    // ------------------------------------------------------------------------------
    /**
     * @brief 씬 흐름, 기본 세이브, 다국어, 입력과 범용 게임플레이 튜닝 설정입니다.
     * @details 리소스 경로 필드는 도메인을 포함한 전역 id 이다(`game/<팩>/maps/start.scene.xml`).
     *          `GameInstanceBase` 가 읽은 뒤 게임 서비스로 등록하고 다국어와 입력 맵을 적용한다.
     *          씬 필드는 `getFirstScene` 과 `getEntranceScene` 이 쓰고, 세이브 경로는 경로 없이 부른 `saveStateToFile` 이 쓴다.
     */
    REFLECT()
    struct SW_GF_API GameSettings
    {
    public:
        REFLECT_BODY();

        PROPERTY()
        string _startMap{}; ///< 시작 맵 / 레벨 경로

        PROPERTY()
        string _titleScene{}; ///< 타이틀 씬

        PROPERTY()
        string _entranceScene{}; ///< 타이틀 다음 씬

        PROPERTY()
        string _defaultSavePath{}; ///< 기본 세이브 슬롯 경로(파일 경로 — 경로 없는 `GameInstanceBase::saveStateToFile` · `loadStateFromFile`)

        PROPERTY()
        string _localizationProject{}; ///< 로컬라이제이션 프로젝트(`*.locproject.json`, 리소스 경로) — 원문 표 · 문화권 번역 표가 그 옆에 있다

        PROPERTY()
        string _defaultLanguage{ "ko_kr" }; ///< 기본 활성 언어

        PROPERTY()
        string _fallbackLanguage{ "en_us" }; ///< 대체(Fallback) 언어

        PROPERTY()
        string _inputMap{}; ///< 게임플레이 InputMap 경로(통합 맵 `InputManager::getInputMap()` 에 읽힌다)

        PROPERTY()
        string _loadingScreen{}; ///< 씬을 비동기로 바꾸는 동안 띄우는 로딩 화면 문서(Loading 층 — `LoadingScreenController`). 정본은 팩의 gamesettings 이고 적지 않으면 로딩 화면 없음

        PROPERTY()
        string _audioEvents{}; ///< 게임의 사운드 이벤트 라이브러리(`*.audioevents.xml`). `GameInstanceBase` 가 `onInitialize` 앞에 올리고, 적었는데 못 읽으면 게임 초기화가 실패한다. 적지 않으면 이벤트 없음

        /** @brief 리소스 경로(XML)에서 부트스트랩 테이블을 로드합니다. */
        [[nodiscard]] bool loadFromResource( string_view assetRelativePath = {} );
        /** @brief `loadFromResource` 의 XML 문자열 버전이며 테스트가 씁니다. 먼저 비우고 읽습니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName = {} );

    private:
        /**
         * @brief `<GameSettings>` 루트의 표준 칸과 커스텀 칸을 읽습니다(`GameDataXML::loadFile`).
         * @details 표준 칸, `<custom>`, `<Defaults>`(`ComponentDefaults` 가 같은 파일을 읽는다), `<Engine>`(`PackEngineSettings` 가 읽는다) 밖의 원소는 로드 오류이다.
         *          커스텀 값은 `<custom><prop key="…">` 안에만 둔다. 표준 칸 철자가 틀렸을 때 커스텀 값이 되어 조용히 사라지지 않게 하려는 것이다.
         */
        [[nodiscard]] bool loadRoot( const XMLNode& root, string_view sourceName );
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) BootstrapConfig — 팩 루트 + GameSettings
    // ------------------------------------------------------------------------------
    /** @brief Resource 아래 팩 루트와 gamesettings 를 로딩하는 설정입니다. */
    REFLECT()
    struct SW_GF_API BootstrapConfig
    {
    public:
        REFLECT_BODY();

        PROPERTY()
        string _packRoot{}; ///< Resource 상대 팩 폴더

        PROPERTY()
        GameSettings _data{}; ///< `{packRoot}/data/gamesettings.xml` 테이블

        /** @brief packRoot 아래 상대 경로를 Resource 상대 경로로 만듭니다. */
        string resolve( string_view packRelative ) const;

        /** @brief `{packRoot}/data/gamesettings.xml` 을 읽고, 그 경로를 컴포넌트 기본값 경로(`Component::setDefaultGameSettingsPath`)로 등록합니다. */
        [[nodiscard]] bool load( string_view gameSettingsFileName = PackEngineSettings::kPackRelativePath );
    };
} // namespace sw
