#include "pch.h"

#include "DevTools/Client/Automation/Scenario/AutomationScratchWorkspace.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/Subsystem.h"

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

            /** @brief `FileUtil::WriteFilter` — 리소스 루트 아래 쓰기(사본은 실행 뿌리 아래라 루트 밖이다)와 프로젝트의 `docs/` · `Config/`(허용 목록 밖) 쓰기를 거절합니다. 그 밖(Saved · 임시 폴더)은 시나리오의 몫이라 막지 않는다. */
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
                StringBuilder<constant::buffer::kMax1024> wrapped;
                wrapped.appendFormat( "/%#/", FileUtil::normalizeSeparators( path ) );
                if ( wrapped.view().find( "/../" ) != string_view::npos )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the path has a '..' segment - a scenario writes to a resolved path", path } );
                // 포함 판정은 OS 대소문자 규칙이다(`isPathAtOrUnderRoot`). 사본(작업 도메인)은 실행 뿌리 아래라 리소스 루트 밖이다 — 아래 "루트 밖은 연다" 가 연다.
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
                    return unexpected( Error{ ErrorKind::Unsupported, "an automation run may not write this tracked folder (only Config/Editor is open); run on a <Scratch> copy or write under Saved/", folder } );
                }

                if ( FileUtil::isPathUnderRoot( rootPath, path ) == false )
                    return {};
                const string target = FileUtil::makeComparableDiskPath( path ).str(); // 폴더 이름 판정용 글(OS 규칙으로 접었다)
                // 골든 기록 실행(`-scenario-update-golden` · `-scenario-record-missing-golden`)만 시나리오 옆 `golden/`(자동화 폴더 아래 어느 깊이든 — `engine/automation/editor/golden/`)에 쓴다.
                // 그 실행이 하는 일이 바로 그것이고, 쓴 파일은 보고에 남아 리뷰한다. 무엇을 덮을지는 판정(`AutomationGoldenUtil::decide`)이 가른다. 폴더 이름은 판정이 쓰는 것과 같은 상수다.
                bool bUpdateGolden        = false;
                bool bRecordMissingGolden = false;
                if ( engine::areEngineServicesBound() )
                {
                    const CommandLineManager& commandLine = engine::getSubsystem<CommandLineManager>();
                    (void)commandLine.getArgument( CommandLineArgument::SCENARIO_UPDATE_GOLDEN, bUpdateGolden );
                    (void)commandLine.getArgument( CommandLineArgument::SCENARIO_RECORD_MISSING_GOLDEN, bRecordMissingGolden );
                }
                const bool bGoldenFolder = FileUtil::getFileNamePart( FileUtil::getDirectoryPart( target ) ) == kAutomationGoldenFolder;
                if ( ( bUpdateGolden || bRecordMissingGolden ) && bGoldenFolder && target.find( "/automation/" ) != string::npos )
                    return {};
                return unexpected( Error{ ErrorKind::Unsupported, "an automation run may write under Resource only inside this folder (declare <Scratch>); this path is a tracked original", AutomationScratch::kScratchDomain } );
            }
        };
    } // namespace

    bool AutomationScratchWorkspace::discard( const AutomationScratchFolder& folder )
    {
        const optional<AbsolutePath> scratchDir = engine::getSubsystem<AssetPaths>().toDiskPath( folder._scratchID );
        return scratchDir.has_value() && FileUtil::removeDirectory( scratchDir->str() ); // 못 풀거나 못 지우면 AssetPaths · FileUtil 이 경로와 이유를 남긴다
    }

    expected<void> AutomationScratchWorkspace::prepare( const AutomationScratchFolder& folder )
    {
        if ( expected<void> valid = folder.validate(); valid.has_value() == false )
            return valid;
        const AssetPaths&            assetPaths = engine::getSubsystem<AssetPaths>();
        const string                 sourceDir  = FileUtil::joinPath( assetPaths.getRootFolderPath(), folder._sourceID );
        const optional<AbsolutePath> scratchAt  = assetPaths.toDiskPath( folder._scratchID ); // 실행 뿌리 아래다(`AssetPaths::getScratchFolderPath`)
        if ( FileUtil::isDirectory( sourceDir ) == false )
        {
            return unexpected( Error{ ErrorKind::NotFound, "<Scratch from> is not a folder under Content/", folder._sourceID } );
        }
        if ( scratchAt.has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<Scratch as> does not resolve to a disk folder", folder._scratchID } );
        const string& scratchDir = scratchAt->str();
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
            // 픽스처가 시나리오의 별칭(`as=` — `scratch/tilesetedit/…`)으로 서로를 가리키면 그 id 는 사본 id 그대로라 바꿀 것이 없다(tilesetedit · uiaudit).
            const string text = AutomationScratch::rebaseText( sourceText, folder._sourceID, folder._scratchID );
            if ( FileUtil::writeTextFile( targetFile, text ) == false )
            {
                return unexpected( Error{ ErrorKind::ExternalFailure, "could not write a scratch file", targetFile } );
            }
        }
        if ( AutomationScratchCounter* const pCounter = engine::findSubsystem<AutomationScratchCounter>() )
            (void)pCounter->_prepareCount.fetch_add( 1 );
        return {};
    }

    FileUtil::WriteFilterRegistration AutomationScratchWorkspace::acquireWriteFilter()
    {
        return FileUtil::bindWriteFilter( &AutomationScratchWorkspaceInternal::allowWrite );
    }

    uint64 AutomationScratchWorkspace::getPrepareCount()
    {
        const AutomationScratchCounter* const pCounter = engine::findSubsystem<AutomationScratchCounter>();
        return pCounter != nullptr ? pCounter->_prepareCount.load() : 0u; // 엔진 범위가 없으면 만든 사본도 없다
    }
} // namespace sw
