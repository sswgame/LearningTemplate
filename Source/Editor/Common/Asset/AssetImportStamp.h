/**
 * @file AssetImportStamp.h
 * @brief 원본 폴더(`<kind>_raw/`)와 임포트 결과를 내용 해시 스탬프(`import.stamp`)로 대조하고, 어긋난 것을 임포트하는 공통 절차입니다.
 * @details 텍스처(`textures_raw/` → DDS)와 모델(`models_raw/` → `.mesh`)이 같은 절차를 쓴다. 종류마다 다른 것은 `IRawAssetImporter` 가 답한다.
 *          원본 확장자, 결과 경로, 원본 해시, 임포트 자체가 그 예다. 폴더 스캔, 스탬프 읽기와 쓰기, 어긋남 판정은 여기에 한 벌만 있다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
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
        uint32         _sourceCount;
        uint32         _importedCount;

        AssetImportSummary()
            : _listProblem{}
            , _sourceCount{ 0 }
            , _importedCount{ 0 }
        {
        }

        bool isClean() const { return _listProblem.empty(); }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class IRawAssetImporter
     * @brief 원본 종류 하나가 일괄 임포트(`AssetImportStampUtil::importAll`)에 제공하는 함수들입니다.
     * @details **임포터 규칙(일괄 임포트는 원본 파일을 병렬로 임포트한다).**
     *          - 모든 `const` 함수는 여러 스레드에서 동시에 불립니다. 임포터는 무상태여야 합니다. 원본 하나의 결과는 반환값으로만 돌려주고(공유 가변 상태 · 함수 밖 static 금지),
     *            로그는 마음대로 남깁니다(로거는 스레드 안전입니다).
     *          - 원본 하나의 임포트는 **자기 결과 경로**(`makeImportedPath` 에서 확장자를 뗀 경로와 그 아래 옆 폴더)에만 쓰고 지웁니다. 같은 경로(또는 그 아래)를 쓰는 원본들은 일괄 임포트가
     *            한 사슬로 묶어 한 스레드가 파일 순서대로 임포트합니다.
     *          - **다른 원본의 임포트 결과를 읽는 순서 의존**(장비 메시가 몸 메시를 읽는다)은 `hasOrderDependency` 로 드러냅니다. 그 원본은 의존이 없는 원본이 모두 끝난 뒤 이 스레드에서 직렬로 임포트합니다.
     *            의존 대상 자신은 의존이 없어야 합니다(의존끼리의 순서는 파일 순서일 뿐입니다).
     */
    class IRawAssetImporter
    {
    public:
        IRawAssetImporter()          = default;
        virtual ~IRawAssetImporter() = default;

        IRawAssetImporter( const IRawAssetImporter& )            = delete;
        IRawAssetImporter& operator=( const IRawAssetImporter& ) = delete;

        /** @brief 원본을 두는 폴더 이름입니다("textures_raw"). 쿠킹이 팩에서 뺍니다(`Config/Engine/PackConfig.json`). */
        virtual string_view getRawFolderName() const = 0;
        /** @brief 스탬프의 헤더 줄입니다. 형식이나 판정이 바뀌면 올립니다. 이전 스탬프는 모두 어긋남이 되어 한 번 다시 임포트합니다. */
        virtual string_view getStampHeader() const = 0;
        /** @brief 문제 메시지에 쓰는 결과의 이름입니다("DDS", "메시"). */
        virtual string_view getImportedLabel() const = 0;
        /** @brief 임포트하는 원본 확장자인지 확인합니다. `import.stamp` 와 `.meta` 같은 파일은 원본이 아닙니다. */
        virtual bool isSourceFile( string_view path ) const = 0;
        /** @brief 원본 경로에 해당하는 결과 경로를 반환합니다. 원본 폴더 구간이 없으면 빈 문자열입니다. */
        virtual string makeImportedPath( string_view sourcePath ) const = 0;
        /** @brief 원본 바이트, 적용한 규칙, 임포터 버전을 섞은 해시입니다. 읽지 못하면 0 입니다. */
        virtual uint64 computeSourceHash( string_view sourcePath, string_view assetPath ) const = 0;
        /**
         * @brief 임포트 결과의 해시입니다. 결과가 없으면 0 입니다. 기본은 결과 파일 하나의 바이트입니다.
         * @details 결과가 여러 파일인 종류는 그것을 모두 섞어 반환한다. 스킨드 모델은 메시와 옆 폴더의 스켈레톤, 클립이 해당한다. 그래야 옆 파일을 손대거나
         *          지워도 어긋남으로 잡힌다.
         */
        virtual uint64 computeImportedHash( string_view importedPath ) const;
        /** @brief 임포트하지 않는 원본이면 그 이유를 반환하고, 아니면 nullptr 입니다. */
        virtual const utf8* findUnsupportedReason( string_view sourcePath ) const = 0;
        /**
         * @brief 이 원본의 임포트가 다른 원본의 임포트 결과를 읽으면 true 입니다(기본은 false). 클래스 설명의 임포터 규칙을 보십시오.
         * @details 읽는 대상이 같은 실행에서 임포트될 수 있으면 그 결과가 먼저 나와야 하므로 데이터로 드러낸다. 조용히 두면 병렬 구간에서 읽는 쪽이 옛 결과를 본다.
         */
        virtual bool hasOrderDependency( [[maybe_unused]] string_view assetPath ) const { return false; }
        /**
         * @brief 원본 하나를 결과 경로로 임포트합니다.
         * @details 스레드 계약: 워커에서 서로 다른 원본에 대해 동시에 불린다(같은 결과 경로를 쓰는 원본은 한 스레드가 순서대로). 이 임포터의 공유 상태, `AssetPaths` 의 전역 쓰기,
         *          에디터 서비스 범위(`findEditorScope`)를 건드리지 않는다.
         */
        [[nodiscard]] virtual bool importSource( string_view sourcePath, string_view importedPath, string_view assetPath ) const = 0;
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
         *          `git` 이 시간 순서를 임의로 뒤집기 때문이다. 원본이 사라진 스탬프 줄도 어긋남이다. ImportStale 은 줄만 지우고 결과는 두므로,
         *          남은 결과는 사람이 정리한다. 스탬프는 내용이 바뀔 때만 쓴다.
         *
         *          **원본 파일은 병렬로 임포트하고 합치기는 직렬로 한다.** 원본 폴더마다 파일 목록과 사슬(같은 결과 자리에 쓰는 파일의 묶음)을 만든 뒤 사슬 단위로 작업 시스템(@p pTaskManager)과
         *          나눠 돌고(해시 계산 · 대조 · 임포트가 한 파일의 일이다), `IRawAssetImporter::hasOrderDependency` 인 원본이 든 사슬은 그 뒤에 이 스레드에서 직렬로 돈다.
         *          파일마다 결과가 자기 칸에 담기고 요약(`_listProblem` 의 순서 · 카운터)과 스탬프 표는 파일 순서대로 합치므로, 결과 바이트 · 스탬프 · 요약은 직렬로 돌 때와 같다.
         *          작업 시스템이 없으면(테스트 하니스 · 서비스 연결 전) 이 스레드가 사슬을 파일 순서대로 돈다. 임포터 로그의 줄 순서만 병렬로 돌 때 파일 사이에서 섞인다.
         * @param resourceRoot `Resource/` 의 절대 경로
         * @param pTaskManager 사슬을 나눠 돌 작업 시스템(빌림, nullptr 이면 이 스레드가 직렬로). 부르는 쪽이 찾아 넘긴다 — 워커에서 불릴 수 있어 여기서는 에디터 서비스 범위(`findEditorScope`)를 읽지 않는다.
         */
        [[nodiscard]] static AssetImportSummary importAll( string_view resourceRoot, const IRawAssetImporter& importer, AssetImportMode mode, TaskManager* pTaskManager );

        /**
         * @brief @p listAssetPath(리소스 루트 기준, 구분자 · 대소문자 무시)의 원본만 스탬프와 대조하고 어긋났으면 임포트합니다(ImportStale). 에디터 핫 리로드가 바뀐 원본만 임포트하는 길입니다.
         * @details 루트 전체를 훑는 `importAll` 은 원본마다 내용 해시를 읽어 비용이 바뀐 파일 수가 아니라 원본 수에 비례한다. 이쪽은 범위 안 원본이 든 폴더만 훑고 범위 밖 원본은 읽지 않는다.
         *          스탬프를 쓰는 길은 `importAll` 과 같은 한 곳이다(에디터가 임포트한 것과 `App --import-*` 가 임포트한 것이 같은 판정을 받는다). 범위 밖 원본의 스탬프 줄은 그대로 둔다.
         *          지우면 다음 전체 대조가 그 원본들을 모두 다시 임포트한다. 사슬과 순서 의존은 범위 안 원본끼리만 따진다.
         *          **범위 밖에서 어긋난 것(규칙 파일만 고친 다른 원본 등)은 보정하지 않는다.** 그 보정은 `App --import-*` 의 전체 대조가 한다.
         *          작업 시스템(@p pTaskManager, `importAll` 과 같음)을 부르는 쪽이 넘기므로 에디터 스레드에서도 워커에서도 부를 수 있다.
         */
        [[nodiscard]] static AssetImportSummary importSources( string_view resourceRoot, const IRawAssetImporter& importer, const vector<string>& listAssetPath,
                                                               TaskManager* pTaskManager );

        /**
         * @brief 원본 경로를 결과 경로로 바꿉니다(`<x>/<rawFolder>/<y>.<ext>` → `<x>/<importedFolder>/<y><importedExtension>`).
         * @details 원본 폴더 이름은 경로 구간 경계에서만 찾는다. `my_textures_raw_backup` 은 해당하지 않는다. 없으면 빈 문자열을 반환한다.
         */
        static string makeImportedPath( string_view rawPath, string_view rawFolderName, string_view importedFolderName, string_view importedExtension );

        /** @brief 파일 바이트 그대로 계산한 FNV-1a 64 해시입니다. 파일이 없거나 비어 있으면 0 입니다. */
        static uint64 computeFileHash( string_view path );
    };
} // namespace sw::editor
