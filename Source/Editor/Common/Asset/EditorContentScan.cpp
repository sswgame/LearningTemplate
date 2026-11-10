#include "pch.h"

#include "Editor/Common/Asset/EditorContentScan.h"

#include "Core/File/FileUtil.h"

#include "Engine/Automation/Scenario/AutomationScratch.h"

namespace sw::editor
{
    bool EditorContentScan::collectFiles( string_view contentRoot, string_view folder, string_view extension, vector<string>& outListFilePath, bool bRecursive,
                                          const StopToken& stopToken )
    {
        const bool bListed = FileUtil::collectFiles( folder, extension, outListFilePath, bRecursive, stopToken );
        AutomationScratch::removeOtherProcessInstances( contentRoot, outListFilePath );
        return bListed;
    }

    bool EditorContentScan::collectFolders( string_view contentRoot, string_view folder, vector<string>& outListFolder, bool bRecursive )
    {
        const bool bListed = FileUtil::collectFolders( folder, outListFolder, bRecursive );
        AutomationScratch::removeOtherProcessInstances( contentRoot, outListFolder );
        return bListed;
    }
} // namespace sw::editor
