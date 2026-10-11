/**
 * @file TestWindowsFileDialogHost.cpp
 * @brief Windows 파일 대화상자(`WindowsFileDialog::open`)를 실제로 띄워 Win32 메시지로 조작합니다 — 고르기 · 취소.
 */
#include "pch.h"

#include "TestFramework/TestFramework.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include "Core/Common/PlatformOsHeaders.h"
    #include "Core/Container/StringUtil.h"
    #include "Core/File/FileUtil.h"
    #include "Core/File/Windows/WindowsFileDialog.h"

    #include <atomic>
    #include <thread>

// 대화상자는 데스크톱 세션이 있어야 뜬다 — CI 러너의 서비스 세션에서는 창을 찾지 못한다.
SW_TEST_REQUIRES_HOST( WindowsFileDialogHostTest, "opens a real common file dialog - needs an interactive desktop session" );

namespace
{
    struct WindowsFileDialogHostTestInternal
    {
        /** @brief 대화상자를 기다리는 시한(밀리초)입니다. */
        static constexpr DWORD kFindTimeoutMilliseconds = 10000;
        /** @brief 탐색기형 대화상자의 파일 이름 콤보 상자 id(`cmb13`)입니다. */
        static constexpr int32 kFileNameComboID = 0x047C;

        /** @brief 제목이 @p pTitle 인 대화상자(`#32770`)를 시한까지 찾습니다. */
        static HWND findDialog( const utf16* pTitle )
        {
            const ULONGLONG deadline = ::GetTickCount64() + kFindTimeoutMilliseconds;
            while ( ::GetTickCount64() < deadline )
            {
                HWND hDialog = ::FindWindowW( L"#32770", pTitle );
                if ( hDialog != nullptr && ::IsWindowVisible( hDialog ) )
                    return hDialog;
                ::Sleep( 20 );
            }
            return nullptr;
        }

        /** @brief @p hParent 아래(자손 포함)의 첫 `Edit` 컨트롤입니다. */
        static HWND findEdit( HWND hParent )
        {
            HWND hFound = nullptr;
            ::EnumChildWindows(
                hParent,
                []( HWND hChild, LPARAM lParam ) -> BOOL
            {
                utf16 arrClass[16]{};
                ::GetClassNameW( hChild, arrClass, static_cast<int32>( SW_COUNT_OF( arrClass ) ) );
                if ( ::lstrcmpiW( arrClass, L"Edit" ) != 0 )
                    return TRUE;
                *reinterpret_cast<HWND*>( lParam ) = hChild;
                return FALSE;
            },
                reinterpret_cast<LPARAM>( &hFound ) );
            return hFound;
        }

        /** @brief 단추 @p buttonID 를 누른 것처럼 대화상자에 알립니다. */
        static void pressButton( HWND hDialog, int32 buttonID )
        {
            ::PostMessageW( hDialog, WM_COMMAND, MAKEWPARAM( buttonID, BN_CLICKED ), reinterpret_cast<LPARAM>( ::GetDlgItem( hDialog, buttonID ) ) );
        }

        /**
         * @brief 대화상자 스레드가 끝날 때까지 기다립니다. 조작이 빗나가 대화상자가 남았으면 그 스레드의 창을 닫아 테스트가 멈추지 않게 합니다.
         */
        static void joinDialogThread( std::thread& dialogThread, const std::atomic<bool>& bDone )
        {
            const DWORD     threadID = ::GetThreadId( static_cast<HANDLE>( dialogThread.native_handle() ) );
            const ULONGLONG deadline = ::GetTickCount64() + kFindTimeoutMilliseconds;
            while ( bDone.load() == false )
            {
                if ( ::GetTickCount64() > deadline )
                {
                    ::EnumThreadWindows(
                        threadID,
                        []( HWND hWindow, LPARAM ) -> BOOL
                    {
                        ::PostMessageW( hWindow, WM_COMMAND, MAKEWPARAM( IDCANCEL, BN_CLICKED ), 0 );
                        return TRUE;
                    },
                        0 );
                }
                ::Sleep( 20 );
            }
            dialogThread.join();
        }
    };
} // namespace

