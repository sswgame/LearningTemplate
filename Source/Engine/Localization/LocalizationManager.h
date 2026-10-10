/**
 * @file LocalizationManager.h
 * @brief 다국어(로컬라이제이션) 관리자입니다(싱글톤이 아닙니다 — 엔진 서비스 `engine::getLocalizationManager()`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/map.h"
#include "Core/Container/set.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

#include "Engine/Localization/CultureInfo.h"
#include "Engine/Localization/LocalizationDocuments.h"

namespace sw
{
    class StringTable;
    class TextArgumentList;

    /** @brief 프로젝트를 누가 올렸는지입니다. 게임을 다시 올려도 엔진 문자열은 남습니다. */
    enum class LocalizationScope : uint8
    {
        Engine = 0,
        Game
    };

    /** @brief 문화권 하나의 번역 상태 수입니다(올린 프로젝트 전체). */
    struct LocalizationCultureStatistics
    {
        uint32 _currentCount{ 0 }; ///< 화면에 나오는 번역
        uint32 _staleCount{ 0 };   ///< 원문이 바뀐 뒤의 번역(나오지 않는다)
        uint32 _reviewCount{ 0 };  ///< 검토 표시(나오지 않는다)
        uint32 _orphanCount{ 0 };  ///< 원문 표에 없는 키의 번역
    };
} // namespace sw

namespace sw
{
    /**
     * @class LocalizationManager
     * @brief 문화권 표 · 로컬라이제이션 프로젝트(원문 표 + 문화권 번역 표)를 올려 키를 지금 문화권의 글로 바꾸고, 문화권에 맞춰 메시지를 포맷합니다.
     * @details 조회 사슬은 지금 문화권 → 그 부모(`ko_kr` → `ko`) → 폴백 문화권 → 그 부모 → 올린 프로젝트들의 원문 문화권입니다(언리얼 culture fallback ·
     *          유니티 Locale fallback). 낡은(stale) · 검토(review) 번역은 화면에 내지 않고 사슬의 다음으로 떨어집니다.
     *          의사 문화권(`qps_ploc` · `qps_plocm`, 문화권 표의 `pseudo`)은 원문을 변환해 만든 표이고 Dev 빌드에서만 고를 수 있습니다.
     *          조회가 돌려주는 `const utf8*` 는 추가 전용 저장소(`StringTable.cpp`)에 있어 다시 읽기 · 언어 변경 뒤에도 유효합니다.
     */
    class SW_API LocalizationManager
    {
    public:
        using LanguageChangedCallback = sw::Delegate<void( string_view oldLanguage, string_view newLanguage )>;

        LocalizationManager();
        ~LocalizationManager();

        LocalizationManager( const LocalizationManager& )            = delete;
        LocalizationManager& operator=( const LocalizationManager& ) = delete;

        /** @brief 언어 코드의 정본 철자입니다 — 소문자, `-` 는 `_`(`ko-KR` · `ko_KR` → `ko_kr`). 표에 넣고 찾는 모든 길이 이것을 지납니다. */
        static string normalizeLanguageCode( string_view languageCode );

        // ------------------------------------------------------------------------------
        // 1) 초기화 · 해제
        // ------------------------------------------------------------------------------
        /** @brief 프로젝트 · 낱개 표 · 지금 언어 · 콜백 · 빠진 키 기록을 비웁니다. 문화권 표는 남깁니다. */
        void clear();

        // ------------------------------------------------------------------------------
        // 2) 데이터 — 문화권 표 · 프로젝트 · 낱개 표
        // ------------------------------------------------------------------------------
        /** @brief 문화권 표(`*.cultures.json`)를 읽습니다. 의사 문화권의 표도 다시 만듭니다. */
        [[nodiscard]] bool loadCultureTable( string_view resourcePath );
        /** @brief 문화권 표를 JSON 글에서 읽습니다(시험 · 도구). */
        [[nodiscard]] bool loadCultureTableJson( string_view jsonText );
        /** @brief @p culture 의 형식 데이터(없으면 부모, 끝까지 없으면 기본값)를 값으로 돌려줍니다. */
        CultureInfo resolveCulture( string_view culture ) const;

