/**
 * @file LocalizationManager.h
 * @brief 다국어(로컬라이제이션) 관리자를 정의합니다. 싱글톤이 아니라 엔진 서비스 `engine::getSubsystem<LocalizationManager>()` 로 얻습니다.
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
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Localization/CultureInfo.h"
#include "Engine/Localization/LocalizationBinaryTable.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Serialization/Base/ReloadOutcome.h"

namespace sw
{
    class StringTable;
    class TextArgumentList;

    /** @brief 프로젝트를 누가 로드했는지 나타냅니다. 게임을 다시 로드해도 엔진 문자열은 남습니다. */
    enum class LocalizationScope : uint8
    {
        Engine = 0,
        Game,
        Editor ///< 에디터 UI 문구입니다. 에디터 모듈이 로드하고 언로드하며 배포본에는 없습니다.
    };

    /** @brief 문화권 하나의 번역 상태 개수입니다(로드한 프로젝트 전체). */
    struct LocalizationCultureStatistics
    {
        uint32 _currentCount{ 0 }; ///< 화면에 나오는 번역입니다.
        uint32 _staleCount{ 0 };   ///< 원문이 바뀐 뒤의 번역(나오지 않는다)
        uint32 _reviewCount{ 0 };  ///< 검토 표시(나오지 않는다)
        uint32 _orphanCount{ 0 };  ///< 원문 테이블에 없는 키의 번역입니다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class LocalizationManager
     * @brief 문화권 테이블과 로컬라이제이션 프로젝트(원문 테이블 + 문화권 번역 테이블)를 로드해 키를 지금 문화권의 텍스트로 바꾸고, 문화권에 맞춰 메시지를 포맷합니다.
     * @details 조회 순서는 지금 문화권, 그 부모(`ko_kr` → `ko`), 폴백 문화권, 그 부모, 로드한 프로젝트들의 원문 문화권입니다(언리얼 culture fallback,
     *          유니티 Locale fallback). 낡은(stale) 번역과 검토(review) 번역은 화면에 내지 않고 순서의 다음으로 넘어갑니다.
     *          의사 문화권(`qps_ploc`, `qps_plocm`, 문화권 테이블의 `pseudo`)은 원문을 변환해 만든 테이블이고 Dev 빌드에서만 고를 수 있습니다.
     *          조회가 돌려주는 `const utf8*` 는 추가 전용 저장소(`StringTable.cpp`)에 있어 다시 읽기와 언어 변경 뒤에도 유효합니다.
     */
    class SW_API LocalizationManager : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. 초기화 · 종료할 것이 없고 수명(서비스 범위 — 엔진 범위보다 먼저 서고 나중에 사라진다)만 컬렉션이 정한다. */
        static constexpr const utf8* kSubsystemName = "Localization";

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        using LanguageChangedCallback = sw::Delegate<void( string_view oldLanguage, string_view newLanguage )>;

        LocalizationManager();
        ~LocalizationManager() override;

        LocalizationManager( const LocalizationManager& )            = delete;
        LocalizationManager& operator=( const LocalizationManager& ) = delete;

        /** @brief 언어 코드의 정규 표기입니다. 소문자로 쓰고 `-` 는 `_` 로 바꿉니다(`ko-KR`, `ko_KR` → `ko_kr`). 테이블에 넣고 찾는 모든 경로가 이 함수를 거칩니다. */
        static string normalizeLanguageCode( string_view languageCode );

        // ------------------------------------------------------------------------------
        // 1) 초기화와 해제
        // ------------------------------------------------------------------------------
        /** @brief 프로젝트, 낱개 테이블, 지금 언어, 콜백, 빠진 키 기록을 비웁니다. 문화권 테이블은 남깁니다. */
        void clear();

        // ------------------------------------------------------------------------------
        // 2) 데이터: 문화권 테이블, 프로젝트, 낱개 테이블
        // ------------------------------------------------------------------------------
        /** @brief 문화권 테이블(`*.cultures.xml`)을 읽습니다. 의사 문화권의 테이블도 다시 만듭니다. */
        [[nodiscard]] bool loadCultureTable( string_view resourcePath );
        /** @brief 문화권 테이블을 XML 텍스트에서 읽습니다(테스트와 도구). */
        [[nodiscard]] bool loadCultureTableXML( string_view xmlText );
        /** @brief @p culture 의 형식 데이터를 값으로 반환합니다(없으면 부모, 끝까지 없으면 기본값). */
        CultureInfo resolveCulture( string_view culture ) const;

        /**
         * @brief 로컬라이제이션 프로젝트(`*.locproject.xml`, 리소스 경로 또는 절대 경로)를 로드합니다. 원문 테이블들과 문화권 번역 테이블들을 읽습니다.
         * @details 같은 이름의 프로젝트가 이미 있으면 바꾼다. 원문 테이블 두 개에 같은 키가 있으면 오류이고 앞의 것을 쓴다.
         */
        [[nodiscard]] bool mountProject( string_view projectPath, LocalizationScope scope );
        /** @brief 그 범위의 프로젝트를 내립니다. */
        void unmountProjects( LocalizationScope scope );
        /** @brief 로드한 프로젝트 이름입니다(로드한 순서). */
        vector<string> getMountedProjectNames() const;
        /**
         * @brief @p source 의 문화권 테이블, 로드한 프로젝트, 낱개 테이블, 폴백 언어, 지금 언어를 이 관리자에 복사합니다(원래 것은 버립니다).
         * @details 관리자는 싱글톤이 아니므로 화면 하나만 다른 언어로 보이는 길이다 — 에디터 UI 미리보기가 사본을 만들어 자기 언어를 고른다
         *          (`UISystem::setOffscreenLocalization`). 파일은 다시 읽지 않고, 번역 상태 수와 의사 문화권 표도 사본에서 다시 만든다.
         *          콜백과 빠진 키 기록은 복사하지 않는다. 텍스트 버전은 오른다.
         */
        void copyStateFrom( const LocalizationManager& source );
        /**
         * @brief 바뀐 파일이 로드한 프로젝트의 것이면 그 프로젝트를 다시 읽습니다(핫 리로드).
         * @return 다시 읽었으면 `Reloaded`, 로드한 프로젝트의 파일이 아니면 `NotTracked`, 다시 읽지 못했으면(오류를 남기고 이전 글을 지켰다) `Failed`
         */
        [[nodiscard]] ReloadOutcome reloadChangedFile( string_view changedPath );
        /** @brief 그 경로가 로드한 프로젝트의 파일(프로젝트, 원문 테이블, 번역 테이블)인지 확인합니다. */
        bool isProjectFile( string_view path ) const;

        /** @brief 번역 테이블 XML(`TranslationTable` 형식)을 낱개 테이블로 로드합니다. 원문 확인 없이 프로젝트 위에 덮어씁니다(테스트와 도구). */
        [[nodiscard]] bool loadLanguageXML( string_view languageCode, string_view xmlText );
        /** @brief 낱개 테이블에 문자열 하나를 넣습니다. */
        void setString( string_view languageCode, const hashed_string& key, string_view value );
        /** @brief 그 언어의 낱개 테이블과 이진 표를 언로드합니다(프로젝트의 텍스트는 남습니다). */
        void unloadLanguage( string_view languageCode );

        /**
         * @brief 쿠킹된 이진 번역 표(`LocalizationBinaryTable`, `.locres`)를 읽어 그 문화권의 글을 프로젝트 위에 덮습니다. 낱개 테이블(`loadLanguageXML`)보다는 아래입니다.
         * @details 저작 파일을 두지 않는 배포본이 쓴다. 같은 문화권의 표를 다시 로드하면 바꿉니다. 깨진 바이트는 거절하고 아무것도 바꾸지 않습니다.
         */
        [[nodiscard]] bool loadBinaryTable( const uint8* pData, size_t size );
        /** @brief 리소스 경로(또는 절대 경로)의 이진 번역 표를 읽습니다. */
        [[nodiscard]] bool loadBinaryTableResource( string_view resourcePath );
        /**
         * @brief 로드한 프로젝트에서 그 문화권의 화면에 나오는 글(낡은 번역 · 검토 번역 제외)을 이진 번역 표 바이트로 베이크합니다(쿠킹 단계가 부른다).
         * @return 그 문화권의 글이 하나도 없으면 false 입니다.
         */
        [[nodiscard]] bool cookBinaryTable( string_view languageCode, vector<uint8>& outBytes ) const;

        /**
         * @brief 게임 프로젝트를 로드하고(앞의 게임 프로젝트는 언로드합니다) 활성 언어와 폴백 언어를 정합니다. 순서는 명령줄 `-lang`, @p defaultLanguage, 폴백, 아무 언어입니다.
         * @return 프로젝트를 로드했으면 true 입니다.
         * @details 언어 기본값은 코드에 두지 않는다 — 게임은 `GameSettings` 의 기본 · 폴백 언어를 넘긴다.
         */
        bool initialize( string_view projectPath, string_view defaultLanguage, string_view fallbackLanguage );

        // ------------------------------------------------------------------------------
        // 3) 언어 설정 및 조회
        // ------------------------------------------------------------------------------
        /** @brief 현재 활성 언어를 설정합니다. 언어가 바뀌면 텍스트 버전(revision)을 올리고 등록된 콜백을 부릅니다. */
        bool setCurrentLanguage( string_view languageCode );
        /** @brief 현재 활성 언어 코드를 값으로 반환합니다(락을 놓은 뒤의 참조는 사라진 버퍼를 가리킬 수 있다). */
        string getCurrentLanguage() const;
        /** @brief 폴백 언어를 설정합니다. */
        void setFallbackLanguage( string_view languageCode );
        /** @brief 폴백 언어 코드를 값으로 반환합니다. */
        string getFallbackLanguage() const;
        /** @brief 그 언어의 텍스트(원문, 번역, 낱개 테이블, 의사 문화권)가 있는지 확인합니다. */
        bool hasLanguage( string_view languageCode ) const;
        /** @brief 고를 수 있는 언어 코드입니다(사전 순). 의사 문화권은 Dev 빌드에서만 들어 있습니다. */
        vector<string> getAvailableLanguages() const;
        /** @brief 고를 수 있는 언어 수입니다. */
        size_t getLanguageCount() const;
        /** @brief 지금의 조회 사슬입니다(앞이 먼저). */
        vector<string> getLookupChain() const;
        /** @brief 지금 문화권이 오른쪽→왼쪽인지입니다(UI 배치를 뒤집는다). */
        bool isRightToLeft() const;
        /** @brief 그 문화권(빈 값이면 지금 문화권)의 폰트 대체 목록입니다. 적힌 것이 없으면 폴백 문화권의 것입니다. */
        vector<string> getFontFallback( string_view languageCode = {} ) const;
        /** @brief 문화권 하나의 번역 상태 수입니다. */
        LocalizationCultureStatistics getStatistics( string_view languageCode ) const;
        /** @brief 텍스트가 바뀔 때마다(언어 변경, 다시 읽기, 로드) 오르는 버전 번호입니다. UI 는 이 번호가 바뀌면 다시 묻습니다(언리얼 TextRevision). */
        uint32 getTextRevision() const;

        // ------------------------------------------------------------------------------
        // 4) 문자열 조회와 포맷
        // ------------------------------------------------------------------------------
        /**
         * @brief 조회 사슬에서 키를 찾아 보일 글을 돌려줍니다. 어디에도 없으면 빠진 키로 알리고(`Localization.MissingKey`) **키 이름**을 돌려줍니다.
         * @details 빈 글로 숨기지 않는다 — 빠진 번역이 화면에서 키로 보여야 고칠 수 있다(언리얼 FText 가 빠진 키를 키 그대로 보이는 것과 같다).
         *          없음을 스스로 다룰 곳은 `findString` 을 쓴다.
         */
        const utf8* getString( const hashed_string& key ) const;
        /** @brief 조회 사슬에서 키를 찾습니다. 없으면 nullptr 이고 빠진 키로 알리지 않습니다. */
        [[nodiscard]] const utf8* findString( const hashed_string& key ) const;
        /**
         * @brief 키를 **intern 하지 않고** 찾습니다. 없으면 nullptr 이고 빠진 키로 알리지 않습니다.
         * @details 물어보는 텍스트가 키가 아닐 수도 있는 곳(대사 원문 등)에서 쓴다. `hashed_string` 을 만들면 그 텍스트가 intern 아레나에 영구히 남는다.
         *          없을 때 무엇을 보일지(텍스트 그대로 · 원문)는 부르는 쪽이 정한다.
         */
        [[nodiscard]] const utf8* findStringByText( string_view keyText ) const;
        /** @brief 지정한 언어에서만 찾습니다(사슬 없음). 없으면 nullptr 입니다. */
        [[nodiscard]] const utf8* findStringFromLanguage( string_view languageCode, const hashed_string& key ) const;
        /** @brief 조회 사슬 어딘가에 그 키가 있는지 확인합니다. */
        bool hasString( const hashed_string& key ) const;
        /** @brief 지정한 언어의 실행 테이블입니다(없으면 nullptr). 테이블 객체는 언어가 사는 동안 같은 주소입니다. */
        const StringTable* getLanguageTable( string_view languageCode ) const;

        /** @brief 메시지 패턴을 지금 문화권으로 포맷합니다(`TextFormatter`). 오류는 경고로 알리고, 텍스트는 그래도 돌려줍니다. */
        string formatText( string_view pattern, const TextArgumentList& arguments ) const;
        /** @brief 키의 글을 찾아 포맷합니다. 없으면 빠진 키로 알리고 @p pFallbackPattern 을 포맷합니다(부르는 쪽이 정한다 — 숨은 빈 글은 없다). */
        string getFormattedString( const hashed_string& key, const TextArgumentList& arguments, const utf8* pFallbackPattern ) const;

        /** @brief 빠진 키를 기록합니다(처음 한 번 경고). 로드한 텍스트가 하나도 없으면 기록하지 않습니다. `SW_LOCTEXT` 가 부릅니다. */
        void reportMissingKey( string_view key ) const;
        /** @brief 지금까지 빠진 키입니다(사전 순). */
        vector<string> getMissingKeys() const;
        /** @brief 빠진 키 기록을 비웁니다. */
        void clearMissingKeys();

        // ------------------------------------------------------------------------------
        // 5) 언어 변경 이벤트 알림
        // ------------------------------------------------------------------------------
        /** @brief 텍스트가 바뀔 때(언어 변경, 같은 언어의 다시 읽기이며 이때는 두 인자가 같습니다) 부를 콜백을 등록하고 ID 를 반환합니다. */
        uint32 registerLanguageChangedCallback( LanguageChangedCallback callback );
        /** @brief 등록된 콜백을 해제합니다. */
        void unregisterLanguageChangedCallback( uint32 callbackID );

    private:
        /** @brief 로드한 프로젝트 하나입니다. 읽은 문서와 그 파일 경로를 가집니다. */
        struct MountedProject
        {
            LocalizationProject          _project;
            string                       _projectPath;
            vector<string>               _listFilePath; ///< 프로젝트, 원문 테이블, 번역 테이블의 경로입니다(있든 없든 포함하며, 새로 생긴 번역 테이블도 다시 읽습니다).
            map<string, SourceTextEntry> _mapSource;    ///< 원문 테이블들을 합친 것입니다.
            vector<TranslationTable>     _listTranslation;
            LocalizationScope            _scope{ LocalizationScope::Game };
        };

        /** @brief 프로젝트 파일 세트를 읽어 @p outProject 를 채웁니다. 프로젝트나 원문 테이블을 읽지 못하면 false 입니다. */
        [[nodiscard]] static bool readProject( string_view projectPath, MountedProject& outProject );
        /** @brief 읽은 문서들로 실행 테이블을 다시 채웁니다(`_mutex` 를 잡은 상태에서 부릅니다). */
        void rebuildTablesLocked();
        /** @brief 조회 순서를 다시 계산합니다(`_mutex` 를 잡은 상태에서 부릅니다). */
        void rebuildLookupChainLocked();
        /** @brief 그 코드의 실행 테이블입니다. 없으면 만듭니다(`_mutex` 를 잡은 상태에서 부릅니다). */
        StringTable& getOrCreateTableLocked( const string& code );
        /** @brief 미리 구한 해시로 조회 사슬을 한 번 훑습니다. */
        const utf8* findByHash( uint64 keyHash ) const;
        /** @brief 텍스트 버전을 올리고 콜백을 부릅니다. */
        void notifyLanguageChanged( string_view oldLanguage, string_view newLanguage );

    private:
        mutable std::shared_mutex                      _mutex;
        string                                         _currentLanguage;
        string                                         _fallbackLanguage;
        CultureTable                                   _cultureTable;
        CultureInfo                                    _currentCulture; ///< 지금 문화권의 형식 데이터(사슬을 다시 셀 때 함께)
        vector<MountedProject>                         _listProject;
        map<string, TranslationTable>                  _mapLooseTable;
        map<string, LocalizationBinaryTable>           _mapBinaryTable; ///< 문화권 코드 → 쿠킹된 이진 표
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
