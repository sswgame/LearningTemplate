/**
 * @file EditorBackgroundIo.h
 * @brief 에디터 파일 스캔/로컬라이즈 로드를 TaskManager 워커에서 수행하는 잡
 *
 * @details 공통 규약(잠금·세대·완료 플래그)은 EditorBackgroundJob 에 있다. 여기 있는 잡들은
 *          입력 타입과 실제 작업 본문만 갖는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorBackgroundJob.h"
#include "Editor/Common/Commands/EditorDataTableCommands.h"

namespace sw
{
    class TaskArgs;
} // namespace sw

namespace sw::editor
{
    /** @brief 폴더/확장자/재귀 여부로 파일을 모으는 잡의 입력. */
    struct EditorFileCollectInput
    {
        string _folder;
        string _extension;
        bool   _bRecursive{ false };
    };

    /** @brief 폴더 하나의 직속 항목을 모으는 잡의 입력. */
    struct EditorFolderListingInput
    {
        string _folder;
    };

    /**
     * @class EditorFileCollectJob
     * @brief 폴더 파일 목록을 워커에서 모으고 게임 스레드에서 꺼냅니다.
     */
    class EditorFileCollectJob final : public EditorBackgroundJob<EditorFileCollectInput, vector<string>>
    {
    public:
        /** @brief 워커에 폴더 스캔을 요청합니다. 이미 대기 중이면 세대를 올립니다. */
        void request( string_view folder, string_view extension, bool recursive );

    private:
        static void runJob( const TaskArgs& args );
    };

    /**
     * @class EditorLocalizationLoadJob
     * @brief 로컬라이즈 JSON을 워커에서 읽고 게임 스레드에서 적용합니다.
     */
    class EditorLocalizationLoadJob final : public EditorBackgroundJob<EditorBackgroundNoInput, vector<LocRecord>>
    {
    public:
        /** @brief 워커에 JSON 로드를 요청합니다. */
        void request();

    private:
        static void runJob( const TaskArgs& args );
    };

    /**
     * @class EditorGameDataScanJob
     * @brief 게임 데이터 XML 파일 목록을 워커에서 모읍니다.
     */
    class EditorGameDataScanJob final : public EditorBackgroundJob<EditorBackgroundNoInput, vector<GameDataFileEntry>>
    {
    public:
        /** @brief 워커에 XML 목록 스캔을 요청합니다. */
        void request();

    private:
        static void runJob( const TaskArgs& args );
    };

    /**
     * @class EditorResourceIndexJob
     * @brief Resource 트리 분류 인덱스를 워커에서 만듭니다.
     */
    class EditorResourceIndexJob final : public EditorBackgroundJob<EditorBackgroundNoInput, vector<EditorResourceIndexEntry>>
    {
    public:
        /** @brief 워커에 Resource 스캔을 요청합니다. */
        void request();

    private:
        static void runJob( const TaskArgs& args );
    };

    /**
     * @class EditorFolderListingJob
     * @brief Content Browser 폴더 직속 항목을 워커에서 모읍니다.
     */
    class EditorFolderListingJob final : public EditorBackgroundJob<EditorFolderListingInput, vector<EditorFolderListingEntry>>
    {
    public:
        /** @brief 워커에 폴더 목록 스캔을 요청합니다. */
        void request( string_view folderAbs );

    private:
        static void runJob( const TaskArgs& args );
    };

    /**
     * @class EditorResourceCatalogJob
     * @brief 프로파일러 리소스 카탈로그 개수를 워커에서 셉니다.
     */
    class EditorResourceCatalogJob final : public EditorBackgroundJob<EditorBackgroundNoInput, EditorResourceCatalogCounts>
    {
    public:
        /** @brief 워커에 카탈로그 스캔을 요청합니다. */
        void request();

    private:
        static void runJob( const TaskArgs& args );
    };
} // namespace sw::editor
