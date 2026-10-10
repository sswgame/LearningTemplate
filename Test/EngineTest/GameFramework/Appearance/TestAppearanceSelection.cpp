#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Network/BitStream.h"

#include "EngineTest/AppearanceTestFixture.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Base/Gameplay/Appearance/UserAppearancePresetStore.h"
#include "GameFramework/Base/Gameplay/Inventory/Equipment.h"

#include "TestFramework/TestFramework.h"

// 외형 선택 — 공유 코드(왕복 · 망가진 코드 거절), 콘텐츠가 바뀐 뒤 펼치기(지워진 값 · 아이템 · 잠금), 부분 프리셋, 입혀 보기,
// 플레이어 프리셋 세이브(즐겨찾기 · 썸네일 · 판 올리기), 네트워크 동기화(받는 쪽 해시가 같다).

using namespace sw;
using appearancetest::Fixture;

namespace
{
    struct AppearanceSelectionTestInternal
    {
        /** @brief 잠금 — 이름에 "cape" 가 든 아이템만 잠겨 있다. */
        class CapeLock final : public IAppearanceUnlockQuery
        {
        public:
            bool isItemUnlocked( const hashed_string& itemID ) const override { return itemID.view().find( "cape" ) == string_view::npos; }
        };

        /** @brief 꾸민 기사 하나 — 무작위 마을 사람 위에 귀 · 수염 · 칼 보석 · 피해 · 형상 변경 · 상태를 얹는다. */
        static void makeDecoratedSpec( const Fixture& fixture, CharacterAppearanceSpec& outSpec )
        {
            (void)fixture.expand( hashed_string( "Knight" ), 99u, outSpec );
            outSpec._customization.setNumber( hashed_string( "Ear.L" ), 0.3337f );
            outSpec._customization.setColor( hashed_string( "Skin" ), float4( 0.31f, 0.52f, 0.77f, 1.0f ) );
            outSpec._customization.setOption( hashed_string( "Beard" ), hashed_string( "Full" ) );
            outSpec._customization.setNumber( hashed_string( "BeardLength" ), 0.81234f );
            AppearanceSlotRequest* pMainHand = outSpec.findSlot( hashed_string( "MainHand" ) );
            pMainHand->_customization.setOption( hashed_string( "Finish" ), hashed_string( "Gold" ) );
            pMainHand->_customization.setColor( hashed_string( "Hilt" ), float4( 0.9f, 0.1f, 0.33f, 1.0f ) );
            pMainHand->_damage = 0.4567f;
            pMainHand->_state  = hashed_string( "Sheathed" );
            pMainHand->_listDetachedPart.push_back( hashed_string( "Tassel" ) );
            outSpec.findSlot( hashed_string( "Head" ) )->_visibleVisual = hashed_string( "cap" );
        }
    };
} // namespace

/**
 * @brief [AppearanceSelectionTest] 공유 코드는 왕복하면 같은 모습이고, 한 글자를 바꾸거나 판이 높거나 글자가 아니면 거절한다
 */
SW_TEST_CASE( AppearanceSelectionTest, ShareCodeRoundTripsAndRejectsCorruption )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec spec;
    AppearanceSelectionTestInternal::makeDecoratedSpec( fixture, spec );
    AppearanceSelection selection;
    AppearanceSelectionUtil::captureSelection( spec, selection );
    const string code = AppearanceShareCode::encode( selection, fixture._database );
    SW_EXPECT_TRUE( code.size() < 400u ); // 짧은 글자열

    AppearanceSelection decoded;
    string              reason;
    SW_ASSERT_TRUE_MSG( AppearanceShareCode::decode( code, fixture._database, decoded, &reason ), reason.c_str() );
    SW_EXPECT_TRUE( decoded._basePresetID == hashed_string( "Knight" ) && decoded._seed == 99u );
    SW_EXPECT_EQUAL( selection._listSlot.size(), decoded._listSlot.size() );

    CharacterAppearanceSpec   applied;
    AppearanceSelectionReport report;
    AppearanceSelectionUtil::applySelection( fixture._database, decoded, CharacterAppearanceSpec{}, nullptr, applied, report );
    SW_EXPECT_TRUE( report.isClean() );
    ResolvedAppearance original;
    ResolvedAppearance roundTrip;
    AppearanceResolver::resolve( fixture._database, spec, original );
    AppearanceResolver::resolve( fixture._database, applied, roundTrip );
    SW_EXPECT_EQUAL( original._hash, roundTrip._hash );

    // 한 글자 바꾸기 → 체크섬.
    string corrupted           = code;
    corrupted[code.size() / 2] = corrupted[code.size() / 2] == 'A' ? 'B' : 'A';
    SW_EXPECT_FALSE( AppearanceShareCode::decode( corrupted, fixture._database, decoded, &reason ) );
    SW_EXPECT_TRUE( reason == "checksum mismatch" );
    // 글자가 아니다.
    SW_EXPECT_FALSE( AppearanceShareCode::decode( "not a code!", fixture._database, decoded, &reason ) );
    SW_EXPECT_TRUE( reason == "not a share code" );
    // 잘린 코드.
    SW_EXPECT_FALSE( AppearanceShareCode::decode( code.substr( 0, code.size() / 3 ), fixture._database, decoded, &reason ) );
}

