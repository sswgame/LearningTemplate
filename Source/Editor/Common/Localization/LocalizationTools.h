/**
 * @file LocalizationTools.h
 * @brief 로컬라이제이션 도구 명령 — 글 수집(`App --gather-text` · `--check-text`) · 번역 교환(`--export-po` · `--import-po=<파일>`)입니다. 소스 트리를 읽는 개발 도구라 에디터 모듈이 맡습니다.
 * @details 언리얼 Localization Dashboard 의 Gather Text 와 같은 자리입니다. 프로젝트마다:
 *          1) `codeRoots`(저장소 상대)의 `.h` · `.cpp` · `.inl` 에서 `SW_LOCTEXT` · `SW_LOCFORMAT`
 *          2) `assetRoots`(리소스 경로)의 리플렉션 XML(`Meta = "Localizable"` 프로퍼티) · `.dialogue.json` · 프로젝트의 `assetRules` 에 맞는 XML 속성
 *          을 모아 첫 원문 표에 합치고(더해짐 · 바뀜 · 지워짐 보고), 문화권 번역 표의 상태(지금 · 낡음 · 검토 · 없음)와 자리표시자 · 최대 길이를 검사합니다.
 *          그 다음 번역 메모리(`tm/<culture>.tm.json`)에 지금 번역을 쌓고, 없는 · 낡은 번역을 메모리의 같은(그대로) · 비슷한(검토 표시) 번역으로 미리 채우며,
 *          원문 표에서 사라진 키의 번역은 번역 표에서 뺍니다(메모리에는 남는다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Localization/TextGatherer.h"

namespace sw
{
    class LocalizationProject;

    /** @brief 문화권 하나의 번역 상태 수입니다(수집 뒤). */
    struct LocalizationCultureReport
    {
        string _culture;
        uint32 _currentCount{ 0 };
        uint32 _staleCount{ 0 };
        uint32 _reviewCount{ 0 };
        uint32 _missingCount{ 0 };
        uint32 _orphanCount{ 0 };    ///< 원문 표에 없는 키의 번역(번역 표에서 뺐다)
        uint32 _prefilledExact{ 0 }; ///< 번역 메모리의 같은 원문으로 채움
        uint32 _prefilledFuzzy{ 0 }; ///< 번역 메모리의 비슷한 원문으로 채움(검토 표시)
        bool   _bChanged{ false };   ///< 번역 표 · 번역 메모리를 고쳤다
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
        bool                              _bOutOfDate{ false }; ///< 디스크의 표가 수집 결과와 다르다(확인 모드의 실패 이유)
        bool                              _bWritten{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief PO 내보내기 · 가져오기 하나의 결과입니다. */
    struct LocalizationExchangeResult
    {
        string _culture;
        string _path;
        uint32 _entryCount{ 0 };      ///< PO 항목 수
        uint32 _translatedCount{ 0 }; ///< 번역이 있는 항목
        uint32 _staleCount{ 0 };      ///< 옛 원문의 번역(내보내기: fuzzy + `#|` · 가져오기: 그 원문 해시로 넣었다)
        uint32 _fuzzyCount{ 0 };      ///< 검토 표시
        uint32 _unknownKeyCount{ 0 }; ///< 가져오기: 원문 표에 없는 키(건너뜀)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LocalizationTools
     * @brief 상태 없는 도구 함수입니다. 헤드리스 진입점(`runEditorLocalizationTask`, App 이 에디터 모듈을 올려 부른다)과 시험이 부릅니다.
     */
    struct LocalizationTools
    {
        /** @brief 저장소 루트(리소스 루트의 부모)입니다. */
        static string findRepositoryRoot();
        /** @brief `-loc-project=all` — 모든 팩을 고르는 프로젝트 인자입니다. */
        static constexpr const utf8* kAllProjects = "all";

        /**
         * @brief 기본 대상 프로젝트 — 엔진 프로젝트와 활성 게임 팩(@p bAllGames 면 `Resource/game` 아래 모든 팩)의 `data/localization` 프로젝트 파일입니다(절대 경로).
         */
        static void collectProjectPaths( vector<string>& outListProjectPath, bool bAllGames = false );
        /**
         * @brief 프로젝트 하나를 수집합니다. @p bWrite 면 바뀐 원문 표 · 번역 표를 쓰고, 아니면 다르다는 것만 적습니다(`_bOutOfDate`).
         * @return 프로젝트 · 표를 읽지 못했으면 false 입니다(수집 오류는 결과의 보고에 있다).
         */
        [[nodiscard]] static bool gatherProject( string_view projectPath, string_view repositoryRoot, bool bWrite, LocalizationGatherResult& outResult );
        /** @brief 리소스 파일 하나를 프로젝트 규칙으로 훑습니다(대화 · 규칙 XML · 리플렉션 XML). */
        static void gatherAssetFile( const LocalizationProject& project, TextGatherer& gatherer, string_view fileText, string_view origin );
        /** @brief 결과를 로그로 씁니다(사람이 읽는 보고). */
        static void logGatherResult( const LocalizationGatherResult& result );
        /**
         * @brief `--gather-text` · `--check-text` 의 본문입니다. @p projectArgument 가 비면 기본 대상 전부입니다.
         * @return 오류가 없고(확인 모드면 표가 최신이고) 모든 프로젝트를 읽었으면 true 입니다 — 프로세스 종료 코드가 된다.
         */
        static bool runGatherCommand( bool bCheckOnly, string_view projectArgument );

        /** @brief 프로젝트의 문화권마다 `po/<culture>.po` 를 씁니다(원문 · 맥락 · 설명 · 최대 길이 · 자리 · 번역 · 번역가 메모 · fuzzy · 옛 원문). */
        [[nodiscard]] static bool exportProjectPo( string_view projectPath, vector<LocalizationExchangeResult>& outListResult );
        /**
         * @brief PO 하나를 프로젝트의 번역 표로 가져옵니다. 문화권은 PO 머리의 `Language`, 번역의 해시는 그 msgid(번역가가 본 원문)의 해시입니다 —
         *        그 사이 원문이 바뀌었으면 가져온 번역은 낡은 것으로 남습니다. `#, fuzzy` 는 검토 표시, `# ` 줄은 번역가 메모입니다.
         */
        [[nodiscard]] static bool importPo( string_view projectPath, string_view poPath, LocalizationExchangeResult& outResult );
        /** @brief `--export-po` 의 본문입니다. */
        static bool runExportCommand( string_view projectArgument );
        /** @brief `--import-po=<파일>` 의 본문입니다. 프로젝트는 `-loc-project`, 없으면 PO 머리의 `X-Localization-Project` 이름으로 고릅니다. */
        static bool runImportCommand( string_view poPath, string_view projectArgument );
    };
} // namespace sw
