/**
 * @file GameData.h
 * @brief 게임플레이 씬 흐름, 다국어, 입력, 세이브 부트스트랩 및 커스텀 게임 데이터
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) GameData — 씬 흐름·입력·다국어·세이브 부트스트랩 및 범용 커스텀 설정
    // ------------------------------------------------------------------------------
    /** @brief 씬 흐름, 기본 세이브, 다국어, 입력 및 범용 게임플레이 튜닝 설정 */
    REFLECT()
    struct SW_GF_API GameData
    {
    public:
        REFLECT_BODY();

        PROPERTY( Alias = "startMap, StartMap" )
        string _startMap{}; ///< 시작 맵 / 레벨 경로

        PROPERTY( Alias = "titleScene, TitleScene" )
        string _titleScene{}; ///< 타이틀 씬

        PROPERTY( Alias = "entranceScene, EntranceScene" )
        string _entranceScene{}; ///< 타이틀 다음 씬

        PROPERTY( Alias = "defaultSavePath, DefaultSavePath" )
        string _defaultSavePath{}; ///< 기본 세이브 슬롯 경로

        PROPERTY( Alias = "stringsData, StringsData" )
        string _stringsData{}; ///< 문자열 테이블 (단일 파일 폴백)

        PROPERTY( Alias = "localizationDirectory, LocalizationDirectory" )
        string _localizationDirectory{}; ///< 다국어 팩 디렉터리

        PROPERTY( Alias = "defaultLanguage, DefaultLanguage" )
        string _defaultLanguage{ "ko_kr" }; ///< 기본 활성 언어

        PROPERTY( Alias = "fallbackLanguage, FallbackLanguage" )
        string _fallbackLanguage{ "en_us" }; ///< 대체(Fallback) 언어

        PROPERTY( Alias = "inputMap, InputMap" )
        string _inputMap{}; ///< 게임플레이 InputMap 경로

        PROPERTY( Alias = "customProperties, CustomProperties" )
        map<string, string> _mapCustomProperty{}; ///< 범용 커스텀 키-값 프로퍼티 저장소

        /** @brief 커스텀 문자열 프로퍼티를 조회합니다 (없으면 fallback 반환). */
        string_view getCustomProperty( string_view key, string_view fallback = {} ) const;
        /** @brief 커스텀 정수 프로퍼티를 조회합니다. */
        int32 getCustomPropertyInt( string_view key, int32 fallback = 0 ) const;
        /** @brief 커스텀 실수 프로퍼티를 조회합니다. */
        float32 getCustomPropertyFloat( string_view key, float32 fallback = 0.0f ) const;
        /** @brief 커스텀 부울 프로퍼티를 조회합니다. */
        bool getCustomPropertyBool( string_view key, bool bFallback = false ) const;

        /** @brief 리소스 경로(XML)에서 부트스트랩 테이블을 로드합니다. */
        bool loadFromResource( string_view assetRelativePath = {} );
    };

    // ------------------------------------------------------------------------------
    // 2) BootstrapConfig — 팩 루트 + GameData
    // ------------------------------------------------------------------------------
    /** @brief Resource 아래 팩 루트와 gamedata 로딩 */
    REFLECT()
    struct SW_GF_API BootstrapConfig
    {
    public:
        REFLECT_BODY();

        PROPERTY( Alias = "packRoot, PackRoot" )
        string _packRoot{}; ///< Resource 상대 팩 폴더

        PROPERTY( Alias = "data, Data" )
        GameData _data{}; ///< `{packRoot}/data/gamedata.xml` 테이블

        /** @brief packRoot 아래 상대 경로를 Resource 상대 경로로 만듦 */
        string resolve( string_view packRelative ) const;

        /** @brief `{packRoot}/data/gamedata.xml`을 로드하고 컴포넌트 Defaults 경로를 연결합니다. */
        bool load( string_view gamedataFileName = "data/gamedata.xml" );
    };
} // namespace sw
