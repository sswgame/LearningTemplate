#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Window/WindowFileDrop.h"

// WindowFileDropTest — X11 XDND · Wayland 가 주는 `text/uri-list` 를 로컬 경로로 바꾼다. 다른 호스트 · 스킴 · 깨진 `%` · NUL 은 버리고 그 수를 돌려준다(경로는 믿지 않는 바깥 입력이다).

/**
 * @brief [WindowFileDropTest] `file://` 줄만 경로가 되고 `%XX` 가 풀린다 — 주석 · 빈 줄은 건너뛰고 CRLF · LF 를 함께 받는다
 */
SW_TEST_CASE( WindowFileDropTest, URIListBecomesLocalPaths )
{
    sw::vector<sw::string> listPath;
    const uint32           rejected = sw::WindowFileDropUtil::parseURIList( "# comment\r\nfile:///home/me/My%20Textures/rock.png\r\n\r\nfile://localhost/tmp/a.glb\nfile:///tmp/%ED%95%9C.png", listPath );
    SW_EXPECT_EQUAL( 0u, rejected );
    SW_REQUIRE_EQUAL( size_t{ 3 }, listPath.size() );
    SW_EXPECT_TRUE( listPath[0] == "/home/me/My Textures/rock.png" );
    SW_EXPECT_TRUE( listPath[1] == "/tmp/a.glb" );
    SW_EXPECT_TRUE( listPath[2] == "/tmp/\xED\x95\x9C.png" ); // UTF-8 바이트가 그대로 풀린다
}

/**
 * @brief [WindowFileDropTest] 다른 호스트 · 다른 스킴 · 깨진 `%` · NUL 로 풀리는 줄은 버리고 버린 수를 센다
 */
SW_TEST_CASE( WindowFileDropTest, UntrustedLinesAreRejected )
{
    sw::vector<sw::string> listPath;
    const uint32           rejected = sw::WindowFileDropUtil::parseURIList(
        "https://example.com/a.png\nfile://otherhost/share/b.png\nfile:///tmp/c%2.png\nfile:///tmp/d%00.png\nfile:///tmp/%zz\nfile://\nfile:///tmp/ok.png\n", listPath );
    SW_EXPECT_EQUAL( 6u, rejected );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listPath.size() );
    SW_EXPECT_TRUE( listPath[0] == "/tmp/ok.png" );
}
