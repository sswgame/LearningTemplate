/**
 * @file LocalizationTools.h
 * @brief 로컬라이제이션 도구 명령입니다. 텍스트 수집(`App --gather-text`, `--check-text`)과 번역 교환(`--export-po`, `--import-po=<파일>`)을 제공합니다. 소스 트리를 읽는 개발 도구이므로 에디터 모듈이 맡습니다.
 * @details 언리얼의 Localization Dashboard 에 있는 Gather Text 에 해당한다. 프로젝트마다 다음 순서로 처리한다.
 *          1) `codeRoots`(저장소 상대 경로)의 `.h`, `.cpp`, `.inl` 에서 `SW_LOCTEXT` 와 `SW_LOCFORMAT` 을 모은다.
 *          2) `assetRoots`(리소스 경로)의 리플렉션 XML(`Meta = "Localizable"` 프로퍼티), `.dialogue.xml`, 프로젝트의 `assetRules` 에 맞는 XML 속성을 모은다.
 *          모은 문구는 첫 원문 테이블에 합치고(추가, 변경, 삭제를 보고한다), 문화권 번역 테이블의 상태(최신, 오래됨, 검토, 없음)와 자리 표시자, 최대 길이를 검사한다.
 *          그다음 번역 메모리(`tm/<culture>.tm.xml`)에 현재 번역을 쌓는다. 없거나 오래된 번역은 메모리의 같은 번역(그대로)이나 비슷한 번역(검토 표시)으로 미리 채운다.
 *          원문 테이블에서 사라진 키의 번역은 번역 테이블에서 빼고, 메모리에는 남긴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Localization/TextGatherer.h"

namespace sw
{
    class LocalizationProject;

    /** @brief 수집 뒤 문화권 하나의 번역 상태별 개수입니다. */
    struct LocalizationCultureReport
    {
        string _culture;
        uint32 _currentCount{ 0 };
        uint32 _staleCount{ 0 };
        uint32 _reviewCount{ 0 };
        uint32 _missingCount{ 0 };
        uint32 _orphanCount{ 0 };    ///< 원문 테이블에 없는 키의 번역이다. 번역 테이블에서 뺀다
        uint32 _prefilledExact{ 0 }; ///< 번역 메모리의 같은 원문으로 채웠다
        uint32 _prefilledFuzzy{ 0 }; ///< 번역 메모리의 비슷한 원문으로 채웠다(검토 표시)
        bool   _bChanged{ false };   ///< 번역 테이블이나 번역 메모리를 고쳤다
        bool   _bStaged{ false };    ///< 프로젝트의 `stagedCultures` 에 있다 — 출시하는 문화권이라 확인 모드에서 빠지거나 낡은 번역을 허용하지 않는다

        /** @brief 빠졌거나 낡았거나 검토가 남은 번역 수입니다. */
        uint32 countIncomplete() const { return _staleCount + _reviewCount + _missingCount; }
    };
} // namespace sw

