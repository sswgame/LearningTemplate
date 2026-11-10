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
#include "Core/String/StringUtil.h"

#include "DevTools/Client/Automation/Scenario/AutomationScratchWorkspace.h"

#include "Engine/Automation/Scenario/AutomationScratch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/GameSelection.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/QuitRequest.h"

namespace sw
{
    /**
     * @brief `-gv_scenarioAllowOutsideUserData=true`: 시나리오 실행이 자동화 폴더 밖의 사용자 데이터 뿌리를 쓴다(환경 변수가 없으면 사람의 OS 사용자 폴더 그대로).
     * @details 기본은 거짓 — 시나리오가 사람의 세이브 · 장치 키를 바꾸거나 다른 실행과 한 폴더를 다투지 않게 한다(`AutomationUserDataRoot`). 일부러 실제 세이브로 재현할 때만 켠다.
     */
    SW_TEST_GLOBAL_VARIABLE( bool, gv_scenarioAllowOutsideUserData, false, "시나리오 실행이 자동화 폴더(Saved/Automation/UserData) 밖의 사용자 데이터 폴더를 쓰게 둡니다 (사람의 실제 세이브로 재현할 때만)" );

    namespace
    {
        struct AutomationUserDataRootInternal
        {
            /** @brief 비교용 정규화(구분자 `/` · 소문자 · 끝 구분자 없음)입니다. */
            static string makeComparable( string_view path ) { return FileUtil::trimTrailingSlashes( FileUtil::normalizePath( path ) ); }

            /** @brief @p path 의 마디 가운데 `..` 가 있으면 true 입니다. */
            static bool hasParentSegment( string_view path ) { return ( "/" + FileUtil::normalizePath( path ) + "/" ).find( "/../" ) != string::npos; }

            /** @brief 감시 값 `UserData.Isolated` — 사용자 데이터 뿌리가 자동화 폴더 아래면 1, 아니면 0 입니다. */
            [[nodiscard]] static bool readIsolated( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                string     root;
                const bool bUnder = ProcessEnvironment::tryReadNonEmpty( kUserDataRootVariable, root ) && AutomationUserDataRoot::checkUnderBase( root, AutomationUserDataRoot::getBaseFolder() ).has_value();
                outValue          = bUnder ? 1.0 : 0.0;
                return true;
            }

            /** @brief 감시 값 `UserData.Root` — 이 실행의 사용자 데이터 폴더(활성 게임)입니다. 실행마다 다른 경로라 `<Record>` 로 견주지 않는다(진단용). */
            [[nodiscard]] static bool readRoot( const AutomationWatchQuery& /*query*/, AutomationWatchValue& outValue )
            {
                outValue.setText( UserDataPath::getDataDirectory( GameSelection::getName() ) );
                return true;
            }

