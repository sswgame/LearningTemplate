// EditorAssetIndexTest — 에셋 색인(Quick Open · 픽커 · 문서 열기)이 다른 프로세스의 시나리오 사본을 넣지 않는다(nogpu).
#include "pch.h"

#define SW_TEST_INTERNAL // 리소스 루트(엔진 서비스 `AssetPaths`)로 디스크 경로를 만든다

#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Asset/EditorAssetIndex.h"
#include "Editor/Common/Commands/Asset/EditorAssetIndexEntry.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Content/AssetPaths.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorAssetIndexTest] 다른 프로세스의 사본(`scratch/<이름>_p<다른 pid>/…`)은 색인에서 빠지고, 이 프로세스의 사본 · 원본은 들어간다.
 *        잔여 사본이 원본과 같은 파일 이름이면 Quick Open 이 사본을 먼저 집어 시퀀서가 씬 플레이어를 찾지 못했다(editor.sequencerkeys · sequencercuts).
 */
SW_TEST_CASE( EditorAssetIndexTest, SkipsOtherProcessScratchCopies )
{
    const AssetPaths& assetPaths = editor::getSubsystem<AssetPaths>();
    SW_REQUIRE_FALSE( assetPaths.getRootFolderPath().empty() );
    const auto makeDiskPath = [&assetPaths]( const string& assetID )
    { return FileUtil::joinPath( assetPaths.getRootFolderPath(), assetID ); };
    const string ownFolder   = "scratch/indexprobe_p" + to_string( Process::getCurrentProcessID() );
    const string otherFolder = "scratch/indexprobe_p" + to_string( Process::getCurrentProcessID() + 1 );

    EditorAssetIndexEntry entry{};
    SW_EXPECT_FALSE( EditorAssetIndex::classifyFile( assetPaths, makeDiskPath( otherFolder + "/probe.seq.xml" ), entry ) );
    SW_EXPECT_TRUE( EditorAssetIndex::classifyFile( assetPaths, makeDiskPath( ownFolder + "/probe.seq.xml" ), entry ) );
    SW_EXPECT_EQUAL( ownFolder + "/probe.seq.xml", entry._path );
    SW_EXPECT_TRUE( EditorAssetIndex::classifyFile( assetPaths, makeDiskPath( "engine/automation/editor/sequencerkeys/sequencerkeys.seq.xml" ), entry ) );
}
