#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/EditorNetPlayPresetStore.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 기본값과 칸마다 다른 구성 — 왕복이 모든 칸을 실어 나르는지 본다. */
    EditorNetPlaySettings makeChangedSettings()
    {
        EditorNetPlaySettings settings;
        settings._serverExecutablePath = "C:/Builds/Server.exe";
        settings._clientExecutablePath = "C:/Builds/App.exe";
        settings._clientArguments      = "-vk";
        settings._traceDirectory       = "C:/Traces/netplay";
        settings._stopTimeoutSeconds   = 30.0f;
        settings._clientCount          = 3;
        settings._windowWidth          = 800;
        settings._windowHeight         = 450;
        settings._windowColumnCount    = 3;
        settings._port                 = 7777;
        settings._mode                 = EditorNetMode::ListenHost;
        settings._bRecordTraces        = false;
        settings._bArrangeWindows      = false;
        return settings;
    }
} // namespace

/**
 * @brief [EditorNetPlayPresetStoreTest] 저장한 프리셋과 마지막으로 고른 이름이 파일을 거쳐 그대로 돌아온다 — 에디터를 다시 띄운 것과 같다
 */
SW_TEST_CASE( EditorNetPlayPresetStoreTest, PresetsAndSelectionRoundTripThroughTheFile )
{
    const string             filePath = FileUtil::joinPath( test::makeTempDirectory( "netplay_presets_round_trip" ), EditorNetPlayPresetStore::kFileName );
    EditorNetPlayPresetStore store;
    SW_ASSERT_TRUE( store.load( filePath ) == EditorUserFileResult::Missing );
    SW_EXPECT_TRUE( store.getPresets().empty() );

    const EditorNetPlaySettings changed = makeChangedSettings();
    SW_ASSERT_TRUE( store.savePreset( "Listen 3", changed ) );
    EditorNetPlaySettings dedicated;
    dedicated._mode = EditorNetMode::DedicatedServer;
    SW_ASSERT_TRUE( store.savePreset( "Server + 2", dedicated ) );
    SW_EXPECT_TRUE( store.getSelectedPresetName() == "Server + 2" ); // 저장한 것을 고른다
    SW_ASSERT_TRUE( store.selectPreset( "Listen 3" ) );

    EditorNetPlayPresetStore reloaded;
    SW_ASSERT_TRUE( reloaded.load( filePath ) == EditorUserFileResult::Loaded );
    SW_ASSERT_EQUAL( size_t{ 2 }, reloaded.getPresets().size() );
    SW_EXPECT_TRUE( reloaded.getSelectedPresetName() == "Listen 3" );
    const EditorNetPlaySettings* pSelected = reloaded.findSelectedPreset();
    SW_ASSERT_TRUE( pSelected != nullptr );
    SW_EXPECT_TRUE( pSelected->_serverExecutablePath == changed._serverExecutablePath );
    SW_EXPECT_TRUE( pSelected->_clientExecutablePath == changed._clientExecutablePath );
    SW_EXPECT_TRUE( pSelected->_clientArguments == changed._clientArguments );
    SW_EXPECT_TRUE( pSelected->_traceDirectory == changed._traceDirectory );
    SW_EXPECT_NEAR_EQUAL( changed._stopTimeoutSeconds, pSelected->_stopTimeoutSeconds, 1e-6f );
    SW_EXPECT_EQUAL( changed._clientCount, pSelected->_clientCount );
    SW_EXPECT_EQUAL( changed._windowWidth, pSelected->_windowWidth );
    SW_EXPECT_EQUAL( changed._windowHeight, pSelected->_windowHeight );
    SW_EXPECT_EQUAL( changed._windowColumnCount, pSelected->_windowColumnCount );
    SW_EXPECT_EQUAL( changed._port, pSelected->_port );
    SW_EXPECT_TRUE( pSelected->_mode == EditorNetMode::ListenHost );
    SW_EXPECT_FALSE( pSelected->_bRecordTraces );
    SW_EXPECT_FALSE( pSelected->_bArrangeWindows );
    const EditorNetPlaySettings* pDedicated = reloaded.findPreset( "Server + 2" );
    SW_ASSERT_TRUE( pDedicated != nullptr );
    SW_EXPECT_TRUE( pDedicated->_mode == EditorNetMode::DedicatedServer );
}

/**
 * @brief [EditorNetPlayPresetStoreTest] 같은 이름으로 저장하면 덮고, 고른 프리셋을 지우면 고른 것도 비고 파일에 남는다
 */