        /**
         * @brief 로컬라이제이션 프로젝트(`*.locproject.json`, 리소스 경로 또는 절대 경로)를 올립니다 — 원문 표들과 문화권 번역 표들을 읽습니다.
         * @details 같은 이름의 프로젝트가 이미 있으면 바꿉니다. 원문 표 두 개에 같은 키가 있으면 오류이고 앞의 것을 씁니다.
         */
        [[nodiscard]] bool mountProject( string_view projectPath, LocalizationScope scope );
        /** @brief 그 범위의 프로젝트를 내립니다. */
        void unmountProjects( LocalizationScope scope );
        /** @brief 올린 프로젝트 이름입니다(올린 순서). */
        vector<string> getMountedProjectNames() const;
        /** @brief 올린 프로젝트를 모두 디스크에서 다시 읽습니다. 하나라도 못 읽으면 false 이고 그 프로젝트는 예전 내용을 지킵니다. */
        [[nodiscard]] bool reloadProjects();
        /** @brief 바뀐 파일이 올린 프로젝트의 것이면 그 프로젝트를 다시 읽고 true 입니다(핫 리로드). 아니면 아무것도 안 하고 false 입니다. */
        [[nodiscard]] bool reloadChangedFile( string_view changedPath );
        /** @brief 그 경로가 올린 프로젝트의 파일(프로젝트 · 원문 표 · 번역 표)인지입니다. */
        bool isProjectFile( string_view path ) const;

        /** @brief 번역 표 JSON(`TranslationTable` 형식)을 낱개 표로 올립니다. 원문 확인 없이 프로젝트 위에 덮입니다(시험 · 도구). */
        [[nodiscard]] bool loadLanguageJson( string_view languageCode, string_view jsonText );
        /** @brief 낱개 표에 문자열 하나를 넣습니다. */
        void setString( string_view languageCode, const hashed_string& key, string_view value );
        /** @brief 그 언어의 낱개 표를 내립니다(프로젝트의 글은 남는다). */
        void unloadLanguage( string_view languageCode );

        /**
         * @brief 게임 프로젝트를 올리고(앞의 게임 프로젝트는 내린다) 활성 · 폴백 언어를 정합니다 — 명령줄 `-lang` → @p defaultLanguage → 폴백 → 아무 언어.
         * @return 프로젝트를 올렸으면 true 입니다.
         */
        bool initialize( string_view projectPath, string_view defaultLanguage = "ko_KR", string_view fallbackLanguage = "en_US" );

        // ------------------------------------------------------------------------------
        // 3) 언어 설정 및 조회
        // ------------------------------------------------------------------------------
        /** @brief 현재 활성 언어를 설정합니다. 언어가 바뀌면 글 판(revision)을 올리고 등록된 콜백을 부릅니다. */
        bool setCurrentLanguage( string_view languageCode );
        /** @brief 현재 활성 언어 코드를 값으로 반환합니다(락을 놓은 뒤의 참조는 사라진 버퍼를 가리킬 수 있다). */
        string getCurrentLanguage() const;
        /** @brief 폴백 언어를 설정합니다. */
        void setFallbackLanguage( string_view languageCode );
        /** @brief 폴백 언어 코드를 값으로 반환합니다. */
        string getFallbackLanguage() const;
        /** @brief 그 언어의 글이 있는지(원문 · 번역 · 낱개 표 · 의사 문화권) 확인합니다. */
        bool hasLanguage( string_view languageCode ) const;
        /** @brief 고를 수 있는 언어 코드입니다(사전 순). 의사 문화권은 Dev 빌드에서만 들어 있습니다. */
        vector<string> getAvailableLanguages() const;
        /** @brief 고를 수 있는 언어 수입니다. */
        size_t getLanguageCount() const;
        /** @brief 지금의 조회 사슬입니다(앞이 먼저). */
        vector<string> getLookupChain() const;
        /** @brief 지금 문화권의 형식 데이터입니다(값). */
        CultureInfo getCurrentCulture() const;
        /** @brief 지금 문화권이 오른쪽→왼쪽인지입니다(UI 배치를 뒤집는다). */
        bool isRightToLeft() const;
        /** @brief 그 문화권(빈 값이면 지금 문화권)의 글꼴 대체 목록입니다. 적힌 것이 없으면 폴백 문화권의 것입니다. */
        vector<string> getFontFallback( string_view languageCode = {} ) const;
        /** @brief 문화권 하나의 번역 상태 수입니다. */
        LocalizationCultureStatistics getStatistics( string_view languageCode ) const;
        /** @brief 글이 바뀔 때마다(언어 변경 · 다시 읽기 · 올리기) 오르는 판 번호입니다. UI 는 이것이 바뀌면 다시 묻습니다(언리얼 TextRevision). */
        uint32 getTextRevision() const;

