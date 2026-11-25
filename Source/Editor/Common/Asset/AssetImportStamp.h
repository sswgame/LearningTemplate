/**
 * @file AssetImportStamp.h
 * @brief 원본 폴더(`<kind>_raw/`)와 임포트 결과를 내용 해시 스탬프(`import.stamp`)로 대조하고, 어긋난 것을 임포트하는 공통 절차입니다.
 * @details 텍스처(`textures_raw/` → DDS)와 모델(`models_raw/` → `.mesh`)이 같은 절차를 쓴다. 종류마다 다른 것은 `IRawAssetImporter` 가 답한다.
 *          원본 확장자, 결과 경로, 원본 해시, 임포트 자체가 그 예다. 폴더 스캔, 스탬프 읽기와 쓰기, 어긋남 판정은 여기에 한 벌만 있다.
 */
#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

namespace sw
{
    class AssetPaths;
    class DerivedDataCache;
    class TaskManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @enum AssetImportMode
     * @brief 일괄 임포트가 어긋난 원본을 만났을 때 할 일을 정합니다.
     */
    enum class AssetImportMode : uint8
    {
        ImportStale = 0, ///< 스탬프와 어긋난 원본만 다시 임포트하고 `import.stamp` 를 갱신합니다.
        CheckOnly,       ///< 아무 파일도 쓰지 않고 어긋난 것만 보고합니다. 테스트와 CI 에서 씁니다.
    };

    /**
     * @struct AssetImportSummary
     * @brief 일괄 임포트 한 번의 결과입니다. `_listProblem` 이 비어 있어야 원본과 임포트 결과가 맞습니다.
     */
    struct AssetImportSummary
    {
        /** @brief 사람이 읽을 수 있는 한 줄 메시지의 목록입니다. CheckOnly 는 어긋남을, ImportStale 은 임포트하지 못한 것을 담습니다. */
        vector<string> _listProblem;
        /** @brief 취소로 보지 않은 원본(리소스 경로)입니다(`AssetImportControl`). 스탬프 줄은 그대로다. */
        vector<string> _listSkippedPath;
        /** @brief 이번에 본 원본(리소스 경로 — 규칙을 고르는 경로)의 정렬한 목록입니다. 뒤에 원본 전체를 보는 일(죽은 규칙 · 공유 저장소 정리)이 트리를 다시 훑지 않는다. */
        vector<string> _listSourcePath;
        uint32         _sourceCount;
        uint32         _importedCount;

        AssetImportSummary()
            : _listProblem{}
            , _listSkippedPath{}
            , _listSourcePath{}
            , _sourceCount{ 0 }
            , _importedCount{ 0 }
        {
        }

        bool isClean() const { return _listProblem.empty(); }

