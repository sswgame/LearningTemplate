/**
 * @file EditorBackgroundIO.h
 * @brief 에디터의 파일 스캔과 로컬라이즈 로드를 TaskManager 워커에서 실행하는 잡들입니다.
 *
 * @details 공통 규약(락, 세대, 완료 플래그)은 EditorBackgroundTask 에 있다. 여기 있는 잡들은 입력 타입과 실제
 *          작업 본문만 가진다.
 *          이 헤더는 결과 값 헤더(`EditorAssetIndexEntry.h` · `EditorFolderListingEntry.h` · `EditorLocalizationSheet.h`)와 잡 뼈대만 include 한다 — 패널 Model 이 잡을 값으로 들어도 커맨드 동작이 View 로 새지 않는다
 *          (MVVM View include 계약, `Config/Rules/Lint/EditorViewIncludes.json` 의 허용 헤더). 작업 본문이 부르는 커맨드는 .cpp 가 include 한다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/Asset/EditorAssetIndexEntry.h"
#include "Editor/Common/Commands/Asset/EditorFolderListingEntry.h"
#include "Editor/Common/Commands/Asset/EditorLocalizationSheet.h"
#include "Editor/Common/Task/EditorBackgroundTask.h"

namespace sw
{
    class TaskArgs;
    class TaskManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 폴더, 확장자, 재귀 여부로 파일을 모으는 잡의 입력입니다. */
    struct EditorFileCollectInput
    {
        string _folder;
        string _extension;
        bool   _bRecursive{ SW_FALSE }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 폴더 하나의 직속 항목을 모으는 잡의 입력입니다. */
    struct EditorFolderListingInput
    {
        string _folder;
        bool   _bRecursive{ SW_FALSE }; ///< 하위 폴더의 파일까지 모은다(검색 중) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorFileCollectJob
     * @brief 폴더의 파일 목록을 워커에서 모으고 게임 스레드에서 꺼냅니다.
     */
    class EditorFileCollectJob final : public EditorBackgroundTask<EditorFileCollectInput, vector<string>>
    {
    public:
        /** @brief 워커에 폴더 스캔을 요청합니다. 이미 대기 중이면 세대를 올립니다. */
        void request( string_view folder, string_view extension, bool recursive );

    private:
        static void runJob( const TaskArgs& args );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorLocalizationLoadJob
     * @brief 로컬라이제이션 프로젝트(원문 테이블, 번역 테이블)를 워커에서 읽고 게임 스레드에서 적용합니다.
     */
    class EditorLocalizationLoadJob final : public EditorBackgroundTask<string, LocalizationSheet>
    {
    public:
        /** @brief 워커에 프로젝트 로드를 요청합니다. */
        void request( string_view projectPath );

    private:
        static void runJob( const TaskArgs& args );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorGameDataScanJob
     * @brief 게임 데이터 XML 파일 목록을 워커에서 모읍니다.
     */
    class EditorGameDataScanJob final : public EditorBackgroundTask<string, vector<GameDataFileEntry>>
    {
    public:
        /** @brief 워커에 XML 목록 스캔을 요청합니다. data 폴더는 호출한(에디터) 스레드에서 찾아 넘긴다 — 에디터 상태는 워커가 읽지 않는다. */
        void request();

    private:
        static void runJob( const TaskArgs& args );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorAssetIndexJob
     * @brief Resource 트리 분류 인덱스를 워커에서 만듭니다.
     */
    class EditorAssetIndexJob final : public EditorBackgroundTask<EditorBackgroundNoInput, vector<EditorAssetIndexEntry>>
    {
    public:
        /** @brief 워커에 Resource 스캔을 요청합니다. */
        void request();

    private:
        static void runJob( const TaskArgs& args );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorFolderListingJob
     * @brief Content Browser 폴더 직속 항목을 워커에서 모읍니다.
     */
    class EditorFolderListingJob final : public EditorBackgroundTask<EditorFolderListingInput, vector<EditorFolderListingEntry>>
    {
    public:
        /** @brief 워커에 폴더 목록 스캔을 요청합니다. @p bRecursive 이면 하위 폴더의 파일까지 모읍니다. */
        void request( string_view folderAbs, bool bRecursive = false );

    private:
        static void runJob( const TaskArgs& args );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorResourceCatalogJob
     * @brief 프로파일러 리소스 카탈로그 개수를 워커에서 셉니다.
     */
    class EditorResourceCatalogJob final : public EditorBackgroundTask<EditorBackgroundNoInput, EditorResourceCatalogCounts>
    {
    public:
        /** @brief 워커에 카탈로그 스캔을 요청합니다. */
        void request();

    private:
        static void runJob( const TaskArgs& args );
    };
} // namespace sw::editor
