#include "pch.h"

#include "Editor/Panels/Profiler/Model/ProfilerViewModel.h"

#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Commands/Diagnostics/EditorTracyLauncher.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    ProfilerViewModel::ProfilerViewModel()
        : _tracyError{}
    {
    }

    FrameProfiler& ProfilerViewModel::getFrameProfiler()
    {
        return editor::getSubsystem<FrameProfiler>(); // 필수 서비스: 없으면 버그
    }

    MemoryProfiler* ProfilerViewModel::findMemoryProfiler()
    {
        return editor::findSubsystem<MemoryProfiler>();
    }

    IRHIDevice* ProfilerViewModel::findRHIDevice()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>().getRHIDevice();
    }

    uint32 ProfilerViewModel::getWorkerCount()
    {
        return static_cast<uint32>( editor::getSubsystem<TaskManager>().getWorkerCount() );
    }

#if !defined( SW_SHIPPING )
    string ProfilerViewModel::takeMergedNetPlayTrace()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorNetPlaySession>().takeUnopenedMergedTrace();
    }
#endif

    void ProfilerViewModel::gatherScene( ProfilerSceneViewState& outState ) const
    {
        outState._listRow.clear(); // 행 저장소는 다시 쓴다
        outState._objectCount    = 0;
        outState._rootCount      = 0;
        outState._componentCount = 0;
        outState._bHasScene      = SW_FALSE;
        Scene* pScene            = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        // 집계는 ImGui 에 의존하지 않는 EditorSceneCommands 가 한다. 테스트가 있다.
        const EditorSceneCommands::SceneStatistics stats = EditorSceneCommands::collectSceneStatistics( pScene->getObjectManager() );
        outState._objectCount                            = stats._objectCount;
        outState._rootCount                              = stats._rootCount;
        outState._componentCount                         = stats._componentCount;
        outState._bHasScene                              = SW_TRUE;
        outState._listRow.reserve( stats._listDistribution.size() );
        for ( const EditorSceneCommands::ComponentDistributionRow& row : stats._listDistribution )
            outState._listRow.push_back( ProfilerComponentRow{ row._typeName, row._instanceCount } );
    }

    void ProfilerViewModel::openTracyViewer()
    {
        const EditorTracyLaunchResult result = EditorTracyLauncher::openViewer();
        _tracyError                          = result == EditorTracyLaunchResult::Launched ? string{} : string{ EditorTracyLauncher::describeResult( result ) };
    }
} // namespace sw::editor
