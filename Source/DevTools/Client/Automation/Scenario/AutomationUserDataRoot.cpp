#include "pch.h"

#include "DevTools/Client/Automation/Scenario/AutomationUserDataRoot.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/File/UserDataPath.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Foundation/QuitRequest.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
    /**
     * @brief `-gv_automationAllowOutsideUserData=true`: 자동화 실행(`-unattended` · `-scenario`)이 자동화 폴더 밖의 사용자 데이터 뿌리와 사람의 상태 폴더를 쓴다
     *        (환경 변수가 없으면 사람의 OS 사용자 폴더 그대로).
     * @details 기본은 거짓 — 자동화가 사람의 세이브 · 장치 키 · 에디터 상태를 바꾸거나 다른 실행과 한 폴더를 다투지 않게 한다(`AutomationUserDataRoot`). 일부러 실제 세이브로 재현할 때만 켠다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_automationAllowOutsideUserData, false,
                             "자동화 실행이 자동화 폴더(Saved/Automation) 밖의 사용자 데이터 폴더와 사람의 상태 폴더를 쓰게 둡니다 (사람의 실제 세이브로 재현할 때만)" );

    namespace
    {
        struct AutomationUserDataRootInternal
        {
            /**
             * @brief 실행마다 따로 써야 하는 상태 자리를 가리키는 전역 변수입니다 — 값을 주면 자동 자리(사용자 데이터 뿌리 · 프로세스별 폴더) 대신 그 자리를 읽고 쓴다.
             * @details `gv_editorStateDir`(에디터 상태 폴더 — `EditorPaths`), `gv_userSettingsFile`(사용자 설정 파일 — `DevToolsHost`). 산출물 경로(`gv_screenshot` · 트레이스 · 보고)는
             *          쓰기만 하고 다음 실행이 읽지 않아 여기 넣지 않는다. 새 상태 자리 변수를 만들면 여기에 더한다.
             */
            static constexpr string_view kArrStateVariable[] = { "gv_editorStateDir", "gv_userSettingsFile" };

            /** @brief @p path 의 마디 가운데 `..` 가 있으면 true 입니다. */
            static bool hasParentSegment( string_view path )
            {
                StringBuilder<constant::buffer::kMax1024> wrapped;
                wrapped.appendFormat( "/%#/", FileUtil::normalizeSeparators( path ) );
                return wrapped.view().find( "/../" ) != string_view::npos;
            }

            /**
             * @brief 상태 자리 변수 @p name 의 값(절대 경로)입니다. 주지 않았으면 빈 글. 상대 경로를 풀 수 없으면 오류입니다.
             * @details 에디터 변수는 기동 단계에 아직 등록 전이라(에디터 모듈이 뒤에 선다) 명령줄의 보류 값으로 읽고 작업 폴더 기준으로 푼다(`GlobalDiskPath` 와 같은 규칙).
             */
            static expected<string> readStateVariable( const CommandLineManager& commandLine, string_view name )
            {
                if ( const GlobalVariableInfo* pVariable = engine::getSubsystem<GlobalVariableManager>().findVariable( name ) )
                    return pVariable->getValueAsString(); // 등록된 디스크 경로 변수는 받을 때 이미 절대로 풀었다
                string text;
                if ( commandLine.findPendingGlobalValue( name, text ) == false || text.empty() )
                    return string{};
                const optional<AbsolutePath> workingFolder = AbsolutePath::tryMake( FileUtil::getCurrentPath().str() );
                const optional<AbsolutePath> resolved      = workingFolder.has_value() ? AbsolutePath::tryResolve( text, *workingFolder ) : optional<AbsolutePath>{};
                if ( resolved.has_value() == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "a state path variable is not a usable path", text } );
                return resolved->str();
            }

            /** @brief 감시 값 `UserData.Isolated` — 사용자 데이터 뿌리가 자동화 폴더 아래면 1, 아니면 0 입니다. */
            [[nodiscard]] static bool readIsolated( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                string     root;
                const bool bUnder = ProcessEnvironment::tryReadNonEmpty( kUserDataRootVariable, root ) && AutomationUserDataRoot::validateUnderBase( root, AutomationUserDataRoot::getBaseFolder().str() ).has_value();
                outValue          = bUnder ? 1.0 : 0.0;
                return true;
            }

            /** @brief 감시 값 `UserData.Root` — 이 실행의 사용자 데이터 폴더(활성 게임)입니다. 실행마다 다른 경로라 `<Record>` 로 견주지 않는다(진단용). */
            [[nodiscard]] static bool readRoot( const AutomationWatchQuery& /*query*/, AutomationWatchValue& outValue )
            {
                outValue.setText( UserDataPath::getDataDirectory( GameSelection::getName() ).str() );
                return true;
            }

            /** @brief 감시 값 `UserData.PlatformFolderChanged` — 사람의 데이터 폴더(활성 게임)가 이 실행의 기동 뒤로 바뀌었으면 1 입니다. 그 폴더를 모르면 값이 없다. */
            [[nodiscard]] static bool readPlatformFolderChanged( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AutomationUserDataIsolation* const pIsolation = engine::areEngineSubsystemsBound() ? engine::findSubsystem<AutomationUserDataIsolation>() : nullptr;
                if ( pIsolation == nullptr || pIsolation->getPlatformFolder().empty() )
                    return false;
                outValue = UserDataFolderStamp::take( pIsolation->getPlatformFolder() ) != pIsolation->getPlatformStartStamp() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( userDataIsolated, "UserData.Isolated", "1 while the user data root (SW_USER_DATA_ROOT) is under Saved/Automation - saves cannot touch the person's folder",
                         &AutomationUserDataRootInternal::readIsolated );
    SW_AUTOMATION_QUERY_WATCH( userDataRoot, "UserData.Root", "The user data folder of the active game in this run (differs per run - for diagnosis, not <Record>)",
                               &AutomationUserDataRootInternal::readRoot, false, false );
    // 활동 수 없음: 바뀜은 OS 폴더 도장(항목 수 · 마지막 쓰기 시각)이 한 번 달라지는 사건이라 그 앞에 셀 중간 진척이 없다 — 시한만 본다.
    SW_AUTOMATION_WALL_WATCH( userDataPlatformFolderChanged, "UserData.PlatformFolderChanged",
                              "1 when the person's OS user data folder of the active game changed since this run started (entry count or latest write time)",
                              &AutomationUserDataRootInternal::readPlatformFolderChanged, nullptr );

    AbsolutePath AutomationUserDataRoot::getBaseFolder()
    {
        return ProcessRoots::get()._saved.join( "Automation" );
    }

    AbsolutePath AutomationUserDataRoot::makeRunUserDataRoot( const AbsolutePath& runRoot )
    {
        return runRoot.join( kRunSubfolder );
    }

    expected<void> AutomationUserDataRoot::validateUnderBase( string_view root, string_view baseFolder )
    {
        using Internal = AutomationUserDataRootInternal;
        if ( AbsolutePath::isStrictAbsolute( root ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the user data root is not an absolute path", root } );
        if ( Internal::hasParentSegment( root ) )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the user data root has a '..' segment", root } );
        // 대소문자는 OS 규칙이다 — 리눅스에서 `userdata` 는 `UserData` 가 아니다(예전 소문자 비교는 그 바깥 폴더를 안으로 봤다).
        if ( FileUtil::isPathUnderRoot( baseFolder, root ) == false )
            return unexpected( Error{ ErrorKind::Unsupported, "an automated run keeps user data under Saved/Automation (pass -gv_automationAllowOutsideUserData=true to use another folder on purpose)", root } );
        return {};
    }

    vector<string> AutomationUserDataRoot::makePersonFolders()
    {
        vector<string> listFolder{ ProcessRoots::get()._project.join( config::kDirSavedEditor ).str() };
        // 게임마다 폴더의 부모(`SWEngine` · `swengine`) — 모든 게임의 사람 세이브 · 설정이다. 모르면(샌드박스) 그 칸이 없다.
        const string listGameFolder[] = { UserDataPath::findPlatformDataDirectory( "" ).str(), UserDataPath::findPlatformConfigDirectory( "" ).str() };
        for ( const string& gameFolder : listGameFolder )
        {
            if ( gameFolder.empty() == false )
                listFolder.push_back( FileUtil::getDirectoryPart( gameFolder ) );
        }
        return listFolder;
    }

    expected<void> AutomationUserDataRoot::validateStatePath( string_view name, string_view path, const vector<string>& listPersonFolder )
    {
        for ( const string& folder : listPersonFolder )
        {
            if ( FileUtil::isPathAtOrUnderRoot( folder, path ) )
            {
                StringBuilder<constant::buffer::kMax1024> detail;
                detail.appendFormat( "%#=%#", name, path );
                return unexpected( Error{ ErrorKind::Unsupported, "an automated run may not point a state variable at the person's folder (pass -gv_automationAllowOutsideUserData=true on purpose)",
                                          detail.view() } );
            }
        }
        return {};
    }

    optional<string> AutomationUserDataRoot::findSharedStateArgument( const vector<string>& listArgument )
    {
        using Internal = AutomationUserDataRootInternal;
        for ( const string& argument : listArgument )
        {
            const string_view name = CommandLineManager::getArgumentName( argument );
            if ( std::find( std::begin( Internal::kArrStateVariable ), std::end( Internal::kArrStateVariable ), name ) != std::end( Internal::kArrStateVariable ) )
                return argument;
        }
        return {};
    }

    expected<string> AutomationUserDataRoot::resolve( bool bAllowOutside )
    {
        string root;
        if ( ProcessEnvironment::tryReadNonEmpty( kUserDataRootVariable, root ) )
        {
            if ( bAllowOutside )
                return root;
            if ( expected<void> under = validateUnderBase( root, getBaseFolder().str() ); under.has_value() == false )
                return unexpected( under.error() );
            return root;
        }
        if ( bAllowOutside )
            return string{}; // 사람이 고른 OS 사용자 폴더 그대로
        // 실행 뿌리는 실행마다 새 폴더다 — 옛 실행의 세이브가 없어 비울 것도, 거둘 형제도 없다(거두기는 `SavedCleanup`).
        root = makeRunUserDataRoot( ProcessRoots::get()._run ).str();
        if ( expected<void> under = validateUnderBase( root, getBaseFolder().str() ); under.has_value() == false )
            return unexpected( under.error() ); // 부모가 실행 뿌리를 자동화 폴더 밖에 줬다
        if ( ProcessEnvironment::trySet( kUserDataRootVariable, root ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "could not set the user data root variable", root } );
        return root;
    }

    bool AutomationUserDataRoot::isOutsideAllowed()
    {
        return gv_automationAllowOutsideUserData;
    }

    UserDataFolderStamp UserDataFolderStamp::take( const AbsolutePath& folder )
    {
        UserDataFolderStamp stamp;
        stamp._bExists = FileUtil::isDirectory( folder );
        if ( stamp._bExists == false )
            return stamp;
        (void)FileUtil::getFileWriteTime( folder, stamp._latestWriteTicks ); // 못 읽으면 0 — 항목 시각이 채운다
        (void)FileUtil::forEachDirectoryEntry( folder, true, [&stamp]( const DirectoryEntry& entry )
        {
            ++stamp._entryCount;
            int64 ticks = 0;
            if ( FileUtil::getFileWriteTime( entry.makePath(), ticks ) && ticks > stamp._latestWriteTicks )
                stamp._latestWriteTicks = ticks;
            return true;
        } ); // 훑다 멈추면 그때까지의 수 — 다음에 찍을 때 달라져 바뀐 것으로 보인다(거짓 통과가 아니다)
        return stamp;
    }

    SubsystemInitResult AutomationUserDataIsolation::initializeSubsystem()
    {
        using Internal                        = AutomationUserDataRootInternal;
        const CommandLineManager& commandLine = engine::getSubsystem<CommandLineManager>();
        if ( commandLine.isAutomatedRun() == false )
            return SubsystemInitResult::Succeeded; // 사람의 실행은 사람의 폴더를 쓴다
        _platformFolder                = UserDataPath::findPlatformDataDirectory( GameSelection::getName() );
        _platformStartStamp            = _platformFolder.empty() ? UserDataFolderStamp{} : UserDataFolderStamp::take( _platformFolder );
        const bool       bAllowOutside = AutomationUserDataRoot::isOutsideAllowed();
        expected<string> root          = AutomationUserDataRoot::resolve( bAllowOutside );
        if ( root.has_value() == false )
            return reject( root.error() );
        _root = std::move( *root );
        // 실행마다 따로 써야 하는 상태 자리를 명령줄로 옮겼으면 그 자리도 사람의 폴더가 아니어야 한다(뿌리와 같은 판정 — 주지 않았으면 자동 자리라 볼 것이 없다).
        if ( bAllowOutside == false )
        {
            const vector<string> listPersonFolder = AutomationUserDataRoot::makePersonFolders();
            for ( const string_view name : Internal::kArrStateVariable )
            {
                const expected<string> path = Internal::readStateVariable( commandLine, name );
                if ( path.has_value() == false )
                    return reject( path.error() );
                if ( path->empty() )
                    continue;
                if ( expected<void> allowed = AutomationUserDataRoot::validateStatePath( name, *path, listPersonFolder ); allowed.has_value() == false )
                    return reject( allowed.error() );
            }
        }
        SW_LOG_INFO_ID( "AutomationUserData.Root", "[Automation] user data root: %#", _root.empty() ? "(the OS user folder - allowed on purpose)" : _root.c_str() );
        return SubsystemInitResult::Succeeded;
    }

    SubsystemInitResult AutomationUserDataIsolation::reject( const Error& error )
    {
        SW_LOG_ERROR_ID( "AutomationUserData.RootRejected", "[Automation] %#: %#", error.message(), error.detail() );
        // 시작할 수 없는 자동화 실행 — 초기화 실패(17)가 아니라 로드 오류(11)로 끝낸다(`App::getInitFailureExitCode`)
        engine::getSubsystem<QuitRequest>().request( ProcessExitCode::LoadError );
        return SubsystemInitResult::Failed;
    }
} // namespace sw
