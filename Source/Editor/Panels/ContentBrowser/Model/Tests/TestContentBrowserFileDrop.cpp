#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserFileDrop.h"

using namespace sw;
using namespace sw::editor;

// ContentBrowserFileDropTest — OS 에서 놓은 파일(믿지 않는 경로)을 임포트 요청으로 바꾼다: 절대 경로 · `..` · 일반 파일 · 확장자 · 크기 · 개수 순서로 거르고, 받은 것만 임포트 길로 간다.

namespace
{
    struct ContentBrowserFileDropTestInternal
    {
        /** @brief 이 플랫폼의 절대 경로 하나(케이스 작업 폴더 아래 — 파일은 만들지 않는다)입니다. 판정은 디스크를 보지 않는다. */
        static string makeAbsolute( string_view fileName ) // joinPath 는 `..` 를 접을 수 있어 글로 잇는다
        {
            StringBuilder<constant::buffer::kMax512> pathBuilder;
            pathBuilder.appendFormat( "%#/%#", test::makeScratchPath( "filedrop" ).str(), fileName );
            return FileUtil::normalizeSeparators( pathBuilder.view() );
        }

        static ContentBrowserDroppedFile makeFile( string path, uint64 byteSize, bool bRegularFile )
        {
            ContentBrowserDroppedFile file{};
            file._path         = std::move( path );
            file._byteSize     = byteSize;
            file._bRegularFile = bRegularFile;
            return file;
        }
    };
} // namespace

/**
 * @brief [ContentBrowserFileDropTest] 받는 파일만 임포트 목록에 오고, 거른 파일은 사유와 함께 남는다(같은 경로는 한 번만)
 */
SW_TEST_CASE( ContentBrowserFileDropTest, DroppedFilesAreFilteredBeforeImport )
{
    using Internal = ContentBrowserFileDropTestInternal;
    ContentBrowserFileDropRules rules{};
    rules._listExtension = { ".png", ".txt" };
    rules._maxBytes      = 1024;
    rules._maxCount      = 8;

    const string                      texture = Internal::makeAbsolute( "rock.PNG" );
    vector<ContentBrowserDroppedFile> listFile{
        Internal::makeFile( texture, 100, true ),
        Internal::makeFile( texture, 100, true ),                                       // 같은 파일을 두 번 — 한 번만 받는다
        Internal::makeFile( "relative/notes.txt", 10, true ),                           // 작업 폴더 상대 경로
        Internal::makeFile( Internal::makeAbsolute( "a/../../secret.txt" ), 10, true ), // `..` 성분
        Internal::makeFile( Internal::makeAbsolute( "folder.png" ), 0, false ),         // 폴더 · 없는 파일
        Internal::makeFile( Internal::makeAbsolute( "tool.exe" ), 10, true ),           // 임포트 종류가 아니다
        Internal::makeFile( Internal::makeAbsolute( "huge.png" ), 4096, true ),         // 크기 한도
    };
    ContentBrowserFileDropPlan plan;
    ContentBrowserFileDropPlan::make( listFile, rules, plan );
    SW_REQUIRE_EQUAL( size_t{ 1 }, plan._listImport.size() );
    SW_EXPECT_TRUE( plan._listImport[0] == texture );
    SW_REQUIRE_EQUAL( size_t{ 5 }, plan._listRejected.size() );
    SW_EXPECT_TRUE( plan._listRejected[0]._reason == ContentBrowserFileDropRejection::NotAbsolute );
    SW_EXPECT_TRUE( plan._listRejected[1]._reason == ContentBrowserFileDropRejection::ParentReference );
    SW_EXPECT_TRUE( plan._listRejected[2]._reason == ContentBrowserFileDropRejection::NotRegularFile );
    SW_EXPECT_TRUE( plan._listRejected[3]._reason == ContentBrowserFileDropRejection::UnknownExtension );
    SW_EXPECT_TRUE( plan._listRejected[4]._reason == ContentBrowserFileDropRejection::TooLarge );
}

/**
 * @brief [ContentBrowserFileDropTest] 한 번에 받는 개수 한도에 닿으면 뒤의 파일은 `TooMany` 로 거른다 — 판정은 디스크를 보지 않는다
 */
SW_TEST_CASE( ContentBrowserFileDropTest, CountLimitRejectsTheRest )
{
    using Internal = ContentBrowserFileDropTestInternal;
    ContentBrowserFileDropRules rules{};
    rules._listExtension = { ".txt" };
    rules._maxBytes      = 1024;
    rules._maxCount      = 2;
    const vector<ContentBrowserDroppedFile> listFile{
        Internal::makeFile( Internal::makeAbsolute( "a.txt" ), 1, true ),
        Internal::makeFile( Internal::makeAbsolute( "b.txt" ), 1, true ),
        Internal::makeFile( Internal::makeAbsolute( "c.txt" ), 1, true ),
    };
    ContentBrowserFileDropPlan plan;
    ContentBrowserFileDropPlan::make( listFile, rules, plan );
    SW_EXPECT_EQUAL( size_t{ 2 }, plan._listImport.size() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, plan._listRejected.size() );
    SW_EXPECT_TRUE( plan._listRejected[0]._reason == ContentBrowserFileDropRejection::TooMany );
    SW_EXPECT_TRUE( string_view{ ContentBrowserFileDropPlan::describeRejection( ContentBrowserFileDropRejection::TooMany ) }.empty() == false );
}
