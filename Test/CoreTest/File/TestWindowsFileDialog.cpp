/**
 * @file TestWindowsFileDialog.cpp
 * @brief Windows 파일 대화상자의 필터 글(`WindowsFileDialog::makeFilterText`) 시험입니다. 대화상자는 띄우지 않습니다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/Windows/WindowsFileDialog.h"

#include "TestFramework/TestFramework.h"

#if defined( SW_PLATFORM_WINDOWS )

/**
 * @brief [WindowsFileDialogTest] 확장자가 여럿이면 모두 한 필터에 `;` 로 들어가고, 글은 널 둘로 끝난다
 * @details 필터는 널이 구분자인 다중 문자열이다. 첫 널에서 끝나는 문자열에 지으면 설명 뒤가 모두 사라져 첫 항목만 남는다.
 */
SW_TEST_CASE( WindowsFileDialogTest, FilterHoldsEveryExtension )
{
    sw::FileDialogParams params;
    params._description         = "Images";
    params._listFilterExtension = { ".png", "jpg", "", ".dds" };

    const sw::wstring filterText = sw::WindowsFileDialog::makeFilterText( params );
    // 배열 길이로 담아 리터럴의 끝 널까지 넣는다 — 널 둘로 끝난다.
    constexpr utf16   kExpected[] = L"Images\0*.png;*.jpg;*.dds\0";
    const sw::wstring expected( kExpected, SW_COUNT_OF( kExpected ) );
    SW_EXPECT_EQUAL( expected.size(), filterText.size() );
    SW_EXPECT_TRUE( filterText == expected );
}

/**
 * @brief [WindowsFileDialogTest] 확장자가 없으면(또는 모두 비면) 설명 `All Files` 와 `*.*` 하나다
 */
SW_TEST_CASE( WindowsFileDialogTest, FilterFallsBackToAllFiles )
{
    sw::FileDialogParams params;
    constexpr utf16      kExpected[] = L"All Files\0*.*\0";
    const sw::wstring    expected( kExpected, SW_COUNT_OF( kExpected ) );
    SW_EXPECT_TRUE( sw::WindowsFileDialog::makeFilterText( params ) == expected );

    params._listFilterExtension = { "" };
    SW_EXPECT_TRUE( sw::WindowsFileDialog::makeFilterText( params ) == expected );
}

#endif