SW_TEST_CASE( EditorNetPlayPresetStoreTest, SaveOverwritesAndRemoveClearsTheSelection )
{
    const string             filePath = FileUtil::joinPath( test::makeTempDirectory( "netplay_presets_remove" ), EditorNetPlayPresetStore::kFileName );
    EditorNetPlayPresetStore store;
    SW_ASSERT_TRUE( store.load( filePath ) == EditorUserFileResult::Missing );
    EditorNetPlaySettings settings;
    settings._clientCount = 2;
    SW_ASSERT_TRUE( store.savePreset( "Pair", settings ) );
    settings._clientCount = 4;
    SW_ASSERT_TRUE( store.savePreset( "Pair", settings ) );
    SW_ASSERT_EQUAL( size_t{ 1 }, store.getPresets().size() );
    SW_EXPECT_EQUAL( 4, store.findPreset( "Pair" )->_clientCount );

    {
        SW_TEST_DEFENSIVE_SCOPE( "an unnamed or unknown preset is refused" );
        SW_EXPECT_FALSE( store.savePreset( "", settings ) ); // 이름 없는 프리셋은 없다
        SW_EXPECT_FALSE( store.selectPreset( "Nobody" ) );
        SW_EXPECT_FALSE( store.removePreset( "Nobody" ) );
    }
    SW_EXPECT_TRUE( store.getSelectedPresetName() == "Pair" );

    SW_ASSERT_TRUE( store.removePreset( "Pair" ) );
    SW_EXPECT_TRUE( store.getSelectedPresetName().empty() );
    EditorNetPlayPresetStore reloaded;
    SW_ASSERT_TRUE( reloaded.load( filePath ) == EditorUserFileResult::Loaded );
    SW_EXPECT_TRUE( reloaded.getPresets().empty() );
    SW_EXPECT_TRUE( reloaded.getSelectedPresetName().empty() );
}

/**
 * @brief [EditorNetPlayPresetStoreTest] 깨진 프리셋 파일은 `.corrupt` 로 치우고 빈 파일을 새로 쓴다 — 일부 값을 조용히 쓰지 않는다
 * @details JSON 이 아닌 것, 칸이 빠진 프리셋(조용히 코드 기본값을 넣지 않는다), 목록에 없는 고른 이름이 모두 같은 복구 경로다.
 */
SW_TEST_CASE( EditorNetPlayPresetStoreTest, BrokenFileIsMovedAsideAndRewritten )
{
    SW_TEST_DEFENSIVE_SCOPE( "a broken preset file is moved aside" );
    const string folder      = test::makeTempDirectory( "netplay_presets_broken" );
    const string filePath    = FileUtil::joinPath( folder, EditorNetPlayPresetStore::kFileName );
    const string corruptPath = filePath + EditorUserFile::kCorruptSuffix;

    // 형식이 맞는 파일 하나를 만들어 두고, 그 글을 고쳐 깨진 파일들을 만든다.
    {
        EditorNetPlayPresetStore writer;
        SW_ASSERT_TRUE( writer.load( filePath ) == EditorUserFileResult::Missing );
        SW_ASSERT_TRUE( writer.savePreset( "Pair", EditorNetPlaySettings{} ) );
    }
    string validText;
    SW_ASSERT_TRUE( FileUtil::readTextFile( filePath, validText ) );
    SW_ASSERT_TRUE( validText.find( "\"_clientCount\"" ) != string::npos );

    string       missingKeyText = validText;
    const size_t keyBegin       = missingKeyText.find( "\"_clientCount\"" );
    missingKeyText.replace( keyBegin, string( "\"_clientCount\"" ).size(), "\"_unknownKey\"" ); // 칸 하나가 빠지고 모르는 칸이 생긴다
    string       wrongSelectionText = validText;
    const size_t selectedBegin      = wrongSelectionText.find( "\"Pair\"", wrongSelectionText.find( "\"_selectedPresetName\"" ) );
    SW_ASSERT_TRUE( selectedBegin != string::npos );
    wrongSelectionText.replace( selectedBegin, string( "\"Pair\"" ).size(), "\"Nobody\"" );

    const string arrBrokenText[] = { "{ not json", missingKeyText, wrongSelectionText };
    for ( const string& brokenText : arrBrokenText )
    {
        (void)FileUtil::removeFile( corruptPath ); // 앞 차례의 사본 — 없어도 된다
        SW_ASSERT_TRUE( FileUtil::writeTextFile( filePath, brokenText ) );
        EditorNetPlayPresetStore store;
        SW_EXPECT_TRUE( store.load( filePath ) == EditorUserFileResult::Recovered );
        SW_EXPECT_TRUE( store.getPresets().empty() );
        SW_EXPECT_TRUE( FileUtil::isRegularFile( corruptPath ) );
        string movedText;
        SW_EXPECT_TRUE( FileUtil::readTextFile( corruptPath, movedText ) && movedText == brokenText );
        EditorNetPlayPresetStore rewritten;
        SW_EXPECT_TRUE( rewritten.load( filePath ) == EditorUserFileResult::Loaded );
    }
}

/**
 * @brief [EditorNetPlayPresetStoreTest] 파일을 열기 전에 바꾸면 실패한다 — 어디에도 쓰이지 않는 프리셋을 만들지 않는다
 */
SW_TEST_CASE( EditorNetPlayPresetStoreTest, ChangesBeforeLoadFail )
{
    SW_TEST_DEFENSIVE_SCOPE( "a preset change before load is refused" );
    EditorNetPlayPresetStore store;
    SW_EXPECT_FALSE( store.savePreset( "Pair", EditorNetPlaySettings{} ) );
}
