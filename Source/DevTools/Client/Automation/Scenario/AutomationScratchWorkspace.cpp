#include "pch.h"

#include "DevTools/Client/Automation/Scenario/AutomationScratchWorkspace.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Process/Process.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationScratch.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{

    namespace
    {
        struct AutomationScratchWorkspaceInternal
        {
            /**
             * @brief 자동화 실행이 쓰면 안 되는 추적 폴더(프로젝트 루트 기준)입니다 — `docs/` 와 `Config/`.
             * @details 언리얼 · 유니티의 자동화 실행도 소스 관리 아래 폴더(`Config/` · `ProjectSettings/`)는 쓰기를 막고 `Saved/` · 임시 폴더로 보낸다. 같은 이유로 둔다.
             */
            static constexpr string_view kTrackedFolders[] = { "docs", "Config" };
            /**
             * @brief `kTrackedFolders` 안에서 쓰기를 허락하는 하위 폴더입니다(프로젝트 루트 기준). 에디터 설정을 저장하는 길이 쓰는 `Config/Editor/` 하나다.
             * @details 프로젝트 설정 창(`Config/Game` · `Config/Engine`)의 저장은 허락하지 않는다 — 시나리오가 그 파일을 고치려면 사본으로 돌려야 한다.
             */
            static constexpr string_view kAllowedConfigWrites[] = { "Config/Editor" };

            /** @brief `FileUtil::WriteGate` — 리소스 루트 아래에서 `scratch/` 밖의 쓰기와 프로젝트의 `docs/` · `Config/`(허용 목록 밖) 쓰기를 거절합니다. 그 밖(Saved · 임시 폴더)은 시나리오의 몫이라 막지 않는다. */
            static expected<void> allowWrite( string_view path )
            {
                // 지킬 원본(`Content`)이 없는 구성(패키지 - 팩만)에는 막을 것이 없다.
                const string& rootPath = ProcessRoots::get()._content.str();
                if ( FileUtil::isDirectory( rootPath ) == false )
                    return {};
                // 상대 경로는 어디를 가리키는지 정할 수 없다 - 통과시키지 않는다(예전에는 절대로 바꾸지 못하면 열어 줬다).
                if ( AbsolutePath::isStrictAbsolute( path ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the path is not absolute - a scenario writes under {out} or a <Scratch> folder" } );
                // `..` 는 풀지 않은 글로 사본 폴더를 벗어난다(`scratch/../engine/…`) — 포함 판정은 글 연산이라 받지 않는다.
                if ( ( "/" + FileUtil::normalizeSeparators( path ) + "/" ).find( "/../" ) != string::npos )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the path has a '..' segment - a scenario writes to a resolved path", path } );
                // 포함 판정은 OS 대소문자 규칙이다(`isPathAtOrUnderRoot`) — 예전 소문자 비교는 리눅스에서 `Content/SCRATCH/…` 를 사본 폴더로 보고 열어 줬다.
                const string  scratchRoot = FileUtil::joinPath( rootPath, AutomationScratch::kScratchDomain );
                const string& projectRoot = ProcessRoots::get()._project.str();

                // 프로젝트 루트의 추적 폴더 — 허용 목록에 든 하위 폴더만 연다.
                for ( const string_view folder : kTrackedFolders )
                {
                    if ( FileUtil::isPathAtOrUnderRoot( FileUtil::joinPath( projectRoot, folder ), path ) == false )
                        continue;
                    const bool bAllowed = std::any_of( std::begin( kAllowedConfigWrites ), std::end( kAllowedConfigWrites ), [&]( string_view allowed )
                    {
                        return FileUtil::isPathAtOrUnderRoot( FileUtil::joinPath( projectRoot, allowed ), path );
                    } );
                    if ( bAllowed )
                        return {};
                    return unexpected( Error{ ErrorKind::Unsupported, "an automation run may not write this tracked folder (only Config/Editor is open); run on a copy under Content/scratch/ or write under Saved/", folder } );
                }

                const bool bScratch = FileUtil::isPathAtOrUnderRoot( scratchRoot, path ); // 사본 도메인 폴더 자체도 사본의 것이다
                if ( FileUtil::isPathUnderRoot( rootPath, path ) == false || bScratch )
                    return {};
                const string target = FileUtil::makeComparableDiskPath( path );
                // 골든 갱신 실행(`-scenario-update-golden`)만 시나리오 옆 `golden/`(자동화 폴더 아래 어느 깊이든 — `engine/automation/editor/golden/`)에 쓴다.
                // 그 실행이 하는 일이 바로 그것이고, 쓴 파일은 보고에 남아 리뷰한다. 폴더 이름은 판정이 쓰는 것과 같은 상수다.
                bool bUpdateGolden = false;
                if ( engine::areEngineServicesBound() )
                    (void)engine::getSubsystem<CommandLineManager>().getArgument( CommandLineArgument::SCENARIO_UPDATE_GOLDEN, bUpdateGolden );
                const bool bGoldenFolder = FileUtil::getFileNamePart( FileUtil::getDirectoryPart( target ) ) == kAutomationGoldenFolder;
                if ( bUpdateGolden && bGoldenFolder && target.find( "/automation/" ) != string::npos )
                    return {};
                return unexpected( Error{ ErrorKind::Unsupported, "an automation run may write under Resource only inside this folder (declare <Scratch>); this path is a tracked original", AutomationScratch::kScratchDomain } );
            }

            /**
             * @brief 사본 폴더 @p scratchDir(`…/<이름>_p<pid>`) 옆의 같은 이름 사본(`<이름>_p<다른 pid>`) 가운데 끝난 프로세스의 것을 지웁니다.
             * @details 사본은 실행이 끝나도 남는다(실패를 열어 보게, 그리고 에디터가 감시 중인 폴더를 끝에 지우면 그 사건이 로그를 낸다). 대신 다음 실행이 죽은 프로세스의
             *          사본을 거둔다. 살아 있는 프로세스의 사본은 건드리지 않는다. 못 지운 것(다른 도구가 연 파일)은 `FileUtil` 이 경고를 남기고 다음 실행이 다시 거둔다 —
             *          이 실행의 사본과 무관하므로 실패로 멈추지 않는다.
             *          프로세스 꼬리가 없는 이름(`makeInstanceID` 를 거치지 않은 사본 — 단위 테스트)은 거둘 형제가 없다.
             */
            static void removeDeadInstances( const string& scratchDir )
            {
                const string          folderName = FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( scratchDir ) );
                const optional<int32> ownID      = AutomationScratch::findInstanceProcessID( folderName );
                if ( ownID.has_value() == false )
                    return;
                const string   prefix = folderName.substr( 0, folderName.rfind( AutomationScratch::kInstanceMark ) + AutomationScratch::kInstanceMark.size() );
                vector<string> listDeadDir;
                (void)FileUtil::forEachDirectoryEntry( FileUtil::getDirectoryPart( scratchDir ), false, [&prefix, &ownID, &listDeadDir]( const DirectoryEntry& entry )
                {
                    string_view name;
                    FileUtil::getFileNamePart( entry._path, name );
                    // 같은 이름의 형제만 본다 — 앞머리가 같고 꼬리 표지가 그 바로 뒤에 있다(`<이름>_p<pid>`).
                    const bool            bSameStem = entry._bDirectory && StringUtil::startsWith( name, prefix ) && name.rfind( AutomationScratch::kInstanceMark ) + AutomationScratch::kInstanceMark.size() == prefix.size();
                    const optional<int32> processID = bSameStem ? AutomationScratch::findInstanceProcessID( name ) : optional<int32>{};
                    const bool            bDead     = processID.has_value() && *processID != *ownID && Process::isProcessAlive( *processID ) == false;
                    if ( bDead )
                        listDeadDir.emplace_back( entry._path );
                    return true;
                } ); // 상위 폴더를 못 열면 거둘 것도 없다 — 이 실행의 사본은 prepare 가 따로 만든다
                for ( const string& deadDir : listDeadDir )
                {
                    (void)FileUtil::removeDirectory( deadDir ); // 못 지우면 경고가 남고 다음 실행이 다시 거둔다(위 설명)
                }
            }
        };
    } // namespace

    void AutomationScratchWorkspace::resetInstanceFolder( const string& instanceDir )
    {
        AutomationScratchWorkspaceInternal::removeDeadInstances( instanceDir );
        (void)FileUtil::removeDirectory( instanceDir ); // 못 지우면 FileUtil 이 경고를 남긴다 — 남은 상태로 시작한다
    }

    bool AutomationScratchWorkspace::discard( const AutomationScratchFolder& folder )
    {
        const string scratchDir = FileUtil::joinPath( engine::getSubsystem<AssetPaths>().getRootFolderPath(), folder._scratchID );
        return FileUtil::removeDirectory( scratchDir ); // 못 지우면 FileUtil 이 경로와 이유를 경고한다
    }

    expected<void> AutomationScratchWorkspace::prepare( const AutomationScratchFolder& folder )
    {
        if ( expected<void> valid = AutomationScratch::isValidFolder( folder ); valid.has_value() == false )
            return valid;
        const string& resourceRoot = engine::getSubsystem<AssetPaths>().getRootFolderPath();
        const string  sourceDir    = FileUtil::joinPath( resourceRoot, folder._sourceID );
        const string  scratchDir   = FileUtil::joinPath( resourceRoot, folder._scratchID );
        if ( FileUtil::isDirectory( sourceDir ) == false )
        {
            return unexpected( Error{ ErrorKind::NotFound, "<Scratch from> is not a folder under Content/", folder._sourceID } );
        }
        AutomationScratchWorkspaceInternal::removeDeadInstances( scratchDir );
        // 지난 실행(중단된 것 포함)이 남긴 사본을 통째로 지운다 — 잔여 파일이 다음 실행의 이름을 바꾸지 않게 한다.
        if ( FileUtil::removeDirectory( scratchDir ) == false || FileUtil::ensureDirectoryExists( scratchDir ) == false )
        {
            return unexpected( Error{ ErrorKind::ExternalFailure, "could not reset the scratch folder", scratchDir } );
        }
        vector<string> listSourceFile;
        if ( FileUtil::collectFiles( sourceDir, "", listSourceFile, true ) == false )
        {
            return unexpected( Error{ ErrorKind::ExternalFailure, "could not list the scratch source folder", sourceDir } );
        }
        for ( const string& sourceFile : listSourceFile )
        {
            string relative;
            if ( FileUtil::makeRelativePath( sourceDir, sourceFile, relative ) == false )
            {
                return unexpected( Error{ ErrorKind::ExternalFailure, "could not place a source file under the scratch source folder", sourceFile } );
            }
            const string targetFile = FileUtil::joinPath( scratchDir, relative );
            if ( FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( targetFile ) ) == false )
            {
                return unexpected( Error{ ErrorKind::ExternalFailure, "could not create the folder of a scratch file", targetFile } );
            }
            vector<uint8> bytes;
            if ( FileUtil::readFile( sourceFile, bytes ) == false )
            {
                return unexpected( Error{ ErrorKind::ExternalFailure, "could not read a scratch source file", sourceFile } );
            }
            // NUL 이 든 파일은 바이너리다 — 그대로 복사한다. 글 파일은 원본 id 를 사본 id 로 바꾼다(에셋은 경로 하나로 가리키므로 id 를 바꾸면 사본끼리 이어진다).
            const bool bBinary = std::find( bytes.begin(), bytes.end(), uint8{ 0 } ) != bytes.end();
            if ( bBinary )
            {
                if ( FileUtil::writeFile( targetFile, bytes.data(), bytes.size() ) == false )
                {
                    return unexpected( Error{ ErrorKind::ExternalFailure, "could not write a scratch file", targetFile } );
                }
                continue;
            }
            const string_view sourceText( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
            string            text = AutomationScratch::rebaseText( sourceText, folder._sourceID, folder._scratchID );
            // 픽스처가 시나리오의 별칭(`as=` — `scratch/tilesetedit/…`)으로 서로를 가리키면 그 별칭도 이 실행의 사본 id 로 바꾼다.
            // 예전에는 별칭이 남아 꼬리 붙은 사본(`…_p<pid>`)의 타일맵이 없는 타일셋을 가리켰다(tilesetedit · uiaudit).
            if ( folder._declaredID.empty() == false && folder._declaredID != folder._scratchID )
                text = AutomationScratch::rebaseText( text, folder._declaredID, folder._scratchID );
            if ( FileUtil::writeTextFile( targetFile, text ) == false )
            {
                return unexpected( Error{ ErrorKind::ExternalFailure, "could not write a scratch file", targetFile } );
            }
        }
        if ( AutomationScratchCounter* const pCounter = engine::findSubsystem<AutomationScratchCounter>() )
            (void)pCounter->_prepareCount.fetch_add( 1 );
        return {};
    }

    void AutomationScratchWorkspace::acquireWriteGate()
    {
        FileUtil::setWriteGate( &AutomationScratchWorkspaceInternal::allowWrite );
    }

    void AutomationScratchWorkspace::releaseWriteGate()
    {
        FileUtil::setWriteGate( nullptr );
    }

    uint64 AutomationScratchWorkspace::getPrepareCount()
    {
        const AutomationScratchCounter* const pCounter = engine::findSubsystem<AutomationScratchCounter>();
        return pCounter != nullptr ? pCounter->_prepareCount.load() : 0u; // 엔진 범위가 없으면 만든 사본도 없다
    }
} // namespace sw