namespace sw
{
    /** @brief 프로젝트 하나의 수집 결과입니다. */
    struct LocalizationGatherResult
    {
        string                            _projectName;
        string                            _gatherTablePath;
        TextGatherReport                  _report;
        vector<LocalizationCultureReport> _listCulture;
        uint32                            _fileCount{ 0 };
        bool                              _bOutOfDate{ false }; ///< 디스크의 테이블이 수집 결과와 다르다. 확인 모드의 실패 이유다
        bool                              _bWritten{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief PO 내보내기나 가져오기 한 번의 결과입니다. */
    struct LocalizationExchangeResult
    {
        string _culture;
        string _path;
        uint32 _entryCount{ 0 };      ///< PO 항목 수
        uint32 _translatedCount{ 0 }; ///< 번역이 있는 항목 수
        uint32 _staleCount{ 0 };      ///< 이전 원문의 번역이다. 내보내기에서는 fuzzy 와 `#|` 로 표시하고, 가져오기에서는 그 원문 해시로 넣는다
        uint32 _fuzzyCount{ 0 };      ///< 검토 표시가 붙은 항목 수
        uint32 _unknownKeyCount{ 0 }; ///< 가져오기에서 원문 테이블에 없는 키의 수다. 건너뛴다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LocalizationTools
     * @brief 상태 없는 도구 함수 모음입니다. 헤드리스 진입점(`runEditorLocalizationTask`)과 테스트가 호출합니다. 헤드리스 진입점은 App 이 에디터 모듈을 로드한 뒤 호출합니다.
     */
    struct LocalizationTools
    {
        /** @brief 저장소 루트를 반환합니다. 리소스 루트의 부모입니다. */
        static string findRepositoryRoot();
        /** @brief `-loc-project=all` 은 모든 팩을 고르는 프로젝트 인자입니다. */
        static constexpr const utf8* kAllProjects = "all";

        /**
         * @brief 기본 대상 프로젝트를 절대 경로로 반환합니다. 엔진 프로젝트와 활성 게임 팩의 `data/localization` 프로젝트 파일이며, @p bAllGames 이면 `Content/game` 아래 모든 팩을 포함합니다.
         */
        static void collectProjectPaths( vector<string>& outListProjectPath, bool bAllGames = false );
        /**
         * @brief 프로젝트 하나를 수집합니다. @p bWrite 이면 바뀐 원문 테이블과 번역 테이블을 쓰고, 아니면 다르다는 것만 기록합니다(`_bOutOfDate`).
         * @return 프로젝트나 테이블을 읽지 못했으면 false 입니다. 수집 오류는 결과의 보고에 있습니다.
         */
        [[nodiscard]] static bool gatherProject( string_view projectPath, string_view repositoryRoot, bool bWrite, LocalizationGatherResult& outResult );
        /** @brief 리소스 파일 하나를 프로젝트 규칙으로 스캔합니다. 대화, 규칙 XML, 리플렉션 XML 이 대상입니다. */
        static void gatherAssetFile( const LocalizationProject& project, TextGatherer& gatherer, string_view fileText, string_view origin );
        /** @brief 결과를 로그로 남깁니다. 사람이 읽는 보고입니다. */
        static void logGatherResult( const LocalizationGatherResult& result );
        /**
         * @brief `--gather-text` 와 `--check-text` 의 본문입니다. @p projectArgument 가 비어 있으면 기본 대상을 모두 처리합니다.
         * @return 오류가 없고 모든 프로젝트를 읽었으면 true 입니다. 프로세스 종료 코드가 됩니다.
         * @details 확인 모드(`--check-text`, CI 에서는 nogpu 의 `LocalizationGatherTest.RepositoryProjectsAreUpToDate`)는 더 엄격하다. 원문 테이블 · 번역 테이블 · 번역 메모리가 수집 결과와 같아야 하고,
         *          경고 수준 문제(자리 표시자 · 태그 · 길이)도 없어야 하며, 출시 문화권(`stagedCultures`)은 빠지거나 낡은 번역이 없어야 한다.
         *          새 키를 번역 없이 넣거나 수집을 다시 돌리지 않은 변경은 여기서 걸린다. 목록 밖 문화권(번역가에게 PO 만 낸 것)은 개수만 보고한다.
         */
        static bool runGatherCommand( bool bCheckOnly, string_view projectArgument );
        /** @brief 확인 모드의 통과 조건입니다. 테이블이 최신이고, 문제(경고 포함)가 없고, 출시 문화권의 번역이 모두 최신이면 true 입니다. 실패 이유는 오류로 남깁니다. */
        static bool isCheckClean( const LocalizationGatherResult& result );

        /** @brief 프로젝트의 문화권마다 `po/<culture>.po` 를 씁니다. 원문, 맥락, 설명, 최대 길이, 자리 표시자, 번역, 번역가 메모, fuzzy, 이전 원문을 담습니다. */
        [[nodiscard]] static bool exportProjectPo( string_view projectPath, vector<LocalizationExchangeResult>& outListResult );
        /**
         * @brief PO 하나를 프로젝트의 번역 테이블로 가져옵니다. 문화권은 PO 헤더의 `Language` 이고, 번역의 해시는 그 msgid(번역가가 본 원문)의 해시입니다.
         *        그 사이 원문이 바뀌었으면 가져온 번역은 오래된 것으로 남습니다. `#, fuzzy` 는 검토 표시이고, `# ` 줄은 번역가 메모입니다.
         */
        [[nodiscard]] static bool importPo( string_view projectPath, string_view poPath, LocalizationExchangeResult& outResult );
        /** @brief `--export-po` 의 본문입니다. */
        static bool runExportCommand( string_view projectArgument );
        /** @brief `--import-po=<파일>` 의 본문입니다. 프로젝트는 `-loc-project` 로 고르고, 없으면 PO 헤더의 `X-Localization-Project` 이름으로 고릅니다. */
        static bool runImportCommand( string_view poPath, string_view projectArgument );
    };
} // namespace sw
