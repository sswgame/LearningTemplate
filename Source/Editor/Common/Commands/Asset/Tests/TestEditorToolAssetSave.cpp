#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"

#include "Engine/Sequencer/SequenceAsset.h"

using namespace sw;
using namespace sw::editor;

// 멀쩡한 문서는 저장되고, 읽어 보는 사본은 남지 않는다.
SW_TEST_CASE( EditorToolAssetSaveTest, GoodDocumentIsSaved )
{
    const string  path = test::makeScratchPath( "good.seq.xml" );
    SequenceAsset asset;
    asset._note = "written";
    SW_REQUIRE_TRUE( EditorToolAssetCommands::saveSequence( asset, path ) );
    SW_EXPECT_FALSE( FileUtil::exists( path + ".savecheck" ) );

    SequenceAsset read;
    SW_REQUIRE_TRUE( EditorToolAssetCommands::loadSequence( read, path ) == ToolAssetLoadResult::Loaded );
    SW_EXPECT_EQUAL( read._note, string( "written" ) );
}

// 결함: 저장은 값을 검사하지 않아, 다시 읽지 못할 문서가 멀쩡한 파일을 덮었다 - 도구 문서 다섯 종류가 지나는 한 길에서 막는다.
SW_TEST_CASE( EditorToolAssetSaveTest, DocumentThatCannotBeReadBackKeepsTheSavedFile )
{
    SW_EXPECT_LOG( "SequenceAsset.SequenceItemField" );
    SW_EXPECT_LOG( "EditorToolAssetCommands.WouldNotReadBack" );
    const string  path = test::makeScratchPath( "kept.seq.xml" );
    SequenceAsset good;
    good._note = "kept";
    SW_REQUIRE_TRUE( EditorToolAssetCommands::saveSequence( good, path ) );

    SequenceAsset     broken;
    SequenceTrackItem item{};
    item._kind = SequenceItemKind::Spawn; // 프리팹이 없어 다시 읽지 못한다
    broken._listItem.push_back( item );
    SW_EXPECT_FALSE( EditorToolAssetCommands::saveSequence( broken, path ) );
    SW_EXPECT_FALSE( FileUtil::exists( path + ".savecheck" ) );

    SequenceAsset read;
    SW_REQUIRE_TRUE( EditorToolAssetCommands::loadSequence( read, path ) == ToolAssetLoadResult::Loaded );
    SW_EXPECT_EQUAL( read._note, string( "kept" ) );
}
