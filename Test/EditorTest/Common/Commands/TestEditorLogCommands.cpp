#include "pch.h"

#include "Editor/Common/Commands/EditorLogCommands.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorLogCommandsTest] 컴파일러 · 셰이더 오류 형식의 위치를 읽는다 — MSVC `경로(줄,열)` 과 clang · gcc `경로:줄:열`
 * @details 드라이브 문자의 콜론(`D:\`)은 숫자가 뒤따르지 않으므로 위치가 아니다. 확장자가 없는 이름(`frame:12`)도 위치가 아니다.
 */
SW_TEST_CASE( EditorLogCommandsTest, ParsesCompilerStyleLocations )
{
    EditorSourceLocation location;
    SW_ASSERT_TRUE( EditorLogCommands::parseSourceLocation( "D:\\Proj\\Source\\Foo.cpp(123,4): error C2065: 'x'", location ) );
    SW_EXPECT_STREQ( "D:\\Proj\\Source\\Foo.cpp", location._file.c_str() );
    SW_EXPECT_EQUAL( 123u, location._line );

    SW_ASSERT_TRUE( EditorLogCommands::parseSourceLocation( "shader failed: engine/shaders/forward.hlsl:42:7: error: undeclared", location ) );
    SW_EXPECT_STREQ( "engine/shaders/forward.hlsl", location._file.c_str() );
    SW_EXPECT_EQUAL( 42u, location._line );

    SW_ASSERT_TRUE( EditorLogCommands::parseSourceLocation( "see C:/work/a b/Bar.h(9)", location ) );
    SW_EXPECT_STREQ( "b/Bar.h", location._file.c_str() ); // 경로는 공백에서 끊긴다(따옴표 없는 공백 경로는 읽지 않는다)
    SW_EXPECT_EQUAL( 9u, location._line );

    SW_EXPECT_FALSE( EditorLogCommands::parseSourceLocation( "Loaded D:\\Proj\\x at frame:12", location ) );
    SW_EXPECT_FALSE( EditorLogCommands::parseSourceLocation( "value(3) and Foo.cpp(abc)", location ) );
    SW_EXPECT_FALSE( EditorLogCommands::parseSourceLocation( "Foo.cpp(0)", location ) );                // 줄은 1 부터
    SW_EXPECT_FALSE( EditorLogCommands::parseSourceLocation( "Foo.cpp(12 apples", location ) );         // 괄호가 닫히지 않았다
    SW_EXPECT_FALSE( EditorLogCommands::parseSourceLocation( "cache C:\\dir\\.hidden(4)", location ) ); // 이름 없이 확장자만
}

/**
 * @brief [EditorLogCommandsTest] 로그 줄은 메시지 안의 위치가 먼저, 없으면 로그를 쓴 자리다
 */
SW_TEST_CASE( EditorLogCommandsTest, EntryLocationPrefersTheMessage )
{
    EditorSourceLocation location;
    SW_ASSERT_TRUE( EditorLogCommands::findLogEntryLocation( "compile error at Mesh.hlsl(5)", "Engine/ShaderCompiler.cpp", 88, location ) );
    SW_EXPECT_STREQ( "Mesh.hlsl", location._file.c_str() );
    SW_EXPECT_EQUAL( 5u, location._line );

    SW_ASSERT_TRUE( EditorLogCommands::findLogEntryLocation( "scene loaded", "Engine/Scene.cpp", 31, location ) );
    SW_EXPECT_STREQ( "Engine/Scene.cpp", location._file.c_str() );
    SW_EXPECT_EQUAL( 31u, location._line );

    SW_EXPECT_FALSE( EditorLogCommands::findLogEntryLocation( "no location", "", 0, location ) );
}

/**
 * @brief [EditorLogCommandsTest] IDE 명령 틀의 {file} · {line} 을 모두 채운다
 */
SW_TEST_CASE( EditorLogCommandsTest, OpenCommandFillsThePlaceholders )
{
    EditorSourceLocation location;
    location._file = "D:\\Proj\\Foo.cpp";
    location._line = 77;
    SW_EXPECT_STREQ( "code -g \"D:\\Proj\\Foo.cpp:77\"", EditorLogCommands::makeOpenCommand( "code -g \"{file}:{line}\"", location ).c_str() );
    SW_EXPECT_STREQ( "rider --line 77 D:\\Proj\\Foo.cpp", EditorLogCommands::makeOpenCommand( "rider --line {line} {file}", location ).c_str() );
    SW_EXPECT_TRUE( EditorLogCommands::makeOpenCommand( "", location ).empty() );
}

/**
 * @brief [EditorLogCommandsTest] 숨긴 태그만 빠지고, 태그는 대소문자를 가리지 않으며, 바뀔 때마다 번호가 오른다
 */
SW_TEST_CASE( EditorLogCommandsTest, TagFilterHidesOnlyThoseTags )
{
    EditorLogTagFilter filter;
    SW_EXPECT_TRUE( filter.isTagVisible( "Renderer" ) );
    const uint32 revision = filter.getRevision();
    filter.setTagVisible( "Renderer", false );
    SW_EXPECT_TRUE( filter.getRevision() != revision );
    SW_EXPECT_FALSE( filter.isTagVisible( "renderer" ) );
    SW_EXPECT_TRUE( filter.isTagVisible( "Editor" ) );
    SW_EXPECT_EQUAL( 1u, filter.getHiddenCount() );

    filter.setTagVisible( "RENDERER", true );
    SW_EXPECT_TRUE( filter.isTagVisible( "Renderer" ) );
    filter.setTagVisible( "Audio", false );
    filter.showAll();
    SW_EXPECT_TRUE( filter.isTagVisible( "Audio" ) );
    SW_EXPECT_EQUAL( 0u, filter.getHiddenCount() );
}