/**
 * @brief [AppearanceSelectionTest] 콘텐츠가 바뀐 뒤 — 지워진 매개변수 · 항목은 버리고 보고, 새 매개변수는 기본값, 지워진 · 잠긴 아이템은 칸 기본으로
 */
SW_TEST_CASE( AppearanceSelectionTest, ContentChangesAreToleratedAndReported )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    AppearanceSelection selection;
    selection._basePresetID = hashed_string( "Knight" );
    selection._schema       = hashed_string( "Human" );
    selection._customization.setNumber( hashed_string( "Wingspan" ), 0.5f );                // 지워진 매개변수
    selection._customization.setOption( hashed_string( "Hair" ), hashed_string( "Afro" ) ); // 지워진 항목
    selection._customization.setNumber( hashed_string( "Fat" ), 0.25f );
    AppearanceSlotRequest gone;
    gone._slot   = hashed_string( "Head" );
    gone._itemID = hashed_string( "laurel_wreath" ); // 지워진 아이템
    AppearanceSlotRequest locked;
    locked._slot   = hashed_string( "Back" );
    locked._itemID = hashed_string( "cape_hidden" );
    AppearanceSlotRequest tail;
    tail._slot          = hashed_string( "Tail" ); // 지워진 칸
    tail._itemID        = hashed_string( "sword" );
    selection._listSlot = { gone, locked, tail };

    // 공유 코드를 거쳐도 같은 보고가 나온다 — 모르는 해시는 자리 이름이 된다.
    AppearanceSelection decoded;
    SW_ASSERT_TRUE( AppearanceShareCode::decode( AppearanceShareCode::encode( selection, fixture._database ), fixture._database, decoded ) );

    const AppearanceSelectionTestInternal::CapeLock lock;
    for ( const AppearanceSelection* pSelection : { &selection, &decoded } )
    {
        CharacterAppearanceSpec   spec;
        AppearanceSelectionReport report;
        {
            SW_TEST_DEFENSIVE_SCOPE( "content removed since the selection was saved" );
            AppearanceSelectionUtil::applySelection( fixture._database, *pSelection, CharacterAppearanceSpec{}, &lock, spec, report );
        }
        SW_EXPECT_FALSE( report.isClean() );
        SW_ASSERT_EQUAL( size_t( 3 ), report._listSlotFallback.size() );
        SW_EXPECT_TRUE( spec.findSlot( hashed_string( "Head" ) )->_itemID == hashed_string( "helm" ) ); // 기준 프리셋(기사)의 칸 기본
        SW_EXPECT_TRUE( spec.findSlot( hashed_string( "Back" ) )->_itemID.empty() );
        SW_EXPECT_TRUE( report._listSlotFallback[1]._reason == AppearanceFallbackReason::LockedItem );
        SW_EXPECT_TRUE( report._listSlotFallback[2]._reason == AppearanceFallbackReason::UnknownSlot );
        SW_EXPECT_NEAR_EQUAL( 0.25f, spec._customization.findValue( hashed_string( "Fat" ) )->_number._x, 1.0e-4f );
        SW_EXPECT_TRUE( spec._customization.findValue( hashed_string( "Hair" ) ) == nullptr ); // 기본(Long)은 해석이 채운다
    }
    // 모르는 해시의 자리 이름.
    SW_EXPECT_TRUE( decoded._listSlot[0]._itemID.view().front() == '#' );
}

