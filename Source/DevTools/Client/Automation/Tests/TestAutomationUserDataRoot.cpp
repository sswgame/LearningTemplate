#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/UserDataPath.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Client/Automation/Scenario/AutomationUserDataRoot.h"

// AutomationUserDataRootTest — 시나리오 실행의 사용자 데이터 뿌리가 자동화 폴더(`Saved/Automation/UserData`) 밖이면 세션 시작이 거절되는 규칙(`checkUnderBase` · `resolve`)과
// 사람의 폴더가 바뀌었는지 보는 도장(`UserDataFolderStamp`)(nogpu).

namespace
{
    /** @brief 시험 동안 `SW_USER_DATA_ROOT` 를 바꾸고 끝에 되돌립니다. */
    class ScopedUserDataRoot
    {
    public:
        ScopedUserDataRoot()
            : _bHadValue{ sw::ProcessEnvironment::tryRead( sw::kUserDataRootVariable, _previous ) ? SW_TRUE : SW_FALSE }
        {
        }
        ~ScopedUserDataRoot()
        {
            if ( _bHadValue )
                (void)sw::ProcessEnvironment::trySet( sw::kUserDataRootVariable, _previous ); // 소멸자에서는 알릴 길이 없다 — 읽은 이름이라 유효하다
            else
                (void)sw::ProcessEnvironment::tryUnset( sw::kUserDataRootVariable ); // 위와 같다
        }
        ScopedUserDataRoot( const ScopedUserDataRoot& )            = delete;
        ScopedUserDataRoot& operator=( const ScopedUserDataRoot& ) = delete;

    private:
        sw::string _previous;
        sw::uint8  _bHadValue : 1;
    };
} // namespace

/**
 * @brief [AutomationUserDataRootTest] 자동화 폴더 아래만 통과하고, 그 폴더 자신 · 이름이 겹치는 옆 폴더 · `..` 로 벗어나는 경로 · 상대 경로는 거절한다
 * @details 변이: 앞머리 비교에 `/` 를 빼면 `UserData2/x` 가, `..` 검사를 빼면 `UserData/../x` 가 통과해 이 시험이 진다.
 */
SW_TEST_CASE( AutomationUserDataRootTest, OnlyRootsUnderTheAutomationFolderPass )
{
    const sw::string base = sw::AutomationUserDataRoot::getBaseFolder();
    SW_EXPECT_TRUE( sw::AutomationUserDataRoot::checkUnderBase( base + "/run_p12", base ).has_value() );
    SW_EXPECT_TRUE( sw::AutomationUserDataRoot::checkUnderBase( base + "/run_p12/dx12", base ).has_value() );
    SW_EXPECT_TRUE( sw::AutomationUserDataRoot::checkUnderBase( base + "/sequence.velmora.carry/", base ).has_value() );

    SW_EXPECT_FALSE( sw::AutomationUserDataRoot::checkUnderBase( base, base ).has_value() );
    SW_EXPECT_FALSE( sw::AutomationUserDataRoot::checkUnderBase( base + "2/run_p12", base ).has_value() );
    SW_EXPECT_FALSE( sw::AutomationUserDataRoot::checkUnderBase( base + "/../../../escaped", base ).has_value() );
    SW_EXPECT_FALSE( sw::AutomationUserDataRoot::checkUnderBase( "Saved/Automation/UserData/run_p12", base ).has_value() );
    SW_EXPECT_FALSE( sw::AutomationUserDataRoot::checkUnderBase( sw::FileUtil::getDirectoryPart( base ) + "/Other", base ).has_value() );
}

/**
 * @brief [AutomationUserDataRootTest] 대소문자만 다른 폴더는 OS 규칙을 따른다 — Windows 는 같은 폴더라 통과, 리눅스는 다른(바깥) 폴더라 거절
 * @details 변이: 비교를 소문자로 접으면(예전 `normalizePath`) 리눅스에서 `.../userdata/run_p12` 가 통과해 이 시험이 진다.
 */
SW_TEST_CASE( AutomationUserDataRootTest, CaseOnlyDifferenceFollowsTheOSRule )
{
    const sw::string base       = sw::AutomationUserDataRoot::getBaseFolder(); // `.../Saved/Automation/UserData`
    const sw::string otherCased = sw::FileUtil::getDirectoryPart( base ) + "/userdata/run_p12";
#if defined( SW_PLATFORM_WINDOWS )
    SW_EXPECT_TRUE( sw::AutomationUserDataRoot::checkUnderBase( otherCased, base ).has_value() );
#elif defined( SW_PLATFORM_POSIX )
    SW_EXPECT_FALSE( sw::AutomationUserDataRoot::checkUnderBase( otherCased, base ).has_value() );
#else
    #error "Unsupported platform"
#endif
}

/**
 * @brief [AutomationUserDataRootTest] 상태 자리 변수가 사람의 폴더 자신이거나 그 아래면 거절하고, 자동화 폴더 · 이름만 겹치는 옆 폴더는 받는다. 백엔드별 부모는 상태 자리 인자를 찾아낸다
 * @details 변이: 폴더 자신 판정을 빼면 `Saved/Editor` 그대로가, 인자 이름 비교를 앞머리로 하면 `-gv_editorStateDirX` 가 걸려 이 시험이 진다.
 */