        // ------------------------------------------------------------------------------
        // 4) 문자열 조회 · 포맷
        // ------------------------------------------------------------------------------
        /** @brief 조회 사슬에서 키를 찾습니다. 어디에도 없으면 빠진 키로 알리고 @p pDefaultText 입니다. */
        const utf8* getString( const hashed_string& key, const utf8* pDefaultText = "" ) const;
        /**
         * @brief 키를 **intern 하지 않고** 조회합니다. 빠진 키로 알리지 않습니다.
         * @details 물어보는 글이 키가 아닐 수도 있는 자리(대사 원문 등)에서 씁니다. `hashed_string` 을 만들면 그 텍스트가 intern 아레나에 영구히 남습니다.
         */
        const utf8* getStringByText( string_view keyText, const utf8* pDefaultText = "" ) const;
        /** @brief 지정한 언어에서만 찾습니다(사슬 없음). */
        const utf8* getStringFromLanguage( string_view languageCode, const hashed_string& key, const utf8* pDefaultText = nullptr ) const;
        /** @brief 조회 사슬 어딘가에 그 키가 있는지 확인합니다. */
        bool hasString( const hashed_string& key ) const;
        /** @brief 지정한 언어에 그 키가 있는지 확인합니다. */
        bool hasStringInLanguage( string_view languageCode, const hashed_string& key ) const;
        /** @brief 지정한 언어의 실행 표입니다(없으면 nullptr). 표 객체는 언어가 사는 동안 같은 주소입니다. */
        const StringTable* getLanguageTable( string_view languageCode ) const;

        /** @brief 메시지 패턴을 지금 문화권으로 포맷합니다(`TextFormatter`). 오류는 경고로 알리고, 글은 그래도 돌려줍니다. */
        string formatText( string_view pattern, const TextArgumentList& arguments ) const;
        /** @brief 키의 글을 찾아 포맷합니다. */
        string getFormattedString( const hashed_string& key, const TextArgumentList& arguments, const utf8* pDefaultText = "" ) const;

        /** @brief 빠진 키를 기록합니다(처음 한 번 경고). 올린 글이 하나도 없으면 기록하지 않습니다. `SW_LOCTEXT` 가 부릅니다. */
        void reportMissingKey( string_view key ) const;
        /** @brief 지금까지 빠진 키입니다(사전 순). */
        vector<string> getMissingKeys() const;
        /** @brief 빠진 키 기록을 비웁니다. */
        void clearMissingKeys();

        // ------------------------------------------------------------------------------
        // 5) 언어 변경 이벤트 알림
        // ------------------------------------------------------------------------------
        /** @brief 글이 바뀔 때(언어 변경 · 같은 언어의 다시 읽기 — 그때는 두 인자가 같다) 부를 콜백을 등록하고 ID 를 반환합니다. */
        uint32 registerLanguageChangedCallback( LanguageChangedCallback callback );
        /** @brief 등록된 콜백을 해제합니다. */
        void unregisterLanguageChangedCallback( uint32 callbackID );

    private:
        /** @brief 올린 프로젝트 하나 — 읽은 문서와 그 파일 경로입니다. */
        struct MountedProject
        {
            LocalizationProject          _project;
            string                       _projectPath;
            vector<string>               _listFilePath; ///< 프로젝트 · 원문 표 · 번역 표(있든 없든 — 새로 생긴 번역 표도 다시 읽는다)
            map<string, SourceTextEntry> _mapSource;    ///< 원문 표들을 합친 것
            vector<TranslationTable>     _listTranslation;
            LocalizationScope            _scope{ LocalizationScope::Game };
        };

        /** @brief 프로젝트 파일 셋을 읽어 @p outProject 를 채웁니다. 프로젝트 · 원문 표를 못 읽으면 false 입니다. */
        [[nodiscard]] static bool readProject( string_view projectPath, MountedProject& outProject );
        /** @brief 읽은 문서들로 실행 표를 다시 채웁니다(`_mutex` 를 쥔 채). */
        void rebuildTablesLocked();
        /** @brief 조회 사슬을 다시 셉니다(`_mutex` 를 쥔 채). */
        void rebuildLookupChainLocked();
        /** @brief 그 코드의 실행 표입니다. 없으면 만듭니다(`_mutex` 를 쥔 채). */
        StringTable& getOrCreateTableLocked( const string& code );
        /** @brief 미리 구한 해시로 조회 사슬을 한 번 훑습니다. */
        const utf8* findByHash( uint64 keyHash ) const;
        /** @brief 글 판을 올리고 콜백을 부릅니다. */
        void notifyLanguageChanged( string_view oldLanguage, string_view newLanguage );

    private:
        mutable std::shared_mutex                      _mutex;
        string                                         _currentLanguage;
        string                                         _fallbackLanguage;
        CultureTable                                   _cultureTable;
        CultureInfo                                    _currentCulture; ///< 지금 문화권의 형식 데이터(사슬을 다시 셀 때 함께)
        vector<MountedProject>                         _listProject;
        map<string, TranslationTable>                  _mapLooseTable;
        unordered_map<string, unique_ptr<StringTable>> _mapLanguageTable;
        map<string, LocalizationCultureStatistics>     _mapCultureStatistic;
        vector<string>                                 _listLookupCulture;
        unordered_map<uint32, LanguageChangedCallback> _mapCallback;

        mutable std::mutex  _missingMutex;
        mutable set<string> _uniqueMissingKey;

        std::atomic<uint32> _textRevision;
        uint32              _nextCallbackID;
    };
} // namespace sw
