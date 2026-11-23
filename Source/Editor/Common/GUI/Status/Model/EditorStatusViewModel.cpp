#include "pch.h"

#include "Editor/Common/GUI/Status/Model/EditorStatusViewModel.h"

#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Commands/Scene/EditorLightProbeBakeSession.h"
#include "Editor/Common/Commands/Scene/EditorReflectionProbeBakeSession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"

namespace sw::editor
{
    bool EditorStatusViewModel::gatherAssetImport( EditorAssetImportStatus& outStatus )
    {
        // 상태줄은 에디터 컨텍스트가 활성일 때만 그려지고 AssetHotReload 는 그 범위의 서브시스템이다 — 없으면 버그.
        const AssetHotReload& hotReload = getSubsystem<AssetHotReload>();
        if ( hotReload.isImportBusy() == false )
            return false;
        outStatus._runningCount = static_cast<uint32>( hotReload.getRunningImportCount() );
        outStatus._queuedCount  = static_cast<uint32>( hotReload.getQueuedImportCount() );
        return true;
    }

    bool EditorStatusViewModel::gatherLightProbeBake( EditorProbeBakeStatus& outStatus )
    {
        const EditorLightProbeBakeSession* pSession = findSubsystem<EditorLightProbeBakeSession>();
        if ( pSession == nullptr || pSession->isRunning() == false )
            return false;
        outStatus._done         = pSession->getVolumeIndex() + 1;
        outStatus._total        = pSession->getVolumeCount();
        outStatus._itemProgress = pSession->getVolumeProgress();
        return true;
    }

    bool EditorStatusViewModel::gatherReflectionProbeBake( EditorProbeBakeStatus& outStatus )
    {
        const EditorReflectionProbeBakeSession* pSession = findSubsystem<EditorReflectionProbeBakeSession>();
        if ( pSession == nullptr || pSession->isRunning() == false )
            return false;
        outStatus._done  = pSession->getDoneProbeCount();
        outStatus._total = pSession->getProbeCount();
        return true;
    }

    void EditorStatusViewModel::cancelLightProbeBake()
    {
        EditorLightProbeBakeSession* pSession = findSubsystem<EditorLightProbeBakeSession>();
        if ( pSession != nullptr && pSession->isRunning() )
            pSession->cancel( "canceled" );
    }

    void EditorStatusViewModel::cancelReflectionProbeBake()
    {
        EditorReflectionProbeBakeSession* pSession = findSubsystem<EditorReflectionProbeBakeSession>();
        if ( pSession != nullptr && pSession->isRunning() )
            pSession->cancel( "canceled" );
    }

    void EditorStatusViewModel::showInFileExplorer( string_view absolutePath )
    {
        (void)EditorAssetFileCommands::showInFileExplorer( absolutePath ); // 실패는 명령이 알린다
    }
} // namespace sw::editor
