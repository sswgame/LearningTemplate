#include "pch.h"

#include "Core/Network/Transport/NetEmulation.h"

#include "Editor/Common/Commands/Play/EditorNetConditions.h"
#include "Editor/Panels/Multiplayer/MultiplayerTab.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 등록 테스트용 탭 — 그리지 않는다. */
    class TestMultiplayerTabA final : public IMultiplayerTab
    {
    public:
        void drawTab( const MultiplayerTabContext& context ) override { (void)context; }
    };

    /** @brief 등록 테스트용 둘째 탭입니다. */
    class TestMultiplayerTabB final : public IMultiplayerTab
    {
    public:
        void drawTab( const MultiplayerTabContext& context ) override { (void)context; }
    };

    // 순서 키가 등록 순서와 거꾸로다 — 정렬은 순서 키가 정한다.
    SW_EDITOR_MULTIPLAYER_TAB( TestMultiplayerTabA, "test_tab_late", 900100, "Late" );
    SW_EDITOR_MULTIPLAYER_TAB( TestMultiplayerTabB, "test_tab_early", 900000, "Early" );
} // namespace

/**
 * @brief [EditorNetConditionsTest] 패널 프리셋 단추는 모두 Core 프리셋 표에 있고, 패널 값으로 옮겨도 그 값이다(LAN · Wi-Fi · LTE · 나쁜 Wi-Fi · 끊김)
 */
SW_TEST_CASE( EditorNetConditionsTest, EveryPanelPresetIsACorePresetWithItsValues )
{
    for ( const EditorNetPresetRow& row : EditorNetConditionsUtil::kArrPresetRow )
    {
        NetEmulationConditions conditions;
        SW_ASSERT_TRUE_MSG( NetEmulationConditions::tryGetPreset( row._pName, conditions ), row._pName );
    }
    // 패널 값(ms · % · KB/s)의 표 — Core 표가 바뀌면 이 테스트가 같이 고쳐진다.
    struct ExpectedRow
    {
        const utf8* _pName;
        float32     _latencyMilliseconds;
        float32     _jitterMilliseconds;
        float32     _lossPercent;
        int32       _bandwidthKilobytesPerSecond;
    };
    constexpr ExpectedRow kArrExpected[] = {
        {         "lan",  1.0f,  0.0f,   0.0f,    0},
        {        "wifi", 10.0f,  5.0f,   0.5f,    0},
        {         "lte", 35.0f, 15.0f,   1.0f, 1024},
        {     "badwifi", 80.0f, 40.0f,   5.0f,  256},
        {"disconnected",  0.0f,  0.0f, 100.0f,    0},
    };
    for ( const ExpectedRow& expected : kArrExpected )
    {
        NetEmulationConditions conditions;
        SW_ASSERT_TRUE( NetEmulationConditions::tryGetPreset( expected._pName, conditions ) );
        const EditorNetLinkSettings settings = EditorNetConditionsUtil::makeLinkSettings( conditions );
        SW_EXPECT_NEAR_EQUAL( settings._latencyMilliseconds, expected._latencyMilliseconds, 1.0e-3f );
        SW_EXPECT_NEAR_EQUAL( settings._jitterMilliseconds, expected._jitterMilliseconds, 1.0e-3f );
        SW_EXPECT_NEAR_EQUAL( settings._lossPercent, expected._lossPercent, 1.0e-3f );
        SW_EXPECT_EQUAL( settings._bandwidthKilobytesPerSecond, expected._bandwidthKilobytesPerSecond );
    }
}

/**
 * @brief [EditorNetConditionsTest] 패널 값으로 만든 `net.emulation` 명령은 자식 프로세스가 읽는 덮어쓰기 글이고, 읽으면 같은 값이다
 */