/**
 * @brief [AppearanceSelectionTest] 부분 프리셋은 그 묶음만 덮고(머리만), 입혀 보기는 아무것도 바꾸지 않는다
 */
SW_TEST_CASE( AppearanceSelectionTest, PartialPresetAndPreview )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec source;
    AppearanceSelectionTestInternal::makeDecoratedSpec( fixture, source );
    AppearanceSelection full;
    AppearanceSelectionUtil::captureSelection( source, full );
    AppearanceSelection hairOnly;
    AppearanceSelectionUtil::makePartial( full, fixture._database, { hashed_string( "Hair" ) }, hairOnly );
    SW_EXPECT_TRUE( hairOnly._listSlot.empty() );
    SW_EXPECT_TRUE( hairOnly._customization.findValue( hashed_string( "Beard" ) ) != nullptr );
    SW_EXPECT_TRUE( hairOnly._customization.findValue( hashed_string( "Ear.L" ) ) == nullptr );

    CharacterAppearanceSpec current;
    SW_ASSERT_TRUE( fixture.expand( hashed_string( "Villager" ), 5u, current ) );
    const CharacterAppearanceSpec before = current;
    ResolvedAppearance            preview;
    AppearanceSelectionReport     report;
    AppearanceSelectionUtil::previewSelection( fixture._database, hairOnly, current, nullptr, preview, report );
    SW_EXPECT_TRUE( preview.hasOwner( hashed_string( "Beard" ) ) );
    SW_EXPECT_TRUE( current._customization.isEquivalent( before._customization ) ); // 입혀 보기는 그대로
    ResolvedAppearance currentResolved;
    AppearanceResolver::resolve( fixture._database, current, currentResolved );
    SW_EXPECT_FALSE( currentResolved.hasOwner( hashed_string( "Beard" ) ) );

    CharacterAppearanceSpec applied;
    AppearanceSelectionUtil::applySelection( fixture._database, hairOnly, current, nullptr, applied, report );
    SW_EXPECT_TRUE( applied.findSlot( hashed_string( "Body" ) )->_itemID == current.findSlot( hashed_string( "Body" ) )->_itemID ); // 장비는 그대로
    SW_EXPECT_TRUE( applied._bodyShape == current._bodyShape );
    SW_EXPECT_TRUE( applied._customization.findValue( hashed_string( "Beard" ) )->_option == hashed_string( "Full" ) );

    // 부분 선택에 묶음 밖 값이 섞여 와도(손으로 만든 · 오래된 코드) 묶음 밖은 덮지 않는다.
    AppearanceSelection mixed = hairOnly;
    mixed._customization.setNumber( hashed_string( "Ear.L" ), 0.9f );
    AppearanceSelectionUtil::applySelection( fixture._database, mixed, current, nullptr, applied, report );
    SW_EXPECT_TRUE( applied._customization.findValue( hashed_string( "Ear.L" ) ) == nullptr );

    AppearanceSelection loadoutOnly;
    AppearanceSelectionUtil::makePartial( full, fixture._database, { hashed_string( AppearanceSelection::kLoadoutCategory ) }, loadoutOnly );
    AppearanceSelectionUtil::applySelection( fixture._database, loadoutOnly, current, nullptr, applied, report );
    SW_EXPECT_TRUE( applied.findSlot( hashed_string( "MainHand" ) )->_itemID == hashed_string( "sword" ) );
    SW_EXPECT_TRUE( applied._customization.findValue( hashed_string( "Beard" ) ) == nullptr ); // 머리 묶음은 그대로(마을 사람은 수염 값이 없다)
}

/**
 * @brief [AppearanceSelectionTest] 플레이어 프리셋 세이브 — 이름 칸 · 즐겨찾기 · 썸네일이 왕복하고, 판 1 세이브는 판 2 로 올라 읽히며, 새 판은 거절한다
 */
