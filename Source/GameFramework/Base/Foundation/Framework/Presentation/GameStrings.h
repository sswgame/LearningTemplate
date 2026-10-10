/**
 * @file GameStrings.h
 * @brief 게임 코드가 부르는 다국어 창구입니다 — 엔진 `LocalizationManager` 를 게임 서비스로 부릅니다.
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
    //    UI/로그 리터럴이 아니라 로컬라이제이션 프로젝트의 대사·표시 다국어 문자열
    // ------------------------------------------------------------------------------
    /** @brief 다국어 언어 파일 로딩과 키 조회를 맡는 게임 텍스트 시스템입니다. */
    class SW_GF_API GameStrings
    {
    public:
        using LanguageChangedCallback = sw::Delegate<void( string_view oldLanguage, string_view newLanguage )>;

        /**
         * @brief 게임의 로컬라이제이션 프로젝트를 올리고(앞의 게임 프로젝트는 내린다) 명령줄 · 기본 · 폴백 언어를 활성화합니다.
         * @param projectPath 프로젝트 파일(`<팩루트>/data/localization/<게임>.locproject.json`)
         * @param defaultLanguage 기본 활성 언어 코드 (예: "ko_KR")
         * @param fallbackLanguage 대체(Fallback) 언어 코드 (예: "en_US")
         */
        static bool initialize( string_view projectPath, string_view defaultLanguage = "ko_KR", string_view fallbackLanguage = "en_US" );

        /** @brief 현재 활성 언어를 설정합니다(언어가 바뀌면 등록된 UI 콜백들에 알림이 갑니다). */
        static bool setLanguage( string_view languageCode );

        /** @brief 현재 활성 언어 코드를 반환합니다. */
        static string getLanguage();

        /** @brief 대체(Fallback) 언어 코드를 설정합니다(현재 언어에 키가 없을 때 씁니다). */
        static void setFallbackLanguage( string_view languageCode );

        /** @brief 대체(Fallback) 언어 코드를 반환합니다. */
        static string getFallbackLanguage();

        /** @brief 특정 언어가 로드되어 있는지 확인합니다. */
        static bool hasLanguage( string_view languageCode );

        /** @brief 등록된 모든 언어 코드 목록을 반환합니다. */
        static vector<string> getAvailableLanguages();

        /** @brief 키를 조회합니다. 현재 활성 언어 → Fallback 언어 → pFallback 순으로 반환합니다. */
        static const utf8* get( const utf8* pKey, const utf8* pFallback = "" );

        /** @brief 특정 언어에서 직접 키를 조회합니다(Fallback 없음). */
        static const utf8* getFromLanguage( string_view languageCode, const utf8* pKey, const utf8* pFallback = "" );

        /** @brief 언어가 바뀔 때 부를 콜백을 등록합니다. */
        static uint32 registerLanguageChangedCallback( LanguageChangedCallback callback );

        /** @brief 등록된 언어 변경 콜백을 해제합니다. */
        static void unregisterLanguageChangedCallback( uint32 callbackID );

        /** @brief 게임 프로젝트를 내립니다(엔진 문자열은 남는다). */
        static void clear();
    };
} // namespace sw