SW_TEST_CASE( AutomationUserDataRootTest, StatePathsMayNotPointAtThePersonFolders )
{
    const sw::string             base   = sw::AutomationUserDataRoot::getBaseFolder();
    const sw::string             person = sw::FileUtil::getDirectoryPart( base ) + "/PersonEditor";
    const sw::vector<sw::string> listPerson{ person };
    SW_EXPECT_FALSE( sw::AutomationUserDataRoot::checkStatePath( "gv_editorStateDir", person, listPerson ).has_value() );
    SW_EXPECT_FALSE( sw::AutomationUserDataRoot::checkStatePath( "gv_userSettingsFile", person + "/usersettings.json", listPerson ).has_value() );
    SW_EXPECT_TRUE( sw::AutomationUserDataRoot::checkStatePath( "gv_editorStateDir", person + "2", listPerson ).has_value() );
    SW_EXPECT_TRUE( sw::AutomationUserDataRoot::checkStatePath( "gv_editorStateDir", base + "/statedir.layout", listPerson ).has_value() );
    SW_EXPECT_FALSE( sw::AutomationUserDataRoot::makePersonFolders().empty() ); // 적어도 `Saved/Editor`

    const sw::optional<sw::string> shared = sw::AutomationUserDataRoot::findSharedStateArgument( { "-unattended", "-gv_editorStateDir=C:/x", "-gv_firstScene=a" } );
    SW_EXPECT_TRUE( shared.has_value() && *shared == "-gv_editorStateDir=C:/x" );
    SW_EXPECT_FALSE( sw::AutomationUserDataRoot::findSharedStateArgument( { "-unattended", "-gv_editorStateDirX=C:/x", "-gv_userSettingsFileName" } ).has_value() );
    SW_EXPECT_TRUE( sw::AutomationUserDataRoot::findSharedStateArgument( { "-gv_userSettingsFile=a.json" } ).has_value() );
}

/**
 * @brief [AutomationUserDataRootTest] 세션 시작 판정 — 환경 변수가 자동화 폴더 밖(사람의 OS 폴더)이면 거절하고, 명시 허락이 있을 때만 받는다. 안이면 그대로 쓴다
 * @details `AutomationUserDataIsolation` 이 이 판정으로 기동을 멈춘다(종료 11). 변이: 밖 뿌리를 받아 주면 첫 단언이 진다.
 */
SW_TEST_CASE( AutomationUserDataRootTest, SessionRejectsRootOutsideTheAutomationFolder )
{
    ScopedUserDataRoot restore;
    const sw::string   platformFolder = sw::UserDataPath::findPlatformDataDirectory( "" );
    const sw::string   outside        = platformFolder.empty() ? sw::FileUtil::getDirectoryPart( sw::AutomationUserDataRoot::getBaseFolder() ) + "/Other" : platformFolder;

    SW_REQUIRE_TRUE( sw::ProcessEnvironment::trySet( sw::kUserDataRootVariable, outside ) );
    SW_EXPECT_FALSE( sw::AutomationUserDataRoot::resolve( false ).has_value() );
    const sw::expected<sw::string> allowed = sw::AutomationUserDataRoot::resolve( true );
    SW_EXPECT_TRUE( allowed.has_value() && *allowed == outside );

    const sw::string inside = sw::AutomationUserDataRoot::getBaseFolder() + "/unit_test_root";
    SW_REQUIRE_TRUE( sw::ProcessEnvironment::trySet( sw::kUserDataRootVariable, inside ) );
    const sw::expected<sw::string> accepted = sw::AutomationUserDataRoot::resolve( false );
    SW_EXPECT_TRUE( accepted.has_value() && *accepted == inside );
    SW_EXPECT_TRUE( sw::AutomationUserDataRoot::makeChildRoot( inside, "vk" ) == inside + "/vk" );
}

/**
 * @brief [AutomationUserDataRootTest] 도장은 파일을 더하면 달라지고 그대로면 같다. 없는 폴더는 있는 폴더와 다르다
 */
SW_TEST_CASE( AutomationUserDataRootTest, FolderStampChangesWhenAFileIsAdded )
{
    const sw::string folder = sw::FileUtil::joinPath( sw::AutomationUserDataRoot::getBaseFolder(), "unit_test_stamp" );
    (void)sw::FileUtil::removeDirectory( folder );
    const sw::UserDataFolderStamp missing = sw::UserDataFolderStamp::take( folder );
    SW_EXPECT_FALSE( missing._bExists );

    SW_REQUIRE_TRUE( sw::FileUtil::ensureDirectoryExists( folder ) );
    const sw::UserDataFolderStamp empty = sw::UserDataFolderStamp::take( folder );
    SW_EXPECT_TRUE( empty != missing );
    SW_EXPECT_TRUE( sw::UserDataFolderStamp::take( folder ) == empty );

    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( folder, "save.dat" ), "x" ) );
    SW_EXPECT_TRUE( sw::UserDataFolderStamp::take( folder ) != empty );
    (void)sw::FileUtil::removeDirectory( folder );
}