SW_TEST_CASE( AppearanceSelectionTest, UserPresetStoreRoundTripsAndUpgrades )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec spec;
    AppearanceSelectionTestInternal::makeDecoratedSpec( fixture, spec );
    AppearanceSelection selection;
    AppearanceSelectionUtil::captureSelection( spec, selection );

    UserAppearancePresetStore store;
    store.setDatabase( &fixture._database );
    SW_EXPECT_EQUAL( 0, store.savePreset( "Parade\nLook", selection ) );
    SW_EXPECT_EQUAL( 1, store.savePreset( "Hair only", AppearanceSelection{} ) );
    SW_EXPECT_EQUAL( 0, store.savePreset( "Parade Look", selection ) ); // 같은 이름은 바꾼다
    SW_EXPECT_TRUE( store.setFavorite( "Parade Look", true ) );
    SW_EXPECT_TRUE( store.setThumbnailPath( "Parade Look", "saves/thumbs/parade.png" ) );
    SW_EXPECT_FALSE( store.renamePreset( "Hair only", "Parade Look" ) );
    SW_EXPECT_TRUE( store.renamePreset( "Hair only", "Barber" ) );

    vector<uint8> bytes;
    SW_ASSERT_TRUE( store.writeBytes( bytes ) );
    UserAppearancePresetStore loaded;
    loaded.setDatabase( &fixture._database );
    SW_ASSERT_TRUE( loaded.readBytes( bytes.data(), bytes.size() ) );
    SW_ASSERT_EQUAL( size_t( 2 ), loaded.getPresets().size() );
    const UserAppearancePreset* pParade = loaded.findPresetByName( "Parade Look" );
    SW_ASSERT_NOT_NULL( pParade );
    SW_EXPECT_TRUE( pParade->_bFavorite == SW_TRUE && pParade->_thumbnailPath == "saves/thumbs/parade.png" );
    vector<const UserAppearancePreset*> listFavorite;
    loaded.collectFavorites( listFavorite );
    SW_EXPECT_EQUAL( size_t( 1 ), listFavorite.size() );
    CharacterAppearanceSpec   applied;
    AppearanceSelectionReport report;
    AppearanceSelectionUtil::applySelection( fixture._database, pParade->_selection, CharacterAppearanceSpec{}, nullptr, applied, report );
    ResolvedAppearance original;
    ResolvedAppearance reloaded;
    AppearanceResolver::resolve( fixture._database, spec, original );
    AppearanceResolver::resolve( fixture._database, applied, reloaded );
    SW_EXPECT_EQUAL( original._hash, reloaded._hash );

    // 판 1 — `version` · `count` · `favorites` 목록.
    const string code = AppearanceShareCode::encode( selection, fixture._database );
    string       version1;
    version1 += "version=1\ncount=2\n";
    version1 += "preset0.name=Old Parade\npreset0.code=" + code + "\n";
    version1 += "preset1.name=Old Barber\npreset1.code=" + code + "\n";
    version1 += "favorites=Old Barber\n";
    UserAppearancePresetStore upgraded;
    upgraded.setDatabase( &fixture._database );
    SW_ASSERT_TRUE( upgraded.loadFromText( version1 ) );
    SW_ASSERT_EQUAL( size_t( 2 ), upgraded.getPresets().size() );
    SW_EXPECT_TRUE( upgraded.findPresetByName( "Old Parade" )->_bFavorite == SW_FALSE );
    SW_EXPECT_TRUE( upgraded.findPresetByName( "Old Barber" )->_bFavorite == SW_TRUE );
    SW_EXPECT_TRUE( upgraded.saveToText().find( "formatVersion=2" ) != string::npos );

    // 지금보다 새 판은 읽지 않는다. 망가진 코드 줄은 건너뛴다.
    {
        SW_TEST_DEFENSIVE_SCOPE( "newer save format and a corrupted preset line" );
        SW_EXPECT_FALSE( upgraded.loadFromText( "formatVersion=3\npresetCount=0\n" ) );
        SW_ASSERT_TRUE( upgraded.loadFromText( "formatVersion=2\npresetCount=2\npreset0.name=Broken\npreset0.code=AAAA\npreset1.name=Good\npreset1.code=" + code + "\n" ) );
    }
    SW_EXPECT_EQUAL( size_t( 1 ), upgraded.getPresets().size() );
    SW_EXPECT_TRUE( upgraded.findPresetByName( "Good" ) != nullptr );
}

