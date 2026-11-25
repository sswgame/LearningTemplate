#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/AssetWriter.h"
#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"

#include "Engine/Sequencer/SequenceAsset.h"

using namespace sw;
using namespace sw::editor;

// 멀쩡한 문서는 저장되고, 읽어 보는 사본은 남지 않는다.
SW_TEST_CASE( EditorToolAssetSaveTest, GoodDocumentIsSaved )
{
    const AbsolutePath path = test::makeScratchPath( "good.seq.xml" );
    AssetWriter        writer;
    SequenceAsset      asset;
    asset._note = "written";
    SW_REQUIRE_TRUE( EditorToolAssetCommands::saveSequence( writer, asset, path.str() ) );
    fixed_string<constant::buffer::kMax512> checkPath;
    formatstring( checkPath.data(), checkPath.capacity(), "%#.savecheck", path.str() );
    SW_EXPECT_FALSE( FileUtil::exists( FileUtil::toDiskPath( checkPath ) ) );

    SequenceAsset read;
    SW_REQUIRE_TRUE( EditorToolAssetCommands::loadSequence( read, path.str() ) == ToolAssetLoadResult::Loaded );
    SW_EXPECT_EQUAL( read._note, string( "written" ) );
}

// 결함: 저장은 값을 검사하지 않아, 다시 읽지 못할 문서가 멀쩡한 파일을 덮었다 - 도구 문서 다섯 종류가 지나는 한 길에서 막는다.
SW_TEST_CASE( EditorToolAssetSaveTest, DocumentThatCannotBeReadBackKeepsTheSavedFile )
{
    SW_EXPECT_LOG( "SequenceAsset.SequenceItemField" );
    SW_EXPECT_LOG( "EditorToolAssetCommands.WouldNotReadBack" );
    const AbsolutePath path = test::makeScratchPath( "kept.seq.xml" );
    AssetWriter        writer;
    SequenceAsset      good;
    good._note = "kept";
    SW_REQUIRE_TRUE( EditorToolAssetCommands::saveSequence( writer, good, path.str() ) );

    SequenceAsset     broken;
    SequenceTrackItem item{};
    item._kind = SequenceItemKind::Spawn; // 프리팹이 없어 다시 읽지 못한다
    broken._listItem.push_back( item );
    SW_EXPECT_FALSE( EditorToolAssetCommands::saveSequence( writer, broken, path.str() ) );
    fixed_string<constant::buffer::kMax512> checkPath;
    formatstring( checkPath.data(), checkPath.capacity(), "%#.savecheck", path.str() );
    SW_EXPECT_FALSE( FileUtil::exists( FileUtil::toDiskPath( checkPath ) ) );

    SequenceAsset read;
    SW_REQUIRE_TRUE( EditorToolAssetCommands::loadSequence( read, path.str() ) == ToolAssetLoadResult::Loaded );
    SW_EXPECT_EQUAL( read._note, string( "kept" ) );
}
