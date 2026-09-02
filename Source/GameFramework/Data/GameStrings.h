/**
 * @file GameStrings.h
 * @brief game/<pack>/data/strings.xml에서 로드하는 키/값 문자열 테이블
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) GameStrings — 프로세스 전역 키 테이블
    //    UI/로그 리터럴이 아니라 팩 XML/JSON/INI 대사·표시 다국어 문자열
    // ------------------------------------------------------------------------------
    /** @brief 다국어 언어 파일 로딩 및 키 조회를 수행하는 게임 텍스트 시스템 */
    class SW_GF_API GameStrings
    {
    public:
        using LanguageChangedCallback = sw::Delegate<void( string_view oldLanguage, string_view newLanguage )>;

        /** @brief Resource 상대 경로에서 단일 언어 또는 기본(default) 언어 파일을 로드합니다 (.xml, .json, .ini, .kv 자동 감지). */
        static bool loadFromResource( string_view assetRelativePath );

        /**
         * @brief 언어 팩 디렉터리 또는 기본 리소스 파일을 스캔하여 로드하고, 커맨드라인/기본/폴백 언어를 자동 활성화합니다.
         * @param directoryOrResourcePath 디렉터리 경로 (예: "<팩루트>/data/localization") 또는 기본 파일 경로 ("<팩루트>/data/strings.xml")
         * @param defaultLanguage 기본 활성 언어 코드 (예: "ko_KR")
         * @param fallbackLanguage 대체(Fallback) 언어 코드 (예: "en_US")
         */
        static bool setupLocalization( string_view directoryOrResourcePath, string_view defaultLanguage = "ko_KR", string_view fallbackLanguage = "en_US" );

        /** @brief Resource 상대 경로에서 특정 언어 코드(예: "ko_KR", "en_US")의 언어 파일을 로드합니다. */
        static bool loadLanguage( string_view languageCode, string_view assetRelativePath );

        /** @brief 파일 시스템 경로에서 특정 언어 코드의 언어 파일을 로드합니다. */
        static bool loadLanguageFile( string_view languageCode, string_view filePath );

        /** @brief 특정 디렉터리 내의 모든 언어 파일(예: ko_kr.json, en_us.json 등)을 파일명을 언어 코드로 하여 일괄 로드합니다. */
        static bool loadLanguageDirectory( string_view directoryPath, string_view filterExtension = ".json", bool bRecursive = false );

        /** @brief 현재 활성 언어를 설정합니다 (언어 변경 시 등록된 UI 콜백들에 알림이 전달됩니다). */
        static bool setLanguage( string_view languageCode );

        /** @brief 현재 활성 언어 코드를 반환합니다. */
        static const string& getLanguage();

        /** @brief 대체(Fallback) 언어 코드를 설정합니다 (현재 언어에 키가 누락되었을 때 사용). */
        static void setFallbackLanguage( string_view languageCode );

        /** @brief 대체(Fallback) 언어 코드를 반환합니다. */
        static const string& getFallbackLanguage();

        /** @brief 특정 언어가 로드되어 있는지 확인합니다. */
        static bool hasLanguage( string_view languageCode );

        /** @brief 등록된 모든 언어 코드 목록을 반환합니다. */
        static vector<string> getAvailableLanguages();

        /** @brief 키를 조회합니다. 현재 활성 언어 -> Fallback 언어 -> pFallback 순으로 반환합니다. */
        static const utf8* get( const utf8* pKey, const utf8* pFallback = "" );

        /** @brief 특정 언어에서 직접 키를 조회합니다 (Fallback 없음). */
        static const utf8* getFromLanguage( string_view languageCode, const utf8* pKey, const utf8* pFallback = "" );

        /** @brief 언어 변경 시 호출될 콜백을 등록합니다. */
        static uint32 onLanguageChanged( LanguageChangedCallback callback );

        /** @brief 등록된 언어 변경 콜백을 해제합니다. */
        static void removeLanguageChangedCallback( uint32 callbackId );

        /** @brief 로드된 문자열 테이블을 비웁니다. */
        static void clear();
    };
} // namespace sw