        /** @brief 시작하지 못한 임포트의 요약입니다(문제 한 줄 — 예: DDC 설정을 읽지 못했다). */
        static AssetImportSummary makeFailed( string problem )
        {
            AssetImportSummary summary;
            summary._listProblem.push_back( std::move( problem ) );
            return summary;
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct AssetImportControl
     * @brief 부른 쪽이 도는 일괄 임포트를 멈추고 진행을 읽는 자리입니다. 임포트 도중인 원본은 끝까지 가고, 남은 원본은 건너뛴다(`AssetImportSummary::_listSkippedPath`).
     */
    struct AssetImportControl
    {
        StopToken       _cancel{};                  ///< 부른 쪽의 취소
        StopToken       _shutdown{};                ///< 에디터 종료
        atomic<uint32>* _pFinishedCount{ nullptr }; ///< 끝난 원본 수(빌림, 아무 스레드나 더한다). nullptr 이면 세지 않는다

        bool isCanceled() const { return _cancel.isStopRequested() || _shutdown.isStopRequested(); }
        void recordFinished() const
        {
            if ( _pFinishedCount != nullptr )
                _pFinishedCount->fetch_add( 1, std::memory_order_relaxed );
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 바뀐 원본 하나와 그 결과입니다(`AssetSourceBatch`). */
    struct AssetSourceBatchItem
    {
        string _relativePath;      ///< 리소스 루트 기준 원본 경로
        bool   _bRan{ false };     ///< 임포터가 보았다(취소로 건너뛰었으면 false — 부른 쪽이 다시 요청한다)
        bool   _bHandled{ false }; ///< 임포터가 원본으로 처리했다 — 임포트했거나 임포트하지 않는다고 알렸다(false 면 부른 쪽이 캐시를 다시 읽어야 한다)
    };

    /**
     * @struct AssetSourceBatch
     * @brief 에디터 핫 리로드가 한 임포터에 한 번에 넘기는 바뀐 원본들입니다(`TextureImporter::importChangedSources` 등). 워커에서 불린다.
     * @details 원본마다 따로 부르지 않는다 — 한 묶음이라야 같은 원본 폴더의 스탬프를 한 번에 쓰고, 다른 원본의 결과를 읽는 원본(장비 메시 → 몸 메시)이 그 원본 뒤에 임포트된다(`AssetImportStampUtil::importSources`).
     *          예전에는 폴더 묶음마다 동시에 따로 불러 같은 `import.stamp` 를 서로 덮었고, 몸과 장비를 함께 고치면 장비가 옛 몸을 읽을 수 있었다.
     */
    struct AssetSourceBatch
    {
        vector<AssetSourceBatchItem> _listItem;
        const AssetPaths*            _pAssetPaths{ nullptr };  ///< 경로 상태(엔진 수명) — 워커는 찾지 않고 요청한 스레드가 넣는다
        TaskManager*                 _pTaskManager{ nullptr }; ///< 원본을 나눠 돌 작업 시스템(빌림 — 요청한 스레드가 찾는다). nullptr 이면 차례로 돈다
        AssetImportControl           _control{};
    };

    /** @brief 바뀐 파일 하나가 다시 임포트해야 하는 원본들입니다 — 곁 파일(구멍 마스크)은 그것을 쓰는 원본으로 옮긴다. */
    struct AssetSourceBatchEntry
    {
        size_t         _itemIndex{ 0 }; ///< `AssetSourceBatch::_listItem` 의 번호
        vector<string> _listSourcePath; ///< 리소스 루트 기준
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class IRawAssetImporter
     * @brief 원본 종류 하나가 일괄 임포트(`AssetImportStampUtil::importAll`)에 제공하는 함수들입니다.
     * @details 임포터 규칙(일괄 임포트는 원본 파일을 병렬로 임포트한다).
     *          - 모든 `const` 함수는 여러 스레드에서 동시에 불립니다. 임포터는 무상태여야 합니다. 원본 하나의 결과는 반환값으로만 돌려주고(공유 가변 상태 · 함수 밖 static 금지),
     *            로그는 마음대로 남깁니다(로거는 스레드 안전입니다).
     *          - 원본 하나의 임포트는 자기 결과 경로(`makeImportedPath` 에서 확장자를 뗀 경로와 그 아래 옆 폴더)에만 쓰고 지웁니다. 같은 경로(또는 그 아래)를 쓰는 원본들은 일괄 임포트가
     *            한 사슬로 묶어 한 스레드가 파일 순서대로 임포트합니다.
     *            공유 저장소(`ImportedAssetStore`)는 예외다 — 이름이 내용에서 나와 두 원본이 같은 자리를 쓰면 바이트가 같다(`writeIfAbsent`).
     *          - 다른 원본의 임포트 결과를 읽는 순서 의존(장비 메시가 몸 메시를 읽는다)은 `hasOrderDependency` 로 드러냅니다. 그 원본은 의존이 없는 원본이 모두 끝난 뒤 병렬로 임포트합니다.
     *            의존 대상 자신은 의존이 없어야 합니다(의존하는 원본끼리는 순서가 없습니다).
     */
    class IRawAssetImporter
    {
    public:
        IRawAssetImporter()          = default;
        virtual ~IRawAssetImporter() = default;

        IRawAssetImporter( const IRawAssetImporter& )            = delete;
        IRawAssetImporter& operator=( const IRawAssetImporter& ) = delete;

        /** @brief 원본을 두는 폴더 이름입니다("textures_raw"). 쿠킹이 팩에서 뺍니다(정체성 규칙 `AssetIdentity.json` 의 원본 폴더). */
        virtual string_view getRawFolderName() const = 0;
        /** @brief 스탬프의 헤더 줄입니다. 형식이나 판정이 바뀌면 올립니다. 이전 스탬프는 모두 어긋남이 되어 한 번 다시 임포트합니다. */
        virtual string_view getStampHeader() const = 0;
        /** @brief 원본 폴더마다 하나 두는 스탬프 파일 이름입니다. 같은 원본에서 다른 결과를 내는 종류(텍스처 형식 변형)만 자기 이름을 씁니다. */
        virtual string_view getStampFileName() const { return "import.stamp"; }
        /** @brief 문제 메시지에 쓰는 결과의 이름입니다("DDS", "메시"). */
        virtual string_view getImportedLabel() const = 0;
        /** @brief 임포트하는 원본 확장자인지 확인합니다. `import.stamp` 같은 파일은 원본이 아닙니다. */
        virtual bool isSourceFile( string_view path ) const = 0;
        /** @brief 원본 경로에 해당하는 결과 경로를 반환합니다. 원본 폴더 구간이 없으면 빈 문자열입니다. */
        virtual AbsolutePath makeImportedPath( const AbsolutePath& sourcePath ) const = 0;
        /** @brief 원본 바이트, 적용한 규칙, 임포터 버전을 섞은 해시입니다. 읽지 못하면 0 입니다. */
        virtual uint64 computeSourceHash( const AbsolutePath& sourcePath, string_view assetPath ) const = 0;
        /**
         * @brief 임포트 결과의 해시입니다. 결과가 없으면 0 입니다. 기본은 결과 파일 하나의 바이트입니다.
         * @details 결과가 여러 파일인 종류는 그것을 모두 섞어 반환한다. 스킨드 모델은 메시와 옆 폴더의 스켈레톤, 클립이 해당한다. 그래야 옆 파일을 손대거나
         *          지워도 어긋남으로 잡힌다.
         */
        virtual uint64 computeImportedHash( const AbsolutePath& importedPath ) const;
        /**
         * @brief 임포트 결과 파일(절대 경로) 목록입니다. 기본은 결과 파일 하나입니다(있으면). `computeImportedHash` 가 섞는 파일과 같아야 합니다.
         * @details DDC 가 이 파일들을 (이름, 바이트) 한 덩어리로 담고 히트에서 그대로 되살린다 — 빠진 파일은 히트 뒤에 없다.
         */
        virtual void collectImportedFiles( const AbsolutePath& importedPath, vector<AbsolutePath>& outListPath ) const;
        /** @brief DDC 키에 넣는 임포터 코드 지문(`CookCodeFingerprint::find`)입니다. 0 이면 `Config/Dev/CookCodeInputs.json` 에 없는 것이라 DDC 를 쓰는 임포트가 오류로 멈춥니다. */
        virtual uint64 findCodeFingerprint() const = 0;
        /**
         * @brief 결과 글에 자기 리소스 경로가 박히면 true 입니다(기본 false). 모델은 `_shared/` · 머티리얼 경로를 적는다.
         * @details true 면 DDC 키에 리소스 경로를 넣고 결과 파일 이름을 리소스 루트 기준으로 담는다. false 면 경로를 키에 넣지 않아 복제본 · 옮긴 원본도 같은 항목을 쓰고
         *          (유니티는 GUID 를 넣어 복제본을 다시 임포트한다), 결과 파일 이름은 결과 폴더 기준이다.
         */
        virtual bool isImportedPathEmbedded() const { return false; }
        /** @brief 임포트하지 않는 원본이면 그 이유를 반환하고, 아니면 nullptr 입니다. */
        virtual const utf8* findUnsupportedReason( const AbsolutePath& sourcePath ) const = 0;
        /**
         * @brief 이 원본의 임포트가 다른 원본의 임포트 결과를 읽으면 true 입니다(기본은 false). 클래스 설명의 임포터 규칙을 보십시오.
         * @details 병렬 구간의 워커가 부른다. 읽는 대상이 같은 실행에서 임포트될 수 있으면 그 결과가 먼저 나와야 하므로 데이터로 드러낸다. 조용히 두면 병렬 구간에서 읽는 쪽이 옛 결과를 본다.
         */
        virtual bool hasOrderDependency( [[maybe_unused]] const AbsolutePath& sourcePath, [[maybe_unused]] string_view assetPath ) const { return false; }
        /**
         * @brief 원본 하나를 결과 경로로 임포트합니다.
         * @details 스레드 계약: 워커에서 서로 다른 원본에 대해 동시에 불린다(같은 결과 경로를 쓰는 원본은 한 스레드가 순서대로). 이 임포터의 공유 상태, `AssetPaths` 의 전역 쓰기,
         *          에디터 서비스 범위(`findEditorScope`)를 건드리지 않는다.
         */
        [[nodiscard]] virtual bool importSource( const AbsolutePath& sourcePath, const AbsolutePath& importedPath, string_view assetPath ) const = 0;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct AssetImportStampUtil
     * @brief 원본 폴더를 스캔해 스탬프와 대조하고 어긋난 것을 임포트합니다.
     */
    struct AssetImportStampUtil
    {
        /**
         * @brief 리소스 루트 아래 모든 원본 폴더(@p importer 의 이름)의 원본을 그 폴더의 `import.stamp` 와 대조합니다. @p mode 가 ImportStale 이면
         *        어긋난 것을 임포트합니다.
         * @details 스탬프 한 줄은 `<원본 해시> <결과 해시> <원본 폴더 기준 상대 경로>` 형식이다. 원본 해시에는 규칙과 임포터 버전이 섞이므로 규칙만
         *          바꿔도 어긋남이고, 결과 해시 덕분에 결과를 손댄 경우도 잡힌다. 판정은 파일 시간이 아니라 내용으로 한다. 원본과 결과를 둘 다 커밋하므로
         *          `git` 이 시간 순서를 임의로 뒤집기 때문이다. 원본이 사라진 스탬프 줄도 어긋남이다. ImportStale 은 그 줄과 함께 결과(파일 · 옆 폴더)를 지운다 —
         *          결과는 생성물이라 깨끗한 클론에 없으므로, 남기면 지운 원본을 아직 가리키는 것이 이 PC 에서만 숨는다. 스탬프는 내용이 바뀔 때만 쓴다.
         *
         *          원본 파일은 병렬로 임포트하고 합치기는 직렬로 한다. 모든 원본 폴더의 파일을 한 목록으로 모아 사슬(같은 결과 자리에 쓰는 파일의 묶음)을 만든 뒤 사슬 단위로 작업 시스템(@p pTaskManager)과
         *          나눠 돌고(해시 계산 · 대조 · 임포트가 한 파일의 일이고 큰 원본부터다), `IRawAssetImporter::hasOrderDependency` 인 원본은 나머지가 모두 끝난 둘째 단계에서 병렬로 돈다.
         *          파일마다 결과가 자기 칸에 담기고 요약(`_listProblem` 의 순서 · 카운터)과 스탬프 표는 파일 순서대로 합치므로, 결과 바이트 · 스탬프 · 요약은 직렬로 돌 때와 같다.
         *          작업 시스템이 없으면(테스트 하니스 · 서비스 연결 전) 이 스레드가 사슬을 파일 순서대로 돈다. 임포터 로그의 줄 순서만 병렬로 돌 때 파일 사이에서 섞인다.
         * @param resourceRoot `Content/` 의 절대 경로
         *
         *          어긋난 원본은 임포트하기 전에 @p derivedDataCache 를 찾고, 미스면 임포트한 뒤 넣는다(ImportStale 만). 키 = 임포터 스탬프 머리 + 코드 지문 + 원본 해시
         *          (원본 바이트 · 규칙 · 임포터 판 · 대상 형식) + (`isImportedPathEmbedded` 면) 리소스 경로. 히트는 임포터를 부르지 않고 결과 파일을 되살린다.
         * @param pTaskManager 사슬을 나눠 돌 작업 시스템(빌림, nullptr 이면 이 스레드가 직렬로). 부르는 쪽이 찾아 넘긴다 — 워커에서 불릴 수 있어 여기서는 에디터 서비스 범위(`findEditorScope`)를 읽지 않는다.
         * @param derivedDataCache 결과를 찾고 넣는 DDC(보통 `DerivedDataCache::open` — 부르는 쪽이 읽어 넘긴다, 테스트는 임시 폴더 층)
         */
        [[nodiscard]] static AssetImportSummary importAll( const AbsolutePath& resourceRoot, const IRawAssetImporter& importer, AssetImportMode mode, TaskManager* pTaskManager,
                                                           const DerivedDataCache& derivedDataCache );

        /**
         * @brief @p listAssetPath(리소스 루트 기준, 구분자 · 대소문자 무시)의 원본만 스탬프와 대조하고 어긋났으면 임포트합니다(ImportStale). 에디터 핫 리로드가 바뀐 원본만 임포트하는 길입니다.
         * @details 루트 전체를 훑는 `importAll` 은 비용이 바뀐 파일 수가 아니라 원본 수에 비례한다. 이쪽은 범위 안 원본이 든 원본 폴더만 목록을 내고 범위 밖 원본은 읽지 않는다.
         *          스탬프를 쓰는 길은 `importAll` 과 같은 한 곳이다(에디터가 임포트한 것과 `App --import-*` 가 임포트한 것이 같은 판정을 받는다). 범위 밖 원본의 스탬프 줄은 그대로 둔다.
         *          지우면 다음 전체 대조가 그 원본들을 모두 다시 임포트한다. 사슬과 순서 의존은 범위 안 원본끼리만 따진다.
         *          범위 밖에서 어긋난 것(규칙 파일만 고친 다른 원본 등)은 보정하지 않는다. 그 보정은 `App --import-*` 의 전체 대조가 한다.
         *          작업 시스템(@p pTaskManager, `importAll` 과 같음)을 부르는 쪽이 넘기므로 에디터 스레드에서도 워커에서도 부를 수 있다.
         *          DDC(@p derivedDataCache)는 `importAll` 과 같이 쓴다.
         * @param pControl 취소 · 진행(빌림). nullptr 이면 끝까지 돈다.
         */
        [[nodiscard]] static AssetImportSummary importSources( const AbsolutePath& resourceRoot, const IRawAssetImporter& importer, const vector<string>& listAssetPath,
                                                               TaskManager* pTaskManager, const DerivedDataCache& derivedDataCache, const AssetImportControl* pControl = nullptr );

        /**
         * @brief 핫 리로드 묶음의 @p listEntry 원본을 한 번에 임포트하고(`importSources`) 항목마다 결과를 적습니다. 문제는 오류로 남깁니다. 워커에서 불린다.
         * @details 원본이 든 루트(`Content/` · 작업 도메인)마다 한 번 돈다. DDC 설정을 읽지 못하면 오류를 남기고 처리한 것으로 적는다(다시 읽을 결과가 없다).
         */
        static void importChangedSources( AssetSourceBatch& batch, const vector<AssetSourceBatchEntry>& listEntry, const IRawAssetImporter& importer );

        /** @brief @p path 가 @p rawFolderName 폴더 아래의 파일인지입니다. 에셋 경로 · 디스크 경로 글을 모두 받는다(에셋 종류를 가리는 자리가 둘을 다 묻는다). */
        [[nodiscard]] static bool isUnderRawFolder( string_view path, string_view rawFolderName );
        /**
         * @brief 원본 경로를 결과 경로로 바꿉니다(`<x>/<rawFolder>/<y>.<ext>` → `<x>/<importedFolder>/<y><importedExtension>`).
         * @details 원본 폴더 이름은 경로 구간 경계에서만 찾는다. `my_textures_raw_backup` 은 해당하지 않는다. 없으면 빈 문자열을 반환한다.
         */
        static string makeImportedPath( string_view rawPath, string_view rawFolderName, string_view importedFolderName, string_view importedExtension );
        /** @brief `makeImportedPath` 의 디스크 경로 판입니다 — 원본이 @p rawFolderName 아래가 아니면 빈 경로다. */
        static AbsolutePath makeImportedPath( const AbsolutePath& rawPath, string_view rawFolderName, string_view importedFolderName, string_view importedExtension );

        /** @brief 파일 바이트 그대로 계산한 FNV-1a 64 해시입니다(`ContentHash::computeFile` — 크기 · 쓰기 시각이 그대로면 읽지 않는다). 파일이 없거나 비어 있으면 0 입니다. */
        static uint64 computeFileHash( const AbsolutePath& path );
    };
} // namespace sw::editor