            /** @brief 감시 값 `UserData.PlatformFolderChanged` — 사람의 데이터 폴더(활성 게임)가 이 실행의 기동 뒤로 바뀌었으면 1 입니다. 그 폴더를 모르면 값이 없다. */
            [[nodiscard]] static bool readPlatformFolderChanged( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                const AutomationUserDataIsolation* const pIsolation = engine::areEngineServicesBound() ? engine::findSubsystem<AutomationUserDataIsolation>() : nullptr;
                if ( pIsolation == nullptr || pIsolation->getPlatformFolder().empty() )
                    return false;
                outValue = UserDataFolderStamp::take( pIsolation->getPlatformFolder() ) != pIsolation->getPlatformStartStamp() ? 1.0 : 0.0;
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( userDataIsolated, "UserData.Isolated", "1 while the user data root (SW_USER_DATA_ROOT) is under Saved/Automation/UserData - saves cannot touch the person's folder",
                         &AutomationUserDataRootInternal::readIsolated );
    SW_AUTOMATION_QUERY_WATCH( userDataRoot, "UserData.Root", "The user data folder of the active game in this run (differs per run - for diagnosis, not <Record>)",
                               &AutomationUserDataRootInternal::readRoot, false, false );
    SW_AUTOMATION_WALL_WATCH( userDataPlatformFolderChanged, "UserData.PlatformFolderChanged",
                              "1 when the person's OS user data folder of the active game changed since this run started (entry count or latest write time)",
                              &AutomationUserDataRootInternal::readPlatformFolderChanged );

    string AutomationUserDataRoot::getBaseFolder()
    {
        return FileUtil::joinPath( ProcessRoots::get()._saved.str(), "Automation/UserData" );
    }

    expected<void> AutomationUserDataRoot::checkUnderBase( string_view root, string_view baseFolder )
    {
        using Internal = AutomationUserDataRootInternal;
        if ( AbsolutePath::isStrictAbsolute( root ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the user data root is not an absolute path", root } );
        if ( Internal::hasParentSegment( root ) )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the user data root has a '..' segment", root } );
        const string target = Internal::makeComparable( root );
        const string base   = Internal::makeComparable( baseFolder );
        if ( StringUtil::startsWith( target, base + "/" ) == false )
            return unexpected( Error{ ErrorKind::Unsupported, "a scenario run keeps user data under Saved/Automation/UserData (pass -gv_scenarioAllowOutsideUserData=true to use another folder on purpose)", root } );
        return {};
    }

    expected<string> AutomationUserDataRoot::resolve( bool bAllowOutside )
    {
        string root;
        if ( ProcessEnvironment::tryReadNonEmpty( kUserDataRootVariable, root ) )
        {
            if ( bAllowOutside )
                return root;
            if ( expected<void> under = checkUnderBase( root, getBaseFolder() ); under.has_value() == false )
                return unexpected( under.error() );
            return root;
        }
        if ( bAllowOutside )
            return string{}; // 사람이 고른 OS 사용자 폴더 그대로
        // 프로세스마다 새 폴더 — 같은 pid 의 옛 실행이 남긴 세이브를 읽지 않게 비우고, 끝난 프로세스의 폴더를 거둔다(사본 폴더와 같은 거두기).
        root = FileUtil::joinPath( getBaseFolder(), AutomationScratch::makeInstanceID( kRunFolderName ) );
        AutomationScratchWorkspace::resetInstanceFolder( root );
        if ( ProcessEnvironment::trySet( kUserDataRootVariable, root ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "could not set the user data root variable", root } );
        return root;
    }

    string AutomationUserDataRoot::makeChildRoot( string_view parentRoot, string_view backend )
    {
        return FileUtil::joinPath( parentRoot, backend );
    }

    bool AutomationUserDataRoot::isOutsideAllowed()
    {
        return gv_scenarioAllowOutsideUserData;
    }

    UserDataFolderStamp UserDataFolderStamp::take( string_view folder )
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
            if ( FileUtil::getFileWriteTime( entry._path, ticks ) && ticks > stamp._latestWriteTicks )
                stamp._latestWriteTicks = ticks;
            return true;
        } ); // 훑다 멈추면 그때까지의 수 — 다음에 찍을 때 달라져 바뀐 것으로 보인다(거짓 통과가 아니다)
        return stamp;
    }

    SubsystemInitResult AutomationUserDataIsolation::initializeSubsystem()
    {
        const CommandLineManager& commandLine = engine::getSubsystem<CommandLineManager>();
        if ( commandLine.isArgumentProvided( CommandLineArgument::SCENARIO ) == false )
            return SubsystemInitResult::Succeeded; // 시나리오 실행이 아니다 — 사람의 실행은 사람의 폴더를 쓴다
        _platformFolder       = UserDataPath::findPlatformDataDirectory( GameSelection::getName() );
        _platformStartStamp   = _platformFolder.empty() ? UserDataFolderStamp{} : UserDataFolderStamp::take( _platformFolder );
        expected<string> root = AutomationUserDataRoot::resolve( AutomationUserDataRoot::isOutsideAllowed() );
        if ( root.has_value() == false )
        {
            SW_LOG_ERROR_ID( "AutomationUserData.RootRejected", "[Scenario] %#: %#", root.error().message(), root.error().detail() );
            engine::getSubsystem<QuitRequest>().request( ProcessExitCode::LoadError ); // 시나리오를 시작할 수 없는 실행 — 초기화 실패(17)가 아니라 로드 오류(11)로 끝낸다(`App::getInitFailureExitCode`)
            return SubsystemInitResult::Failed;
        }
        _root = std::move( *root );
        SW_LOG_INFO_ID( "AutomationUserData.Root", "[Scenario] user data root: %#", _root.empty() ? "(the OS user folder - allowed on purpose)" : _root.c_str() );
        return SubsystemInitResult::Succeeded;
    }
} // namespace sw
