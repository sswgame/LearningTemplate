/**
 * @file EditorContentScan.h
 * @brief 에디터가 리소스 트리를 디스크에서 훑는 한 입구입니다 — 원본 곁 서술(`.meta`)과 작업 도메인의 옛 자리(`Content/scratch/`)에 남은 찌꺼기를 뺀 목록을 돌려줍니다.
 * @details 작업 도메인(`scratch/`, 시나리오 사본)은 이 프로세스의 실행 뿌리 아래다(`AssetPaths::getScratchFolderPath`). 그래서 리소스 루트를 훑어도 남의 실행 사본은 나오지 않고,
 *          이 실행의 사본은 그 폴더를 따로 훑는다(`collectTreeFiles`). 예전 자리(`Content/scratch/`)에 남은 폴더가 원본과 같은 파일 이름으로 보이면 Quick Open 이 그것을 집으므로 뺀다.
 *          에디터 코드는 리소스 트리를 `FileUtil::collectFiles` · `collectFolders` 로 직접 훑지 않고 이것을 부른다(린트 common-shapes 의 editorContentScan).
 *          쿠킹 · 팩은 작업 도메인을 빼는 `AssetPaths::collectContentFiles` 를 쓴다(규칙이 다르다).
 */
#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 트리 훑기가 찾은 파일 하나입니다. 도장은 나열이 함께 준 값이라 파일마다 다시 묻지 않는다. */
    struct EditorContentFile
    {
        string    _path;           ///< 디스크 경로(대소문자 그대로)
        FileStamp _fileStamp{};    ///< 파일의 크기 · 쓰기 시각
        FileStamp _sidecarStamp{}; ///< 곁 서술(`<파일>.meta`)의 크기 · 쓰기 시각. 없으면 0 이다
    };

    /**
     * @struct EditorContentScan
     * @brief 리소스 트리의 폴더를 훑습니다. 반환과 경로 규칙은 `FileUtil::collectFiles` · `collectFolders` 와 같고, 곁 서술과 옛 작업 도메인 자리(`ProcessRoots::_content` 아래 `scratch/`)만 빠집니다.
     */
    struct EditorContentScan
    {
        /**
         * @brief 리소스 id @p assetID(디스크 표기)가 에디터 목록에 보이는 파일이면 true 입니다 — 원본 곁 서술(`.meta`)은 아니다.
         * @details 트리 훑기(`collectFiles`)와 에셋 색인의 한 파일 갱신(`EditorAssetIndex::applyChanges`)이 같은 규칙을 이것 하나로 쓴다.
         */
        [[nodiscard]] static bool isListedFile( string_view assetID );
        /** @brief @p folder(디스크 경로) 아래 파일입니다. 확장자 @p extension 이 비면 모두입니다. */
        [[nodiscard]] static bool collectFiles( const AbsolutePath& folder, string_view extension, vector<AbsolutePath>& outListFilePath, bool bRecursive, const StopToken& stopToken = {} );
        /** @brief @p folder(디스크 경로, 하위까지) 아래 원본 곁 서술(`.meta`)입니다 — 파일 없이 서술만 있는 원본을 찾는 쪽(`AssetSourceLocator`)이 쓴다. */
        [[nodiscard]] static bool collectSidecarFiles( const AbsolutePath& folder, vector<AbsolutePath>& outListFilePath );
        /** @brief @p folder(디스크 경로) 아래 폴더입니다. */
        [[nodiscard]] static bool collectFolders( const AbsolutePath& folder, vector<AbsolutePath>& outListFolder, bool bRecursive );
        /**
         * @brief 에디터의 리소스 트리 전체, 곧 리소스 루트 @p contentRoot 와 작업 도메인 폴더(@p scratchFolder, 아직 없으면 뺀다) 아래 파일입니다.
         * @details 트리 전체를 보는 쪽(에셋 색인)이 부른다. 작업 도메인 파일의 id 는 `AssetPaths::toAssetID` 로 푼다(루트 기준 상대 경로가 아니다).
         */
        [[nodiscard]] static bool collectTreeFiles( const AbsolutePath& contentRoot, const AbsolutePath& scratchFolder, vector<EditorContentFile>& outListFile, const StopToken& stopToken = {} );
    };
} // namespace sw::editor
