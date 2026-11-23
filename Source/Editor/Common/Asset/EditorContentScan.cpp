#include "pch.h"

#include "Editor/Common/Asset/EditorContentScan.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Content/AssetPaths.h"

namespace sw::editor
{
    namespace
    {
        struct EditorContentScanInternal
        {
            /**
             * @brief @p path 가 리소스 루트 안의 작업 도메인 옛 자리(`Content/scratch/`)면 true 입니다 — 지금의 작업 도메인은 실행 뿌리 아래다.
             * @details 훑는 뿌리가 리소스 루트가 아닐 때(실행 뿌리를 뿌리로 쓰는 작업 도메인 임포트)도 같은 자리를 뺀다 — 그래서 훑는 뿌리가 아니라 프로세스의 리소스 루트로 견준다.
             */
            static bool isLeftoverScratch( string_view path )
            {
                static const AbsolutePath s_leftoverFolder = ProcessRoots::get()._content.join( AssetPaths::kScratchDomain );
                return FileUtil::isPathAtOrUnderRoot( s_leftoverFolder.str(), path );
            }
        };
    } // namespace

    bool EditorContentScan::isListedFile( string_view assetID )
    {
        // 원본 곁의 정체성 서술(`<파일>.meta`)은 에셋이 아니다 — 브라우저 · 빠른 열기 · 에셋 색인 · 임포트 훑기가 모두 이 규칙을 지난다(유니티도 .meta 를 보이지 않는다).
        return StringUtil::endsWith( assetID, AssetMeta::kExtension, true ) == false;
    }

    bool EditorContentScan::collectFiles( string_view folder, string_view extension, vector<string>& outListFilePath, bool bRecursive, const StopToken& stopToken )
    {
        const bool bListed = FileUtil::collectFiles( FileUtil::toDiskPath( folder ), extension, outListFilePath, bRecursive, stopToken );
        (void)VectorUtil::removeIf( outListFilePath, []( const string& path )
        { return EditorContentScanInternal::isLeftoverScratch( path ) || isListedFile( FileUtil::normalizeSeparators( path ) ) == false; } );
        return bListed;
    }

    bool EditorContentScan::collectSidecarFiles( string_view folder, vector<string>& outListFilePath )
    {
        const bool bListed = FileUtil::collectFiles( FileUtil::toDiskPath( folder ), AssetMeta::kExtension, outListFilePath, true );
        (void)VectorUtil::removeIf( outListFilePath, []( const string& path )
        { return EditorContentScanInternal::isLeftoverScratch( path ); } );
        return bListed;
    }

    bool EditorContentScan::collectFolders( string_view folder, vector<string>& outListFolder, bool bRecursive )
    {
        const bool bListed = FileUtil::collectFolders( FileUtil::toDiskPath( folder ), outListFolder, bRecursive );
        (void)VectorUtil::removeIf( outListFolder, []( const string& path )
        { return EditorContentScanInternal::isLeftoverScratch( path ); } );
        return bListed;
    }

    bool EditorContentScan::collectTreeFiles( string_view contentRoot, string_view scratchFolder, string_view extension, vector<string>& outListFilePath, const StopToken& stopToken )
    {
        bool bListed = contentRoot.empty() || collectFiles( contentRoot, extension, outListFilePath, true, stopToken );
        if ( scratchFolder.empty() == false && FileUtil::isDirectory( FileUtil::toDiskPath( scratchFolder ) ) )
        {
            vector<string> listScratchFile;
            bListed = collectFiles( scratchFolder, extension, listScratchFile, true, stopToken ) && bListed;
            outListFilePath.insert( outListFilePath.end(), listScratchFile.begin(), listScratchFile.end() );
        }
        return bListed;
    }
} // namespace sw::editor
