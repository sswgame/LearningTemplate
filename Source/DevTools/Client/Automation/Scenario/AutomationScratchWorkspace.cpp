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

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Automation/Scenario/AutomationScenario.h"
#include "Engine/Automation/Scenario/AutomationScratch.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetRegistry.h"
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

            /** @brief 사본 파일 하나의 정체성입니다. */
            struct CopiedIdentity
            {
                AssetGUID _guid;
                string    _assetID;
            };

            /**
             * @brief 원본 파일들의 GUID 마다 사본의 GUID 를 정해 @p outMapGUID(원본 → 사본) · @p outListIdentity(사본 → 사본 id)에 담습니다.
             * @details 자기 정체성(머리 · `.meta`)은 사본 폴더 id 를 이름으로, 임포트 산출물은 사본 원본의 GUID 와 산출물 이름으로 — 사본을 디스크에서 읽을 때(`readIdentity`)와 같은 규칙이다.
             */
            static void collectIdentities( const AssetIdentityRules& rules, const AbsolutePath& sourceDir, const vector<AbsolutePath>& listSourceFile, const AutomationScratchFolder& folder,
                                           unordered_map<AssetGUID, AssetGUID>& outMapGUID, vector<CopiedIdentity>& outListIdentity )
            {
                for ( const AbsolutePath& sourceFile : listSourceFile )
                {
                    string relative;
                    if ( FileUtil::makeRelativePath( sourceDir.str(), sourceFile.str(), relative ) == false )
                        continue; // 복사가 그 파일에서 오류로 멈춘다
                    const string              sourceID = FileUtil::joinPath( folder._sourceID, FileUtil::normalizeSeparators( relative ) );
                    const optional<AssetGUID> guid     = rules.readIdentity( sourceFile, sourceID );
                    if ( guid.has_value() == false )
                        continue;
                    AssetGUID                   copy = AssetGUID::fromName( *guid, folder._scratchID );
                    vector<AssetImportedSource> listSource;
                    if ( rules.isUnderDerivedFolder( sourceID ) )
                        rules.collectImportedSources( sourceID, listSource );
                    for ( const AssetImportedSource& source : listSource )
                    {
                        // 산출물이면 사본 원본의 GUID 에서 난다. 원본이 사본 폴더 밖이면 산출물 사본은 새 원본 없는 파일이라 자기 이름을 갖는다.
                        string                    sourceRelative;
                        const bool                bCopiedSource = FileUtil::tryMakePathUnderRoot( folder._sourceID, source._sourceID, sourceRelative );
                        const optional<AssetGUID> owner         = bCopiedSource ? rules.readIdentity( sourceDir.join( sourceRelative ), source._sourceID ) : optional<AssetGUID>{};
                        if ( owner.has_value() == false || AssetGUID::fromName( *owner, source._outputName ) != *guid )
                            continue;
                        copy = AssetGUID::fromName( AssetGUID::fromName( *owner, folder._scratchID ), source._outputName );
                        break;
                    }
                    outMapGUID.insert_or_assign( *guid, copy );
                    outListIdentity.push_back( CopiedIdentity{ copy, FileUtil::joinPath( folder._scratchID, FileUtil::normalizeSeparators( relative ) ) } );
                }
            }

            /** @brief `FileUtil::WriteFilter` — 리소스 루트 아래 쓰기(사본은 실행 뿌리 아래라 루트 밖이다)와 프로젝트의 `docs/` · `Config/`(허용 목록 밖) 쓰기를 거절합니다. 그 밖(Saved · 임시 폴더)은 시나리오의 몫이라 막지 않는다. */
            static expected<void> allowWrite( string_view path )
            {
                // 지킬 원본(`Content`)이 없는 구성(패키지 - 팩만)에는 막을 것이 없다.
                const AbsolutePath rootPath = ProcessRoots::get()._content;
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
                const AbsolutePath projectRoot = ProcessRoots::get()._project;

                // 프로젝트 루트의 추적 폴더 — 허용 목록에 든 하위 폴더만 연다.
                for ( const string_view folder : kTrackedFolders )
                {
                    if ( FileUtil::isPathAtOrUnderRoot( FileUtil::joinPath( projectRoot.str(), folder ), path ) == false )
                        continue;
                    const bool bAllowed = std::any_of( std::begin( kAllowedConfigWrites ), std::end( kAllowedConfigWrites ), [&]( string_view allowed )
                    {
                        return FileUtil::isPathAtOrUnderRoot( FileUtil::joinPath( projectRoot.str(), allowed ), path );
                    } );
                    if ( bAllowed )
                        return {};
                    return unexpected( Error{ ErrorKind::Unsupported, "an automation run may not write this tracked folder (only Config/Editor is open); run on a <Scratch> copy or write under Saved/", folder } );
                }

                if ( FileUtil::isPathUnderRoot( rootPath.str(), path ) == false )
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
        return scratchDir.has_value() && FileUtil::removeDirectory( *scratchDir ); // 못 풀거나 못 지우면 AssetPaths · FileUtil 이 경로와 이유를 남긴다
    }

    expected<void> AutomationScratchWorkspace::prepare( const AutomationScratchFolder& folder )
    {
        if ( expected<void> valid = folder.validate(); valid.has_value() == false )
            return valid;
        const AssetPaths&            assetPaths = engine::getSubsystem<AssetPaths>();
        const AbsolutePath           sourceDir  = FileUtil::toDiskPath( FileUtil::joinPath( assetPaths.getRootFolderPath(), folder._sourceID ) );
        const optional<AbsolutePath> scratchAt  = assetPaths.toDiskPath( folder._scratchID ); // 실행 뿌리 아래다(`AssetPaths::getScratchFolderPath`)
        if ( FileUtil::isDirectory( sourceDir ) == false )
        {
            return unexpected( Error{ ErrorKind::NotFound, "<Scratch from> is not a folder under Content/", folder._sourceID } );
        }
        if ( scratchAt.has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<Scratch as> does not resolve to a disk folder", folder._scratchID } );
        const AbsolutePath scratchDir = *scratchAt;
        // 지난 실행(중단된 것 포함)이 남긴 사본을 통째로 지운다 — 잔여 파일이 다음 실행의 이름을 바꾸지 않게 한다.
        if ( FileUtil::removeDirectory( scratchDir ) == false || FileUtil::ensureDirectoryExists( scratchDir ) == false )
        {
            return unexpected( Error{ ErrorKind::ExternalFailure, "could not reset the scratch folder", scratchDir.str() } );
        }
        vector<AbsolutePath> listSourceFile;
        if ( FileUtil::collectFiles( sourceDir, "", listSourceFile, true ) == false )
        {
            return unexpected( Error{ ErrorKind::ExternalFailure, "could not list the scratch source folder", sourceDir.str() } );
        }
        AssetIdentityRules identityRules;
        if ( identityRules.load( ProcessRoots::get()._project ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the asset identity rules could not be read", AssetIdentityRules::kConfigFile } );
        unordered_map<AssetGUID, AssetGUID>                        mapGUID;
        vector<AutomationScratchWorkspaceInternal::CopiedIdentity> listIdentity;
        AutomationScratchWorkspaceInternal::collectIdentities( identityRules, sourceDir, listSourceFile, folder, mapGUID, listIdentity );
        for ( const AbsolutePath& sourceFile : listSourceFile )
        {
            string relative;
            if ( FileUtil::makeRelativePath( sourceDir.str(), sourceFile.str(), relative ) == false )
            {
                return unexpected( Error{ ErrorKind::ExternalFailure, "could not place a source file under the scratch source folder", sourceFile.str() } );
            }
            const AbsolutePath targetFile = scratchDir.join( relative );
            if ( FileUtil::ensureDirectoryExists( targetFile.getParent() ) == false )
            {
                return unexpected( Error{ ErrorKind::ExternalFailure, "could not create the folder of a scratch file", targetFile.str() } );
            }
            vector<uint8> bytes;
            if ( FileUtil::readFile( sourceFile, bytes ) == false )
            {
                return unexpected( Error{ ErrorKind::ExternalFailure, "could not read a scratch source file", sourceFile.str() } );
            }
            // NUL 이 든 파일은 바이너리다 — 그대로 복사한다. 글 파일은 원본 id 를 사본 id 로, 원본 GUID 를 사본 GUID 로 바꾼다(경로로도 GUID 로도 사본끼리 이어진다).
            const bool bBinary = std::find( bytes.begin(), bytes.end(), uint8{ 0 } ) != bytes.end();
            if ( bBinary )
            {
                if ( FileUtil::writeFile( targetFile, bytes.data(), bytes.size() ) == false )
                {
                    return unexpected( Error{ ErrorKind::ExternalFailure, "could not write a scratch file", targetFile.str() } );
                }
                continue;
            }
            const string_view sourceText( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
            // 픽스처가 시나리오의 별칭(`as=` — `scratch/tilesetedit/…`)으로 서로를 가리키면 그 id 는 사본 id 그대로라 바꿀 것이 없다(tilesetedit · uiaudit).
            const string text = rebaseIdentities( AutomationScratch::rebaseText( sourceText, folder._sourceID, folder._scratchID ), mapGUID );
            if ( FileUtil::writeTextFile( targetFile, text ) == false )
            {
                return unexpected( Error{ ErrorKind::ExternalFailure, "could not write a scratch file", targetFile.str() } );
            }
        }
        for ( const AutomationScratchWorkspaceInternal::CopiedIdentity& identity : listIdentity )
            AssetRegistry::record( identity._guid, identity._assetID ); // 사본끼리의 GUID 참조가 파일 감시를 기다리지 않고 풀린다
        if ( AutomationScratchCounter* const pCounter = engine::findSubsystem<AutomationScratchCounter>() )
            (void)pCounter->_prepareCount.fetch_add( 1 );
        return {};
    }

    string AutomationScratchWorkspace::rebaseIdentities( string_view text, const unordered_map<AssetGUID, AssetGUID>& mapGUID )
    {
        const auto isWordCharacter = []( utf8 character )
        { return ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || ( '0' <= character && character <= '9' ); };
        string result;
        result.reserve( text.size() );
        size_t cursor = 0;
        while ( cursor < text.size() )
        {
            size_t end = cursor;
            while ( end < text.size() && isWordCharacter( text[end] ) )
                ++end;
            const string_view         word = text.substr( cursor, end - cursor );
            const optional<AssetGUID> guid = AssetGUID::tryParse( word );
            const auto                copy = guid.has_value() ? mapGUID.find( *guid ) : mapGUID.end();
            if ( copy != mapGUID.end() )
                result.append( copy->second.toString() );
            else
                result.append( word );
            if ( end < text.size() )
                result.push_back( text[end] );
            cursor = end + 1;
        }
        return result;
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
