#include "pch.h"

#include "Engine/Automation/Scenario/AutomationScratch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Process/Process.h"
#include "Core/UUID/UUID.h"

#include "Engine/Asset/AssetMeta.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    namespace
    {
        struct AutomationScratchInternal
        {
            /**
             * @brief 사본 폴더를 끝까지 만든 횟수입니다(`AutomationScratch::getPrepareCount`).
             * @details 읽는 쪽(에디터 감시 울타리)이 서비스 범위를 거치지 않고 정적 창구로 읽으므로 범위에 맡기지 않는다. 원자 변수 하나라 상수 초기화되고,
             *          지연 생성하는 접근자를 두지 않는다. 프로세스가 도는 동안 오르기만 한다.
             */
            static inline atomic<uint64> s_prepareCount{ 0 };

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

            /** @brief @p target(정규화한 절대 경로)이 @p folder(루트 뒤 상대 폴더) 자신이거나 그 아래인지입니다. */
            static bool isInside( string_view target, string_view projectRoot, string_view folder )
            {
                const string base = string( projectRoot ) + string( folder );
                return target == base || StringUtil::startsWith( target, base + "/" );
            }

            /** @brief `FileUtil::WriteGate` — 리소스 루트 아래에서 `scratch/` 밖의 쓰기와 프로젝트의 `docs/` · `Config/`(허용 목록 밖) 쓰기를 거절합니다. 그 밖(Saved · 임시 폴더)은 시나리오의 몫이라 막지 않는다. */
            static bool allowWrite( string_view path, string& outReason )
            {
                const string& rootPath = engine::getSubsystem<AssetPaths>().getRootFolderPath();
                if ( rootPath.empty() )
                    return true; // 리소스 루트가 아직 없으면 지킬 원본도 없다
                string absolutePath;
                if ( FileUtil::isAbsolutePath( path ) )
                    absolutePath = string( path );
                else if ( FileUtil::makeAbsolutePath( path, absolutePath ) == false )
                    return true;
                const string root        = FileUtil::normalizePath( rootPath ) + "/";
                const string scratchRoot = root + AutomationScratch::kScratchDomain + "/";
                const string target      = FileUtil::normalizePath( absolutePath );

                // 프로젝트 루트(Resource 의 부모)의 추적 폴더 — 허용 목록에 든 하위 폴더만 연다.
                const string projectRoot = FileUtil::normalizePath( FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( rootPath ) ) ) + "/";
                for ( const string_view folder : kTrackedFolders )
                {
                    if ( isInside( target, projectRoot, folder ) == false )
                        continue;
                    const bool bAllowed = std::any_of( std::begin( kAllowedConfigWrites ), std::end( kAllowedConfigWrites ), [&]( string_view allowed )
                    {
                        return isInside( target, projectRoot, allowed );
                    } );
                    if ( bAllowed )
                        return true;
                    outReason = "an automation run may not write the tracked folder " + string( folder ) + "/ (only Config/Editor is open); run on a copy under Content/scratch/ or write under Saved/";
                    return false;
                }

                const bool bScratch = StringUtil::startsWith( target, scratchRoot ) || target + "/" == scratchRoot; // 사본 도메인 폴더 자체도 사본의 것이다
                if ( StringUtil::startsWith( target, root ) == false || bScratch )
                    return true;
                // 골든 갱신 실행(`-scenario-update-golden`)만 시나리오 옆 `golden/` 에 쓴다 — 그 실행이 하는 일이 바로 그것이고, 쓴 파일은 보고에 남아 리뷰한다.
                bool bUpdateGolden = false;
                if ( engine::areEngineServicesBound() )
                    (void)engine::getSubsystem<CommandLineManager>().getArgument( CommandLineArgument::SCENARIO_UPDATE_GOLDEN, bUpdateGolden );
                if ( bUpdateGolden && target.find( "/automation/golden/" ) != string::npos )
                    return true;
                outReason = "an automation run may write under Resource only inside " + string( AutomationScratch::kScratchDomain ) + "/ (declare <Scratch>); this path is a tracked original";
                return false;
            }

            /** @brief 프로세스 꼬리의 앞머리(`<이름>_p<pid>`)입니다. */
            static constexpr string_view kInstanceMark = "_p";

            /** @brief 리소스 id 에 들어가는 글자인지입니다(id 경계 판정). */
            static bool isIDCharacter( utf8 character )
            {
                const bool bLetterOrDigit = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || ( '0' <= character && character <= '9' );
                return bLetterOrDigit || character == '_' || character == '.' || character == '/' || character == '-';
            }

            /** @brief @p text 가 비지 않은 10 진 숫자만이면 그 값을 @p outValue 에 넣고 true 입니다. */
            [[nodiscard]] static bool parseProcessID( string_view text, int32& outValue )
            {
                const bool bDigits = text.empty() == false && VectorUtil::allOf( text, []( utf8 character )
                {
                    return '0' <= character && character <= '9';
                } );
                return bDigits && StringUtil::parseInt( text, outValue );
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
                const string folderName = FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( scratchDir ) );
                const size_t markAt     = folderName.rfind( kInstanceMark );
                int32        ownID      = 0;
                if ( markAt == string::npos || parseProcessID( string_view( folderName ).substr( markAt + kInstanceMark.size() ), ownID ) == false )
                    return;
                const string   prefix = folderName.substr( 0, markAt + kInstanceMark.size() );
                vector<string> listDeadDir;
                (void)FileUtil::forEachDirectoryEntry( FileUtil::getDirectoryPart( scratchDir ), false, [&prefix, ownID, &listDeadDir]( const DirectoryEntry& entry )
                {
                    string_view name;
                    FileUtil::getFileNamePart( entry._path, name );
                    int32      processID = 0;
                    const bool bSibling  = entry._bDirectory && StringUtil::startsWith( name, prefix ) && parseProcessID( name.substr( prefix.size() ), processID );
                    const bool bDead     = bSibling && processID != ownID && Process::isProcessAlive( processID ) == false;
                    if ( bDead )
                        listDeadDir.emplace_back( entry._path );
                    return true;
                } ); // 상위 폴더를 못 열면 거둘 것도 없다 — 이 실행의 사본은 prepare 가 따로 만든다
                for ( const string& deadDir : listDeadDir )
                {
                    (void)FileUtil::removeDirectory( deadDir ); // 못 지우면 경고가 남고 다음 실행이 다시 거둔다(위 설명)
                }
            }

            /**
             * @brief `.meta`(이미 사본 id 로 바꾼 글)를 `AssetMeta` 로 읽어 guid 를 새로 만든 글을 돌려줍니다. 원본과 사본이 같은 guid 를 가지면 안 된다.
             * @details 글자 찾기로 고치지 않는다 — 형식이 바뀌면(키=값 → XML) 조용히 그대로 복사됐다. 읽지 못하면 오류다.
             */
            [[nodiscard]] static bool renewMetaGUID( string_view text, const string& sourceFile, string& outText, string& outError )
            {
                AssetMeta meta;
                if ( meta.parseXML( text, sourceFile.c_str() ) == false )
                {
                    outError = "could not read the asset meta " + sourceFile;
                    return false;
                }
                meta._guid = UUID::generate().toString();
                outText    = meta.toXML();
                return true;
            }
        };
    } // namespace

    bool AutomationScratch::isValidFolder( const AutomationScratchFolder& folder, string& outReason )
    {
        const string scratchPrefix = string( kScratchDomain ) + "/";
        if ( folder._sourceID.empty() || folder._scratchID.empty() )
        {
            outReason = "<Scratch> needs from=\"<resource id>\" and as=\"scratch/<name>\"";
            return false;
        }
        const bool bLeavesTree = AssetPaths::hasParentDirectoryComponent( folder._sourceID ) || AssetPaths::hasParentDirectoryComponent( folder._scratchID );
        if ( bLeavesTree )
        {
            outReason = "<Scratch> ids may not use '..'";
            return false;
        }
        if ( StringUtil::startsWith( folder._scratchID, scratchPrefix ) == false )
        {
            outReason = "<Scratch as=\"" + folder._scratchID + "\"> must sit under " + scratchPrefix;
            return false;
        }
        if ( StringUtil::startsWith( folder._sourceID, scratchPrefix ) )
        {
            outReason = "<Scratch from=\"" + folder._sourceID + "\"> must be a tracked folder, not another scratch copy";
            return false;
        }
        return true;
    }

    string AutomationScratch::rebaseText( string_view text, string_view fromID, string_view toID )
    {
        string result;
        result.reserve( text.size() );
        size_t cursor = 0;
        while ( cursor < text.size() )
        {
            const size_t hit = text.find( fromID, cursor );
            if ( hit == string_view::npos || fromID.empty() )
                break;
            const size_t end            = hit + fromID.size();
            const bool   bStartBoundary = hit == 0 || AutomationScratchInternal::isIDCharacter( text[hit - 1] ) == false;
            const bool   bEndBoundary   = end == text.size() || text[end] == '/' || AutomationScratchInternal::isIDCharacter( text[end] ) == false;
            const bool   bAtBoundary    = bStartBoundary && bEndBoundary;
            result.append( text.substr( cursor, hit - cursor ) );
            result.append( bAtBoundary ? toID : fromID );
            cursor = end;
        }
        result.append( text.substr( cursor ) ); // 찾은 끝(`hit + fromID.size()`)은 글 길이를 넘지 않는다
        return result;
    }

    string AutomationScratch::makeInstanceID( string_view name )
    {
        return string( name ) + string( AutomationScratchInternal::kInstanceMark ) + to_string( Process::getCurrentProcessID() );
    }

    bool AutomationScratch::prepare( const AutomationScratchFolder& folder, string& outError )
    {
        if ( isValidFolder( folder, outError ) == false )
            return false;
        const string& resourceRoot = engine::getSubsystem<AssetPaths>().getRootFolderPath();
        const string  sourceDir    = FileUtil::joinPath( resourceRoot, folder._sourceID );
        const string  scratchDir   = FileUtil::joinPath( resourceRoot, folder._scratchID );
        if ( FileUtil::isDirectory( sourceDir ) == false )
        {
            outError = "<Scratch from=\"" + folder._sourceID + "\"> is not a folder under Content/";
            return false;
        }
        AutomationScratchInternal::removeDeadInstances( scratchDir );
        // 지난 실행(중단된 것 포함)이 남긴 사본을 통째로 지운다 — 잔여 파일이 다음 실행의 이름을 바꾸지 않게 한다.
        if ( FileUtil::removeDirectory( scratchDir ) == false || FileUtil::ensureDirectoryExists( scratchDir ) == false )
        {
            outError = "could not reset the scratch folder " + scratchDir;
            return false;
        }
        vector<string> listSourceFile;
        if ( FileUtil::collectFiles( sourceDir, "", listSourceFile, true ) == false )
        {
            outError = "could not list " + sourceDir;
            return false;
        }
        for ( const string& sourceFile : listSourceFile )
        {
            string relative;
            if ( FileUtil::makeRelativePath( sourceDir, sourceFile, relative ) == false )
            {
                outError = "could not place " + sourceFile + " under " + sourceDir;
                return false;
            }
            const string targetFile = FileUtil::joinPath( scratchDir, relative );
            if ( FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( targetFile ) ) == false )
            {
                outError = "could not create the folder of " + targetFile;
                return false;
            }
            vector<uint8> bytes;
            if ( FileUtil::readFile( sourceFile, bytes ) == false )
            {
                outError = "could not read " + sourceFile;
                return false;
            }
            // NUL 이 든 파일은 바이너리다 — 그대로 복사한다. 글 파일은 원본 id 를 사본 id 로 바꾸고, `.meta` 는 guid 를 새로 만든다.
            const bool bBinary = std::find( bytes.begin(), bytes.end(), uint8{ 0 } ) != bytes.end();
            if ( bBinary )
            {
                if ( FileUtil::writeFile( targetFile, bytes.data(), bytes.size() ) == false )
                {
                    outError = "could not write " + targetFile;
                    return false;
                }
                continue;
            }
            const string_view sourceText( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
            string            text  = rebaseText( sourceText, folder._sourceID, folder._scratchID );
            const bool        bMeta = StringUtil::endsWith( FileUtil::normalizePath( targetFile ), ".meta" );
            if ( bMeta && AutomationScratchInternal::renewMetaGUID( string( text ), sourceFile, text, outError ) == false )
                return false;
            if ( FileUtil::writeTextFile( targetFile, text ) == false )
            {
                outError = "could not write " + targetFile;
                return false;
            }
        }
        (void)AutomationScratchInternal::s_prepareCount.fetch_add( 1 );
        return true;
    }

    void AutomationScratch::acquireWriteGate()
    {
        FileUtil::setWriteGate( &AutomationScratchInternal::allowWrite );
    }

    void AutomationScratch::releaseWriteGate()
    {
        FileUtil::setWriteGate( nullptr );
    }

    uint64 AutomationScratch::getPrepareCount()
    {
        return AutomationScratchInternal::s_prepareCount.load();
    }
} // namespace sw