/**
 * @brief [AppearanceSelectionTest] 네트워크 동기화 — 보낸 쪽(프리셋 + 씨앗 + 장비 인스턴스 상태 + 꾸미기)을 비트로 보내 받은 쪽이 펼쳐 해석하면 해시가 같다
 */
SW_TEST_CASE( AppearanceSelectionTest, NetworkSyncResolvesToTheSameHash )
{
    Fixture fixture;
    SW_ASSERT_TRUE_MSG( fixture.load(), fixture._database.getReport().joined().c_str() );
    CharacterAppearanceSpec sender;
    AppearanceSelectionTestInternal::makeDecoratedSpec( fixture, sender );
    sender.findSlot( hashed_string( "MainHand" ) )->_state = hashed_string( "Drawn" ); // 술(떨어진 부품)이 보이는 상태
    // 장비가 프리셋을 덮는다 — 내구도가 닳은 칼 · 조건이 깨져 숨긴 망토.
    Equipment equipment;
    fixture.makeEquipment( equipment );
    vector<InventorySlot> listRemoved;
    InventorySlot         item;
    item._count                     = 1;
    const utf8* const arrPiece[][2] = {
        {"Head",    "helm"},
        {"Body",   "plate"},
        {"Legs", "greaves"}
    };
    for ( const auto& piece : arrPiece )
    {
        item._itemID = hashed_string( piece[1] );
        SW_ASSERT_TRUE( equipment.equip( hashed_string( piece[0] ), item, listRemoved ) == EquipResult::Ok );
    }
    item._itemID = hashed_string( "cape_hidden" );
    SW_ASSERT_TRUE( equipment.equip( hashed_string( "Back" ), item, listRemoved ) == EquipResult::Ok );
    SW_ASSERT_TRUE( equipment.unequip( hashed_string( "Legs" ), listRemoved ) == EquipResult::Ok );
    InventorySlot sword;
    sword._itemID           = hashed_string( "sword" );
    sword._count            = 1;
    sword._durability       = 37.0f;
    sword._customization    = sender.findSlot( hashed_string( "MainHand" ) )->_customization;
    sword._listDetachedPart = { hashed_string( "Tassel" ) };
    SW_ASSERT_TRUE( equipment.equip( hashed_string( "MainHand" ), sword, listRemoved ) == EquipResult::Ok );
    AppearanceInputUtil::applyEquipment( equipment, sender );
    SW_EXPECT_TRUE( sender.findSlot( hashed_string( "Back" ) )->_bSuppressed == SW_TRUE );
    SW_EXPECT_TRUE( sender.findSlot( hashed_string( "MainHand" ) )->_listDetachedPart.size() == 1u );

    ResolvedAppearance sent;
    AppearanceResolver::resolve( fixture._database, sender, sent );

    AppearanceSelection selection;
    AppearanceSelectionUtil::captureSelection( sender, selection );
    BitWriter writer;
    AppearanceSelectionCodec::writeSelection( writer, selection, fixture._database );
    const vector<uint8> bytes = writer.releaseBytes();
    SW_EXPECT_TRUE( bytes.size() < 256u );

    BitReader           reader( bytes.data(), static_cast<int32>( bytes.size() ) );
    AppearanceSelection received;
    SW_ASSERT_TRUE( AppearanceSelectionCodec::readSelection( reader, fixture._database, received ) );
    CharacterAppearanceSpec   receiver;
    AppearanceSelectionReport report;
    AppearanceSelectionUtil::applySelection( fixture._database, received, CharacterAppearanceSpec{}, nullptr, receiver, report );
    SW_EXPECT_TRUE( report.isClean() );
    ResolvedAppearance rebuilt;
    AppearanceResolver::resolve( fixture._database, receiver, rebuilt );
    SW_EXPECT_EQUAL( sent._hash, rebuilt._hash );
    SW_EXPECT_EQUAL( sent._meshHash, rebuilt._meshHash );
    SW_EXPECT_EQUAL( sent._listPart.size(), rebuilt._listPart.size() );

    // 잘린 패킷은 거절한다.
    BitReader           truncated( bytes.data(), static_cast<int32>( bytes.size() / 2 ) );
    AppearanceSelection ignored;
    SW_EXPECT_FALSE( AppearanceSelectionCodec::readSelection( truncated, fixture._database, ignored ) );
}
