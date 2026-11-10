#include "pch.h"

#include "Editor/Common/Asset/EditorContentScan.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Automation/Scenario/AutomationScratch.h"

namespace sw::editor
{
    bool EditorContentScan::collectFiles( string_view contentRoot, string_view folder, string_view extension, vector<string>& outListFilePath, bool bRecursive,
                                          const StopToken& stopToken )
    {
        const bool bListed = FileUtil::collectFiles( folder, extension, outListFilePath, bRecursive, stopToken );
        AutomationScratch::removeOtherProcessInstances( contentRoot, outListFilePath );
        // 원본 곁의 정체성 서술(`<파일>.meta`)은 에셋이 아니다 — 브라우저 · 빠른 열기 · 참조 색인 · 임포트 훑기가 모두 이 입구를 지나므로 여기서 한 번 뺀다(유니티도 .meta 를 보이지 않는다).
        outListFilePath.erase( std::remove_if( outListFilePath.begin(), outListFilePath.end(), []( const string& path )
        { return StringUtil::endsWith( path, AssetMeta::kExtension, true ); } ),
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
