/**
 * @file LocalizationManager.h
 * @brief 다국어(로컬라이제이션) 관리자입니다(싱글톤이 아닙니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class StringTable;

    /**
     * @class LocalizationManager
     * @brief 언어별 문자열 테이블(JSON/XML/KeyValue/디렉터리)을 파일 단위로 로드하고 관리하는 다국어 매니저입니다(싱글톤이 아닙니다).
     */
    class SW_API LocalizationManager
    {
    public:
        using LanguageChangedCallback = sw::Delegate<void( string_view oldLanguage, string_view newLanguage )>;

        LocalizationManager();
        ~LocalizationManager();

        LocalizationManager( const LocalizationManager& )            = delete;
        LocalizationManager& operator=( const LocalizationManager& ) = delete;
        LocalizationManager( LocalizationManager&& other ) noexcept;
        LocalizationManager& operator=( LocalizationManager&& other ) noexcept;

        // ------------------------------------------------------------------------------
        // 1) 초기화 · 해제
        // ------------------------------------------------------------------------------
        /** @brief 모든 언어 테이블과 콜백을 비웁니다. */
        void clear();

        // ------------------------------------------------------------------------------
        // 2) 로딩 및 등록
        // ------------------------------------------------------------------------------
        /** @brief 파일 확장자(.json, .xml, .ini, .kv, .bin 등)로 형식을 골라 언어 파일을 로드합니다. */
        bool loadLanguageFile( string_view languageCode, string_view filePath );

        /** @brief 리소스 상대 경로에서 언어 파일을 로드합니다. */
        bool loadLanguageResource( string_view languageCode, string_view assetRelativePath );

        /** @brief JSON 텍스트에서 언어 테이블을 로드합니다. */
        bool loadLanguageJson( string_view languageCode, string_view jsonText );

        /** @brief XML 텍스트에서 언어 테이블을 로드합니다(<GameStrings><string key="...">...</string></GameStrings>). */
        bool loadLanguageXml( string_view languageCode, string_view xmlText );

        /** @brief KeyValue/INI 텍스트에서 언어 테이블을 로드합니다(key=value). */
        bool loadLanguageKeyValue( string_view languageCode, string_view kvText );

        /**
         * @brief 디렉터리 안의 언어 파일(예: ko_kr.json, en_us.json)을 파일 이름을 언어 코드로 삼아 한꺼번에 로드합니다.
         * @param directoryPath 탐색할 디렉터리 경로
         * @param filterExtension 탐색할 확장자(기본 ".json", "" 면 모든 파일)
         * @param bRecursive 하위 폴더 포함 여부
         */
        bool loadLanguageDirectory( string_view directoryPath, string_view filterExtension = ".json", bool bRecursive = false );

        /**
         * @brief 언어 팩 디렉터리나 리소스 파일을 찾아 언어 팩들을 로드하고 활성 · 폴백 언어를 정합니다.
         * @param directoryOrResourcePath 디렉터리 경로(예: "<팩루트>/data/localization") 또는 기본 파일 경로("<팩루트>/data/strings.xml")
         * @param defaultLanguage 기본 활성 언어 코드(예: "ko_KR")
         * @param fallbackLanguage 폴백 언어 코드(예: "en_US")
         * @return 언어 파일을 하나 이상 로드하고 설정했으면 true 입니다.
         */
        bool initialize( string_view directoryOrResourcePath, string_view defaultLanguage = "ko_KR", string_view fallbackLanguage = "en_US" );

        /** @brief 등록된 모든 언어 테이블을 바이너리 로컬라이제이션 팩(LOC1) 파일 하나로 저장합니다. */
        bool saveToBinaryPack( string_view filePath ) const;
        /** @brief 바이너리 로컬라이제이션 팩(LOC1) 파일 하나에서 모든 언어 테이블을 로드합니다. */
        bool loadFromBinaryPack( string_view filePath );

        /** @brief 언어 테이블을 직접 등록하거나 기존 테이블을 대체합니다. */
        void registerLanguageTable( string_view languageCode, unique_ptr<StringTable> pStringTable );

        /** @brief 한 언어의 테이블을 내립니다. */
        void unloadLanguage( string_view languageCode );

        // ------------------------------------------------------------------------------
        // 3) 언어 설정 및 조회
        // ------------------------------------------------------------------------------
        /** @brief 현재 활성 언어를 설정합니다. 언어가 바뀌면 등록된 콜백을 부릅니다. */
        bool setCurrentLanguage( string_view languageCode );

        /**
         * @brief 현재 활성 언어 코드를 반환합니다.
         * @details **값으로 반환합니다.** 참조를 주면 `_mutex` 를 놓은 뒤의 참조가 되고, 그 사이
         *          `setCurrentLanguage` 가 길이가 다른 코드를 넣으면 `string` 이 버퍼를 새로 잡아
         *          받아 든 쪽이 사라진 메모리를 읽습니다. 락으로 지키는 것이 뜻을 가지려면 락을 쥔 동안
         *          복사해서 나가야 합니다.
         */
        string getCurrentLanguage() const;

        /** @brief 폴백 언어를 설정합니다(현재 언어에 키가 없을 때 씁니다). */
        void setFallbackLanguage( string_view languageCode );

        /** @brief 폴백 언어 코드를 반환합니다. 위와 같은 이유로 값으로 반환합니다. */
        string getFallbackLanguage() const;

        /** @brief 그 언어가 로드되어 있는지 확인합니다. */
        bool hasLanguage( string_view languageCode ) const;

        /** @brief 현재 등록된 모든 언어 코드 목록을 반환합니다. */
        vector<string> getAvailableLanguages() const;

        /** @brief 등록된 언어 테이블 개수를 반환합니다. */
        size_t getLanguageCount() const;

        // ------------------------------------------------------------------------------
        // 4) 문자열 조회
        // ------------------------------------------------------------------------------
        /**
         * @brief 현재 활성 언어에서 문자열을 찾습니다.
         *        현재 언어에 키가 없으면 폴백 언어에서 찾고, 모두 없으면 pDefaultText 를 반환합니다.
         */
        const utf8* getString( const hashed_string& key, const utf8* pDefaultText = "" ) const;
        /**
         * @brief 키를 **intern 하지 않고** 조회합니다.
         * @details 물어보는 문자열이 키가 아닐 수도 있는 자리(대사 원문 등)에서 씁니다. `hashed_string`
         *          을 만들어 물으면 그 텍스트가 intern 아레나에 영구히 남아, 콘텐츠가 늘수록 함께
         *          늘어나는 누수가 됩니다.
         */
        const utf8* getStringByText( string_view keyText, const utf8* pDefaultText = "" ) const;

        /**
         * @brief 지정한 언어에서만 문자열을 찾습니다(폴백 없음).
         */
        const utf8* getStringFromLanguage( string_view languageCode, const hashed_string& key, const utf8* pDefaultText = nullptr ) const;

        /** @brief 현재 언어나 폴백 언어에 그 키가 있는지 확인합니다. */
        bool hasString( const hashed_string& key ) const;

        /** @brief 지정한 언어에 그 키가 있는지 확인합니다. */
        bool hasStringInLanguage( string_view languageCode, const hashed_string& key ) const;

        /** @brief 지정한 언어 테이블에 문자열을 직접 넣습니다. */
        void setString( string_view languageCode, const hashed_string& key, string_view value );

        /** @brief 지정한 언어의 StringTable 을 반환합니다(없으면 nullptr). */
        const StringTable* getLanguageTable( string_view languageCode ) const;

        /** @brief 지정한 언어의 StringTable 을 반환합니다. 없으면 새로 만듭니다. */
        StringTable* getOrCreateLanguageTable( string_view languageCode );

        // ------------------------------------------------------------------------------
        // 5) 언어 변경 이벤트 알림
        // ------------------------------------------------------------------------------
        /** @brief 언어가 바뀔 때 부를 콜백을 등록하고 고유 콜백 ID 를 반환합니다. */
        uint32 registerLanguageChangedCallback( LanguageChangedCallback callback );

        /** @brief 등록된 언어 변경 콜백을 해제합니다. */
        void unregisterLanguageChangedCallback( uint32 callbackId );

    private:
        /** @brief 한 언어가 처음 들어왔을 때 활성 언어가 비어 있으면 그 언어로 세웁니다. */
        void markLanguageLoaded( string_view languageCode );
        void notifyLanguageChanged( string_view oldLanguage, string_view newLanguage );
        /**
         * @brief 미리 구한 해시로 활성 언어 → 폴백 언어 순으로 한 번만 훑습니다.
         * @details 두 `getString` 오버로드의 공통 경로입니다. **해시를 인자로 받는 이유**는
         *          `hashed_string` 이 이미 해시를 들고 있기 때문입니다. 문자열을 넘기면 그
         *          경로에서 해시를 다시 계산하게 됩니다.
         */
        const utf8* findByHash( uint64 keyHash, const utf8* pDefaultText ) const;

    private:
        mutable std::shared_mutex _mutex;
        string                    _currentLanguage;
        string                    _fallbackLanguage;

        unordered_map<string, unique_ptr<StringTable>> _mapLanguageTable;
        unordered_map<uint32, LanguageChangedCallback> _mapCallback;
        uint32                                         _nextCallbackId;
    };
} // namespace sw
