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
