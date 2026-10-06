/**
 * @file GameSettings.h
 * @brief 게임플레이 씬 흐름 · 다국어 · 입력 · 세이브 부트스트랩과 커스텀 게임 데이터입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    // ------------------------------------------------------------------------------
    // 1) GameSettings — 씬 흐름 · 입력 · 다국어 · 세이브 부트스트랩과 범용 커스텀 설정
    // ------------------------------------------------------------------------------
    /**
     * @brief 씬 흐름 · 기본 세이브 · 다국어 · 입력과 범용 게임플레이 튜닝 설정입니다.
     * @details 리소스 경로 칸은 도메인을 포함한 전역 id 입니다(`game/<팩>/maps/start.scene.xml`). `GameInstanceBase` 가 읽은 뒤 게임 서비스로 묶고
     *          다국어 · 입력 맵을 적용하며, 씬 칸은 `getFirstScene` · `getEntranceScene`, 세이브 경로는 경로 없는 `saveStateToFile` 이 씁니다.
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
        map<string, string> _mapCustomProperty{}; ///< 범용 커스텀 키-값 프로퍼티 저장소

        /** @brief 커스텀 문자열 프로퍼티를 조회합니다(없으면 fallback 을 반환합니다). */
        string_view getCustomProperty( string_view key, string_view fallback = {} ) const;
        /** @brief 커스텀 정수 프로퍼티를 조회합니다. */
        int32 getCustomPropertyInt( string_view key, int32 fallback = 0 ) const;
        /** @brief 커스텀 실수 프로퍼티를 조회합니다. */
        float32 getCustomPropertyFloat( string_view key, float32 fallback = 0.0f ) const;
        /** @brief 커스텀 부울 프로퍼티를 조회합니다. */
        bool getCustomPropertyBool( string_view key, bool bFallback = false ) const;

        /** @brief 리소스 경로(XML)에서 부트스트랩 테이블을 로드합니다. */
        [[nodiscard]] bool loadFromResource( string_view assetRelativePath = {} );
        /** @brief `loadFromResource` 의 XML 글 판입니다(시험). 먼저 비우고 읽습니다. */
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );

    private:
        /**
         * @brief `<GameSettings>` 루트의 표준 칸과 커스텀 칸을 읽습니다(`GameDataXml::loadFile`).
         * @details 표준 칸 · `<custom>` · `<Defaults>`(`ComponentDefaults` 가 같은 파일을 읽는다) 밖의 원소는 로드 오류입니다 — 커스텀 값은
         *          `<custom><prop key="…">` 안에만 둡니다(표준 칸 철자가 틀리면 커스텀 값이 되어 조용히 사라지지 않게).
         */
        [[nodiscard]] bool loadRoot( const XmlNode& root, string_view sourceName );
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) BootstrapConfig — 팩 루트 + GameSettings
    // ------------------------------------------------------------------------------
    /** @brief Resource 아래 팩 루트와 gamesettings 로딩입니다. */
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
        [[nodiscard]] bool load( string_view gameSettingsFileName = "data/gamesettings.xml" );
    };
} // namespace sw
