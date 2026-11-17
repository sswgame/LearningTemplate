#include "pch.h"

#include "Editor/Common/Asset/EditorContentScan.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Automation/Scenario/AutomationScratch.h"

namespace sw::editor
{
    bool EditorContentScan::isListedFile( string_view assetID )
    {
        // 원본 곁의 정체성 서술(`<파일>.meta`)은 에셋이 아니다 — 브라우저 · 빠른 열기 · 에셋 색인 · 임포트 훑기가 모두 이 규칙을 지난다(유니티도 .meta 를 보이지 않는다).
        return AutomationScratch::isOtherProcessInstance( assetID ) == false && StringUtil::endsWith( assetID, AssetMeta::kExtension, true ) == false;
    }

    bool EditorContentScan::collectFiles( string_view contentRoot, string_view folder, string_view extension, vector<string>& outListFilePath, bool bRecursive,
                                          const StopToken& stopToken )
    {
        const bool bListed = FileUtil::collectFiles( folder, extension, outListFilePath, bRecursive, stopToken );
        outListFilePath.erase( std::remove_if( outListFilePath.begin(), outListFilePath.end(), [contentRoot]( const string& path )
        {
            string relativePath;
            return FileUtil::makeRelativePath( contentRoot, path, relativePath ) && isListedFile( FileUtil::normalizeSeparators( relativePath ) ) == false;
        } ),
                               outListFilePath.end() );
        return bListed;
    }

    bool EditorContentScan::collectFolders( string_view contentRoot, string_view folder, vector<string>& outListFolder, bool bRecursive )
    {
        const bool bListed = FileUtil::collectFolders( folder, outListFolder, bRecursive );
        AutomationScratch::removeOtherProcessInstances( contentRoot, outListFolder );
        return bListed;
    }
} // namespace sw::editor