SW_TEST_CASE( EditorNetConditionsTest, LinkCommandCarriesThePanelValues )
{
    EditorNetLinkSettings settings;
    settings._latencyMilliseconds         = 120.0f;
    settings._jitterMilliseconds          = 30.0f;
    settings._lossPercent                 = 30.0f;
    settings._bandwidthKilobytesPerSecond = 64;
    const string command                  = EditorNetConditionsUtil::makeLinkCommand( EditorNetLinkDirection::Up, settings );
    const string prefix                   = "net.emulation up ";
    SW_ASSERT_TRUE( command.rfind( prefix, 0 ) == 0 );

    NetEmulationConditions parsed;
    string                 error;
    SW_ASSERT_TRUE_MSG( NetEmulationConditions::parseOverrides( command.substr( prefix.size() ), parsed, error ), error.c_str() );
    const EditorNetLinkSettings back = EditorNetConditionsUtil::makeLinkSettings( parsed );
    SW_EXPECT_NEAR_EQUAL( back._latencyMilliseconds, 120.0f, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( back._jitterMilliseconds, 30.0f, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( back._lossPercent, 30.0f, 1.0e-3f );
    SW_EXPECT_EQUAL( back._bandwidthKilobytesPerSecond, 64 );

    SW_EXPECT_EQUAL( EditorNetConditionsUtil::makePresetCommand( "badwifi" ), string( "net.emulation both badwifi" ) );
    SW_EXPECT_EQUAL( EditorNetConditionsUtil::makeSpikeCommand( 2.5f ), string( "net.emulation spike 2.5" ) );
    SW_EXPECT_EQUAL( EditorNetConditionsUtil::makeLinkCommand( EditorNetLinkDirection::Down, EditorNetLinkSettings{} ).rfind( "net.emulation down latencyMs=0 ", 0 ),
                     size_t{ 0 } );
}

/**
 * @brief [EditorNetConditionsTest] 명령 대상 글(server · client<N> · clients · all · 번호)을 프로세스 구간으로 읽고, 없는 프로세스는 거절한다
 */
SW_TEST_CASE( EditorNetConditionsTest, TargetTextSelectsProcessRanges )
{
    int32 firstIndex = -1;
    int32 lastIndex  = -1;
    SW_ASSERT_TRUE( EditorNetConditionsUtil::tryParseTarget( "server", 3, firstIndex, lastIndex ) );
    SW_EXPECT_EQUAL( firstIndex, 0 );
    SW_EXPECT_EQUAL( lastIndex, 0 );
    SW_ASSERT_TRUE( EditorNetConditionsUtil::tryParseTarget( "client2", 3, firstIndex, lastIndex ) );
    SW_EXPECT_EQUAL( firstIndex, 2 );
    SW_EXPECT_EQUAL( lastIndex, 2 );
    SW_ASSERT_TRUE( EditorNetConditionsUtil::tryParseTarget( "clients", 3, firstIndex, lastIndex ) );
    SW_EXPECT_EQUAL( firstIndex, 1 );
    SW_EXPECT_EQUAL( lastIndex, 2 );
    SW_ASSERT_TRUE( EditorNetConditionsUtil::tryParseTarget( "all", 3, firstIndex, lastIndex ) );
    SW_EXPECT_EQUAL( firstIndex, 0 );
    SW_EXPECT_EQUAL( lastIndex, 2 );
    SW_ASSERT_TRUE( EditorNetConditionsUtil::tryParseTarget( "1", 3, firstIndex, lastIndex ) );
    SW_EXPECT_EQUAL( firstIndex, 1 );

    SW_EXPECT_FALSE( EditorNetConditionsUtil::tryParseTarget( "client3", 3, firstIndex, lastIndex ) );
    SW_EXPECT_FALSE( EditorNetConditionsUtil::tryParseTarget( "client0", 3, firstIndex, lastIndex ) );
    SW_EXPECT_FALSE( EditorNetConditionsUtil::tryParseTarget( "clients", 1, firstIndex, lastIndex ) ); // 서버만 있으면 클라이언트 구간이 비었다
    SW_EXPECT_FALSE( EditorNetConditionsUtil::tryParseTarget( "host", 3, firstIndex, lastIndex ) );
}

/**
 * @brief [MultiplayerTabTest] `SW_EDITOR_MULTIPLAYER_TAB` 한 줄이 탭 등록부에 오르고, 등록 순서가 아니라 순서 키로 줄 서고, 등록이 탭을 만든다
 * @details Multiplayer 창은 이 등록부만 읽어 탭 막대를 그린다(Session · Conditions 와 확장 모듈의 탭). 실제 에디터의 탭 목록은 `AppSmokeTest.EditorRegistriesKeepTheirOrder` 가 본다.
 */
SW_TEST_CASE( MultiplayerTabTest, RegisteredTabsSortByOrderAndCreateTheirTab )
{
    using Registry = EditorExtensionRegistry<EditorMultiplayerTabRegistration>;
    SW_EXPECT_STREQ( EditorMultiplayerTabRegistration::kKindName, "multiplayerTab" );
    const EditorMultiplayerTabRegistration* pEarly = Registry::find( "test_tab_early" );
    const EditorMultiplayerTabRegistration* pLate  = Registry::find( "test_tab_late" );
    SW_ASSERT_NOT_NULL( pEarly );
    SW_ASSERT_NOT_NULL( pLate );
    SW_EXPECT_STREQ( pEarly->_pLabel, "Early" );

    int32 earlyPosition = -1;
    int32 latePosition  = -1;
    for ( uint32 index = 0; index < Registry::getCount(); ++index )
    {
        const EditorMultiplayerTabRegistration& registration = Registry::getAt( index );
        if ( &registration == pEarly )
            earlyPosition = static_cast<int32>( index );
        if ( &registration == pLate )
            latePosition = static_cast<int32>( index );
    }
    SW_EXPECT_TRUE( 0 <= earlyPosition && earlyPosition < latePosition );

    const unique_ptr<IMultiplayerTab> pTab = pEarly->_pCreate();
    SW_EXPECT_NOT_NULL( pTab.get() );
}
