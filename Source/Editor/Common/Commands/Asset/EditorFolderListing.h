/**
 * @file EditorFolderListing.h
 * @brief 콘텐츠 브라우저가 그리는 폴더 목록을 디스크에서 읽습니다(ImGui · 에디터 컨텍스트 없음).
 * @details 하위 폴더를 읽은 횟수(`getChildFolderScanCount`)는 에디터 범위의 상태 저장소(`EditorStateStore`)가 센다 — 에디터 범위(또는 테스트가 건 저장소)가 있어야 `collectChildFolders` 를 부를 수 있다.
 */
#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief Content Browser 폴더의 항목 하나입니다. */
    struct EditorFolderListingEntry
    {
        string _name;
        string _relativePath;
        string _absolutePath;
        string _extension;
        bool   _bIsDirectory{ false };
        bool   _bReadOnly{ false }; ///< 파일이 읽기 전용이다. 버전 관리가 잠그기 전이거나 사람이 막은 경우다. 목록을 만들 때 워커가 확인한다.
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorFolderListing
     * @brief 폴더 하나의 항목과 직속 하위 폴더를 읽습니다. 백그라운드 워커와 폴더 트리 캐시가 부릅니다.
     */
    struct EditorFolderListing
    {
        /** @brief 폴더의 직속 하위 폴더와 파일을 채웁니다. .meta 는 제외합니다. @p bRecursive 이면 파일은 하위 폴더 전부에서 모으고 폴더는 직속만 모읍니다. */
        static void collectFolderListing( string_view folderAbs, vector<EditorFolderListingEntry>& outList, bool bRecursive = false, const StopToken& stopToken = {} );
        /** @brief 폴더의 직속 하위 폴더 절대 경로를 채웁니다. */
        static void collectChildFolders( string_view folderAbs, vector<string>& outList );
        /** @brief `collectChildFolders` 가 디스크를 읽은 횟수입니다. 에디터 범위의 누계이며, 자체 테스트가 "그릴 때마다 디스크를 읽지 않는다" 를 확인할 때 씁니다. */
        static uint64 getChildFolderScanCount();
    };
} // namespace sw::editor
