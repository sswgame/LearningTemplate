#include "pch.h"

#include "Editor/Common/Commands/EditorTracyLauncher.h"

#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Process/Process.h"

#include "Editor/Common/EditorUtil.h"

#include "Engine/Profiling/ProfilerBackend.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorTracy" );

    /**
     * @brief `-gv_tracyViewerPath=<경로>`: "Tracy 열기" 가 띄울 Tracy 뷰어(tracy-profiler, 같은 판 0.14.1). 파일이나 그 파일이 든 폴더.
     * @details 비우면 `<프로젝트>/Tools/Tracy/` 를 본다. 뷰어는 저장소에 넣지 않는다(`Source/Engine/Profiling/README.md`).
     */
    SW_GLOBAL_VARIABLE( sw::string, gv_tracyViewerPath, "", "Tracy 뷰어(tracy-profiler 0.14.1) 경로 — 파일이나 폴더 (비우면 Tools/Tracy)" );

    const utf8* EditorTracyLauncher::getViewerFileName()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return "tracy-profiler.exe";
#else
        return "tracy-profiler";
#endif
    }

    bool EditorTracyLauncher::findViewerPath( string_view configuredPath, string_view projectRoot, string& outPath )
    {
        outPath.clear();
        if ( configuredPath.empty() == false )
        {
            // 폴더를 가리키면 그 안의 실행 파일, 파일을 가리키면 그대로. 폴더를 먼저 본다 — `exists` 는 폴더에도 true 다.
            if ( FileUtil::isDirectory( configuredPath ) )
            {
                const string inFolder = FileUtil::joinPath( configuredPath, getViewerFileName() );
                if ( FileUtil::exists( inFolder ) )
                {
                    outPath = inFolder;
                    return true;
                }
            }
            else if ( FileUtil::exists( configuredPath ) )
            {
                outPath = string( configuredPath );
                return true;
            }
        }
        if ( projectRoot.empty() == false )
        {
            const string inTools = FileUtil::joinPath( FileUtil::joinPath( projectRoot, kDefaultViewerFolder ), getViewerFileName() );
            if ( FileUtil::exists( inTools ) )
            {
                outPath = inTools;
                return true;
            }
        }
        return false;
    }

    string EditorTracyLauncher::makeViewerCommand( string_view viewerPath, uint16 port )
    {
        // `-a <주소>` 는 뜨자마자 그 주소에 붙는다(Tracy 뷰어 명령줄). 같은 PC 라 localhost 다.
        string command = "\"";
        command += FileUtil::toNativeSeparators( viewerPath );
        command += "\" -a 127.0.0.1 -p ";
        command += to_string( static_cast<uint32>( port ) );
        return command;
    }

    EditorTracyLaunchResult EditorTracyLauncher::openViewer()
    {
        if ( ProfilerBackend::isTracyCompiled() == false )
            return EditorTracyLaunchResult::TracyNotCompiled;

        string viewerPath;
        if ( findViewerPath( gv_tracyViewerPath, EditorUtil::getProjectRootPath(), viewerPath ) == false )
        {
            SW_LOG_WARNING( "Tracy viewer not found — download the 0.14.1 Windows release and put %# in Tools/Tracy (or set gv_tracyViewerPath)",
                            getViewerFileName() );
            return EditorTracyLaunchResult::ViewerNotFound;
        }

        // 뷰어가 붙기 전에 출력을 켠다 — 켜는 순간부터 기록하고, 뷰어는 붙을 때 그때까지 쌓인 것을 받는다.
        if ( ProfilerBackend::startTracy() == false )
            return EditorTracyLaunchResult::TracyNotCompiled;

        const string command = makeViewerCommand( viewerPath, ProfilerBackend::getTracyPort() );
        if ( Process::launchDetached( command ) == false )
        {
            SW_LOG_WARNING( "Failed to launch the Tracy viewer: %#", command );
            return EditorTracyLaunchResult::ViewerLaunchFailed;
        }
        SW_LOG_INFO( "Tracy viewer launched: %#", command );
        return EditorTracyLaunchResult::Launched;
    }

    const utf8* EditorTracyLauncher::describeResult( EditorTracyLaunchResult result )
    {
        switch ( result )
        {
            case EditorTracyLaunchResult::Launched:
                return "Tracy viewer launched and connecting to 127.0.0.1";
            case EditorTracyLaunchResult::TracyNotCompiled:
                return "Tracy is not linked into this build (SW_ENABLE_TRACY=OFF or Shipping)";
            case EditorTracyLaunchResult::ViewerNotFound:
                return "Tracy viewer not found: put tracy-profiler (0.14.1) in Tools/Tracy or set gv_tracyViewerPath";
            case EditorTracyLaunchResult::ViewerLaunchFailed:
                return "Failed to launch the Tracy viewer (see the log)";
        }
        return "";
    }
} // namespace sw::editor
