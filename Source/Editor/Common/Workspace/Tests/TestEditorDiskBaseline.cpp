// EditorDiskBaselineTest — 연 뒤 밖에서 바뀐 파일을 저장이 말없이 덮지 않는다: 첫 저장은 거절, 같은 내용에 대한 다음 저장은 통과, 저장 · 다시 읽기가 기준을 새로 적는다(nogpu).
#include "pch.h"

#include "Core/Container/span.h"
#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Workspace/Asset/EditorDiskBaseline.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"

using namespace sw;
using namespace sw::editor;

/** @brief [EditorDiskBaselineTest] 기준이 없거나 같은 파일은 막지 않고, 밖에서 바뀐 파일은 한 번 막는다. 알린 내용이 또 바뀌면 다시 막는다 */
SW_TEST_CASE( EditorDiskBaselineTest, OutsideChangeIsRefusedOncePerContent )
{
    const test::ScopedScratchDomain domain;
    const AbsolutePath              arrPath[] = { domain.makeDiskPath( "doc.xml" ) };
    EditorDiskBaseline              baseline;
    SW_EXPECT_TRUE( baseline.validateOverwrite( arrPath ) ); // 아직 없는 새 자리

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( arrPath[0], "opened" ) );
    baseline.capture( arrPath );
    SW_EXPECT_TRUE( baseline.validateOverwrite( arrPath ) );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( arrPath[0], "changed outside" ) );
    SW_EXPECT_LOG( "EditorDiskBaseline.ChangedOnDisk" );
    SW_EXPECT_FALSE( baseline.validateOverwrite( arrPath ) );
    SW_EXPECT_TRUE( baseline.validateOverwrite( arrPath ) ); // 알고 다시 저장한다

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( arrPath[0], "changed outside once more" ) );
    SW_EXPECT_FALSE( baseline.validateOverwrite( arrPath ) );

    baseline.capture( arrPath ); // 저장했거나 다시 읽었다
    SW_EXPECT_TRUE( baseline.validateOverwrite( arrPath ) );

    SW_REQUIRE_TRUE( FileUtil::removeFile( arrPath[0] ) );
    SW_EXPECT_TRUE( baseline.validateOverwrite( arrPath ) ); // 지워진 파일은 다시 만든다
}