/**
 * @brief [WindowsFileDialogHostTest] 열기 대화상자에서 파일 이름을 넣고 확인을 누르면 그 경로 하나가 돌아온다
 * @details 필터 글(`makeFilterText`)은 nogpu 테스트가 보지만 대화상자를 실제로 띄워 결과 버퍼를 읽는 길은 이 케이스만 지난다.
 */
SW_TEST_CASE( WindowsFileDialogHostTest, OpenReturnsTheChosenFile )
{
    using Internal              = WindowsFileDialogHostTestInternal;
    const sw::string folder     = test::makeTempDirectory( "file_dialog" );
    const sw::string chosenPath = sw::FileUtil::joinPath( folder, "chosen.txt" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( chosenPath, "probe" ) );

    sw::FileDialogParams params;
    params._type                = sw::FileDialogParams::Type::Open;
    params._title               = "SW File Dialog Probe Open";
    params._initialDirectory    = folder;
    params._listFilterExtension = { ".txt" };

    sw::vector<sw::string> listPath;
    bool                   bResult = false;
    std::atomic<bool>      bDone{ false };
    std::thread            dialogThread( [&]()
    {
        bResult = sw::WindowsFileDialog::open( params, listPath );
        bDone.store( true );
    } );

    const HWND hDialog = Internal::findDialog( L"SW File Dialog Probe Open" );
    SW_EXPECT_TRUE_MSG( hDialog != nullptr, "the file dialog never appeared" );
    if ( hDialog != nullptr )
    {
        const HWND hCombo = ::GetDlgItem( hDialog, Internal::kFileNameComboID );
        const HWND hEdit  = hCombo != nullptr ? Internal::findEdit( hCombo ) : nullptr;
        SW_EXPECT_TRUE_MSG( hEdit != nullptr, "the file name box was not found" );
        if ( hEdit != nullptr )
        {
            const sw::wstring nameW = sw::StringUtil::utf8ToUtf16( sw::FileUtil::toNativeSeparators( chosenPath ).c_str() );
            ::SendMessageW( hEdit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>( nameW.c_str() ) );
            Internal::pressButton( hDialog, IDOK );
        }
        else
        {
            Internal::pressButton( hDialog, IDCANCEL );
        }
    }
    Internal::joinDialogThread( dialogThread, bDone );

    SW_EXPECT_TRUE( bResult );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listPath.size() ) );
    SW_EXPECT_TRUE_MSG( sw::FileUtil::pathsEqualNormalized( listPath[0], chosenPath ), listPath[0].c_str() );
}

/**
 * @brief [WindowsFileDialogHostTest] 취소하면 false 이고 경로가 없다
 */
SW_TEST_CASE( WindowsFileDialogHostTest, CancelReturnsNothing )
{
    using Internal = WindowsFileDialogHostTestInternal;
    sw::FileDialogParams params;
    params._type  = sw::FileDialogParams::Type::Save;
    params._title = "SW File Dialog Probe Cancel";

    sw::vector<sw::string> listPath;
    bool                   bResult = true;
    std::atomic<bool>      bDone{ false };
    std::thread            dialogThread( [&]()
    {
        bResult = sw::WindowsFileDialog::open( params, listPath );
        bDone.store( true );
    } );

    const HWND hDialog = Internal::findDialog( L"SW File Dialog Probe Cancel" );
    SW_EXPECT_TRUE_MSG( hDialog != nullptr, "the file dialog never appeared" );
    if ( hDialog != nullptr )
        Internal::pressButton( hDialog, IDCANCEL );
    Internal::joinDialogThread( dialogThread, bDone );

    SW_EXPECT_FALSE( bResult );
    SW_EXPECT_TRUE( listPath.empty() );
}

#endif
