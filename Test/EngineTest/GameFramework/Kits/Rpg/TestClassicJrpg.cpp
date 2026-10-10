// 클래식 JRPG 키트 — 전직(레벨 1 · 능력치 절반 · 주문 유지), 여관 · 교회 · 상점 · 장비, 민첩 순 라운드 · 방어 우선 · 타이밍 공격/방어,
// 시전 잠금 깨기 · 취소 · 약화, 콤보 포인트 합동기 · 도망 확률, 무협 내공 · 비급 숙련 해금, 걸음 수 인카운터 · 보상 분배 · 결정성.
#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Input/TimingJudge.h"
#include "GameFramework/Base/Foundation/Framework/GameStateRefs.h"
#include "GameFramework/Base/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Kits/Genre/Rpg/ClassicJrpg/JrpgBattle.h"
#include "GameFramework/Kits/Genre/Rpg/ClassicJrpg/JrpgCatalog.h"
#include "GameFramework/Kits/Genre/Rpg/ClassicJrpg/JrpgEncounter.h"
#include "GameFramework/Kits/Genre/Rpg/ClassicJrpg/JrpgParty.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr const utf8* kJrpgCatalogXml = R"(
<JrpgCatalog>
  <ExperienceCurve base="10" exponent="1" linear="0" maxLevel="50"/>
  <Class id="hero" hp="30" mp="5" str="12" agi="8" vit="10" intellect="5" luck="5" growHp="6" growMp="2" growStr="3" growAgi="2" growVit="2" growIntellect="1" growLuck="1" attackType="Sword">
    <Learn level="3" spell="heal"/>
  </Class>
  <Class id="warrior" hp="35" mp="0" str="14" agi="5" vit="12" intellect="1" luck="3" growHp="7" growMp="0" growStr="4" growAgi="1" growVit="3" growIntellect="0" growLuck="1" attackType="Sword"/>
  <Class id="mage" hp="18" mp="15" str="4" agi="7" vit="5" intellect="14" luck="5" growHp="3" growMp="4" growStr="1" growAgi="2" growVit="1" growIntellect="3" growLuck="1" attackType="Blunt">
    <Learn level="1" spell="frizz"/><Learn level="5" spell="sizz"/>
  </Class>
  <Class id="sage" hp="25" mp="20" str="8" agi="8" vit="8" intellect="12" requires="book_of_satori"/>
  <Class id="sunblade" hp="40" mp="10" str="14" agi="10" vit="10" intellect="6" attackType="Sword"><Learn level="1" spell="sunball"/></Class>
  <Class id="moonstaff" hp="36" mp="12" str="10" agi="9" vit="9" intellect="8" attackType="Blunt"><Learn level="1" spell="moonerang"/></Class>
  <Class id="swordsman" hp="60" mp="0" str="12" agi="9" vit="12" intellect="4" attackType="Sword"/>
  <Spell id="frizz" kind="Damage" mp="2" power="12" type="Fire" target="One"/>
  <Spell id="sizz" kind="Damage" mp="5" power="10" type="Fire" target="All"/>
  <Spell id="heal" kind="Heal" mp="3" power="30" target="One"/>
  <Spell id="zing" kind="Revive" mp="10" target="One"/>
  <Spell id="sunball" kind="Damage" mp="3" power="15" type="Sun"/>
  <Spell id="moonerang" kind="Damage" mp="3" power="15" type="Moon"/>
  <Spell id="eclipse" kind="Damage" power="60" type="Moon"/>
  <Spell id="pine_cut" manual="pine_sword" inner="20" power="20" type="Sword" proficiency="15"/>
  <Spell id="pine_storm" manual="pine_sword" inner="40" power="40" type="Sword" target="All" proficiency="5"/>
  <Combo id="solstice" members="zale,valere" points="3" power="30" types="Sun,Moon" target="All"/>
  <Manual id="pine_sword"><Stage proficiency="0" technique="pine_cut"/><Stage proficiency="30" technique="pine_storm"/></Manual>
  <Enemy id="slime" hp="10" str="8" agi="3" vit="2" exp="6" gold="4" attackType="Blunt" weak="Fire"/>
  <Enemy id="golem" hp="300" str="20" agi="1" vit="20" exp="50" gold="30" attackType="Blunt" weak="Sun"/>
  <Enemy id="wyrd" hp="500" str="10" agi="2" vit="10" intellect="0" exp="100" gold="50" cast="eclipse" castTurns="2" castEvery="1" locks="Sword,Moon,Sword"/>
  <Enemy id="dweller" hp="999" str="6" agi="1" vit="10" exp="500" gold="300" boss="true"/>
  <Area id="field" rate="0.25" grace="3"><Group enemies="slime,slime" weight="3"/><Group enemies="golem" weight="1"/></Area>
</JrpgCatalog>
)";

    constexpr const utf8* kJrpgItemXml = R"(
<ItemCatalog>
  <Item id="copper_sword" category="Weapon" slot="Weapon" maxStack="1" value="100"><Stats attack="12"/></Item>
  <Item id="book_of_satori" category="Key" maxStack="1" value="0"/>
  <Item id="herb" category="Food" maxStack="99" value="8"/>
</ItemCatalog>
)";

    constexpr const utf8* kJrpgShopXml = R"(
<ShopCatalog><Shop id="aliahan" currency="Gold"><Stock item="copper_sword" price="100"/><Stock item="herb" price="8"/></Shop></ShopCatalog>
)";

    /** @brief 정해 둔 타이밍 — 공격 · 방어마다 눌렀는지와 어긋난 시간입니다. */
    class ScriptedTiming final : public IJrpgTimingInput
    {
    public:
        bool findPressOffset( JrpgTimingKind kind, int32 memberIndex, float32& outOffset ) const override
        {
            if ( _onlyMember >= 0 && memberIndex != _onlyMember )
                return false;
            const bool bPressed = kind == JrpgTimingKind::Attack ? _bAttackPressed : _bBlockPressed;
            outOffset           = kind == JrpgTimingKind::Attack ? _attackOffset : _blockOffset;
            return bPressed;
        }

        float32 _attackOffset{ 0.0f };
        float32 _blockOffset{ 0.0f };
        int32   _onlyMember{ -1 };
        bool    _bAttackPressed{ false };
        bool    _bBlockPressed{ false };
    };

    struct JrpgTestWorld
    {
        JrpgCatalog _catalog;
        ItemCatalog _itemCatalog;
        ShopCatalog _shopCatalog;
        TimingJudge _judge;
        Inventory   _inventory; ///< 플레이어 가방(파티가 빌린다)
        Wallet      _wallet;    ///< 지갑(파티가 빌린다)

        bool initialize()
        {
            vector<TimingWindow> listWindow;
            TimingWindow         window;
            window._grade      = hashed_string( "Perfect" );
            window._earlyWidth = 0.05f;
            window._lateWidth  = 0.08f;
            listWindow.push_back( window );
            _judge.setWindows( listWindow );
            return _catalog.loadFromXmlText( kJrpgCatalogXml, "ClassicJrpgTest" ) && _itemCatalog.loadFromXmlText( kJrpgItemXml, "ClassicJrpgTest" ) &&
                   _shopCatalog.loadFromXmlText( kJrpgShopXml, "ClassicJrpgTest" );
        }

        /** @brief 파티가 빌릴 공유 상태 — 부를 때마다 가방 · 지갑을 새로 엽니다. */
        GameStateRefs makeRefs()
        {
            _inventory.initialize( &_itemCatalog, 20 );
            _wallet.clear();
            GameStateRefs refs;
            refs._pInventory = &_inventory;
            refs._pWallet    = &_wallet;
            return refs;
        }
    };

    int32 countEvents( const vector<JrpgBattleEvent>& listEvent, JrpgBattleEvent::Kind kind, int32 bEnemyActor = -1, int32 actor = -1 )
    {
        int32 count = 0;
        for ( const JrpgBattleEvent& event : listEvent )
        {
            if ( event._kind != kind )
                continue;
            if ( bEnemyActor >= 0 && event._bEnemyActor != ( bEnemyActor != 0 ) )
                continue;
            if ( actor >= 0 && event._actor != actor )
                continue;
            ++count;
        }
        return count;
    }

    int32 findEventValue( const vector<JrpgBattleEvent>& listEvent, JrpgBattleEvent::Kind kind )
    {
        for ( const JrpgBattleEvent& event : listEvent )
        {
            if ( event._kind == kind )
                return event._value;
        }
        return -1;
    }

    /** @brief 상태 하나의 바이트입니다. */
    template <typename TState>
    vector<uint8> captureJrpgBytes( const TState& state )
    {
        Archive archive;
        state.writeState( archive );
        vector<uint8> bytes;
        archive.writeData( bytes );
        return bytes;
    }
} // namespace

SW_TEST_CASE( ClassicJrpgTest, ClassChangeHalvesStatsKeepsSpellsAndResetsLevel )
{
    JrpgTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );
    JrpgParty party;
    party.initialize( &world._catalog, &world._itemCatalog, world.makeRefs() );
    const int32 mage   = party.addMember( hashed_string( "maya" ), "Maya", hashed_string( "mage" ), 20 );
    const int32 novice = party.addMember( hashed_string( "nina" ), "Nina", hashed_string( "mage" ), 19 );
    SW_ASSERT_TRUE( mage == 0 && novice == 1 );
    SW_EXPECT_EQUAL( -1, party.addMember( hashed_string( "maya" ), "Dup", hashed_string( "mage" ), 1 ) ); // 같은 id
    SW_EXPECT_EQUAL( -1, party.addMember( hashed_string( "x" ), "X", hashed_string( "bard" ), 1 ) );      // 모르는 직업

    const JrpgClassDef* pMage = world._catalog.findClass( hashed_string( "mage" ) );
    SW_ASSERT_NOT_NULL( pMage );
    int32 arrBefore[kJrpgStatCount]{};
    for ( int32 index = 0; index < kJrpgStatCount; ++index )
    {
        arrBefore[index] = party.getMember( mage )._arrStat[index];
        SW_EXPECT_EQUAL( pMage->_arrBase[index] + 19 * pMage->_arrGrowth[index], arrBefore[index] );
    }
    SW_EXPECT_TRUE( party.getMember( mage ).knowsSpell( hashed_string( "frizz" ) ) && party.getMember( mage ).knowsSpell( hashed_string( "sizz" ) ) );

    SW_EXPECT_TRUE( party.changeClass( novice, hashed_string( "warrior" ) ) == JrpgClassChangeResult::LevelTooLow );
    SW_EXPECT_TRUE( party.changeClass( mage, hashed_string( "mage" ) ) == JrpgClassChangeResult::SameClass );
    SW_EXPECT_TRUE( party.changeClass( mage, hashed_string( "sage" ) ) == JrpgClassChangeResult::MissingItem );
    SW_EXPECT_TRUE( party.changeClass( mage, hashed_string( "ninja" ) ) == JrpgClassChangeResult::UnknownClass );

    SW_ASSERT_TRUE( party.changeClass( mage, hashed_string( "warrior" ) ) == JrpgClassChangeResult::Ok );
    const JrpgMember& changed = party.getMember( mage );
    SW_EXPECT_EQUAL( 1, changed._level.getLevel() );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( changed._level.getXp() ) );
    for ( int32 index = 0; index < kJrpgStatCount; ++index )
    {
        SW_EXPECT_EQUAL( arrBefore[index] / 2, changed._arrStat[index] ); // 절반을 지킨다(직업 레벨 1 능력치로 돌아가지 않는다)
    }
    SW_EXPECT_TRUE( changed._arrStat[static_cast<size_t>( JrpgStat::MaxHp )] != world._catalog.findClass( hashed_string( "warrior" ) )->_arrBase[0] );
    SW_EXPECT_TRUE( changed.knowsSpell( hashed_string( "frizz" ) ) ); // 배운 주문은 남는다(DQ3)
    SW_EXPECT_TRUE( party.canUseSpell( mage, hashed_string( "sizz" ) ) );
    SW_EXPECT_TRUE( changed._hp <= changed.getStat( JrpgStat::MaxHp ) && changed._hp >= 1 );

    // 새 직업의 성장으로 오른다 — 레벨 1 → 2 는 10, 2 → 3 은 20.
    const int32 strengthBefore = changed.getStat( JrpgStat::Strength );
    SW_EXPECT_EQUAL( 2, party.addExp( mage, 30 ) );
    SW_EXPECT_EQUAL( 3, party.getMember( mage )._level.getLevel() );
    SW_EXPECT_EQUAL( strengthBefore + 2 * 4, party.getMember( mage ).getStat( JrpgStat::Strength ) );

    // 깨달음의 책을 가지면 현자로.
    SW_EXPECT_EQUAL( 1, world._inventory.addItem( hashed_string( "book_of_satori" ), 1 ) );
    party.getMember( novice )._level.setLevel( world._catalog.getCurve(), 20 );
    SW_EXPECT_TRUE( party.changeClass( novice, hashed_string( "sage" ) ) == JrpgClassChangeResult::Ok );
    vector<JrpgPartyEvent> listEvent;
    party.drainEvents( listEvent );
    int32 classChanged = 0;
    for ( const JrpgPartyEvent& event : listEvent )
    {
        classChanged += event._kind == JrpgPartyEvent::Kind::ClassChanged ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 2, classChanged );
}

SW_TEST_CASE( ClassicJrpgTest, InnChurchShopAndEquipment )
{
    JrpgTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );
    JrpgParty party;
    party.initialize( &world._catalog, &world._itemCatalog, world.makeRefs() );
    (void)party.addMember( hashed_string( "hero" ), "Hero", hashed_string( "hero" ), 5 );
    (void)party.addMember( hashed_string( "sol" ), "Sol", hashed_string( "warrior" ), 4 );
    (void)party.addMember( hashed_string( "mia" ), "Mia", hashed_string( "mage" ), 3 );
    world._wallet.setBalance( Wallet::getDefaultCurrency(), 200 );

    // 상점(기반 ShopState) → 인벤토리 → 장비 → 공격력.
    ShopState shop;
    shop.initialize( &world._shopCatalog, &world._itemCatalog );
    const int32 attackBefore = party.computeAttack( 0 );
    SW_EXPECT_TRUE( shop.buy( hashed_string( "aliahan" ), hashed_string( "copper_sword" ), 1, world._wallet, world._inventory ) == ShopResult::Ok );
    SW_EXPECT_EQUAL( 100, static_cast<int32>( world._wallet.getBalance( Wallet::getDefaultCurrency() ) ) );
    const int32 inventorySlot = world._inventory.findFirstSlot( hashed_string( "copper_sword" ) );
    SW_ASSERT_TRUE( inventorySlot >= 0 );
    SW_EXPECT_TRUE( party.getMember( 0 )._equipment.equipFromInventory( world._inventory, inventorySlot ) == EquipResult::Ok );
    SW_EXPECT_EQUAL( attackBefore + 12, party.computeAttack( 0 ) );
    SW_EXPECT_EQUAL( 0, world._inventory.getItemCount( hashed_string( "copper_sword" ) ) );

    // 여관: 살아 있는 멤버 수 × 값 — 쓰러진 멤버는 그대로.
    party.getMember( 0 )._hp = 1;
    party.getMember( 0 )._mp = 0;
    party.getMember( 2 )._hp = 0;
    SW_EXPECT_TRUE( party.restAtInn( 10 ) );
    SW_EXPECT_EQUAL( 80, static_cast<int32>( world._wallet.getBalance( Wallet::getDefaultCurrency() ) ) );
    SW_EXPECT_EQUAL( party.getMember( 0 ).getStat( JrpgStat::MaxHp ), party.getMember( 0 )._hp );
    SW_EXPECT_EQUAL( party.getMember( 0 ).getStat( JrpgStat::MaxMp ), party.getMember( 0 )._mp );
    SW_EXPECT_EQUAL( 0, party.getMember( 2 )._hp );
    SW_EXPECT_FALSE( party.restAtInn( 100 ) ); // 200 이 모자라다 — 아무것도 바뀌지 않는다
    SW_EXPECT_EQUAL( 80, static_cast<int32>( world._wallet.getBalance( Wallet::getDefaultCurrency() ) ) );

    // 교회: 레벨 × 값.
    SW_EXPECT_FALSE( party.reviveAtChurch( 2, 30 ) ); // 90 > 80
    SW_EXPECT_EQUAL( 0, party.getMember( 2 )._hp );
    SW_EXPECT_TRUE( party.reviveAtChurch( 2, 20 ) );
    SW_EXPECT_EQUAL( 20, static_cast<int32>( world._wallet.getBalance( Wallet::getDefaultCurrency() ) ) );
    SW_EXPECT_EQUAL( party.getMember( 2 ).getStat( JrpgStat::MaxHp ), party.getMember( 2 )._hp );
    SW_EXPECT_FALSE( party.reviveAtChurch( 2, 0 ) ); // 이미 살아 있다
    SW_EXPECT_EQUAL( 3, party.countAlive() );
}

SW_TEST_CASE( ClassicJrpgTest, AgilityOrderDefendPriorityAndTimedAttackAndBlock )
{
    JrpgTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );
    ScriptedTiming timing;

    // 민첩 8 의 용사가 민첩 1 의 골렘보다 먼저, 타이밍을 맞히면 추가 타격 · 콤보 포인트 둘.
    const auto runRound = [&]( bool bAttackPressed, float32 attackOffset, bool bBlockPressed, JrpgCommandKind kind, vector<JrpgBattleEvent>& outListEvent,
                               int32& outHeroHp )
    {
        JrpgParty party;
        party.initialize( &world._catalog, &world._itemCatalog, world.makeRefs() );
        (void)party.addMember( hashed_string( "hero" ), "Hero", hashed_string( "hero" ), 1 );
        timing._bAttackPressed = bAttackPressed;
        timing._attackOffset   = attackOffset;
        timing._bBlockPressed  = bBlockPressed;
        timing._blockOffset    = 0.01f;
        JrpgBattle battle;
        battle.initialize( &world._catalog, &world._judge, JrpgBattleSettings{}, 99 );
        battle.setTimingInput( &timing );
        if ( battle.start( &party, { hashed_string( "golem" ) } ) == false )
            return 0;
        if ( battle.setCommand( 0, kind == JrpgCommandKind::Defend ? JrpgCommand::makeDefend() : JrpgCommand::makeAttack( 0 ) ) == false )
            return 0;
        battle.resolveRound();
        outListEvent.clear();
        battle.drainEvents( outListEvent );
        outHeroHp = party.getMember( 0 )._hp;
        return battle.getComboPoints();
    };

    vector<JrpgBattleEvent> listPlain;
    vector<JrpgBattleEvent> listTimed;
    vector<JrpgBattleEvent> listLate;
    int32                   hpPlain = 0;
    int32                   hpTimed = 0;
    int32                   hpLate  = 0;
    SW_EXPECT_EQUAL( 1, runRound( false, 0.0f, false, JrpgCommandKind::Attack, listPlain, hpPlain ) );
    SW_EXPECT_EQUAL( 2, runRound( true, 0.02f, false, JrpgCommandKind::Attack, listTimed, hpTimed ) );
    SW_EXPECT_EQUAL( 1, runRound( true, 0.2f, false, JrpgCommandKind::Attack, listLate, hpLate ) ); // 창 밖
    SW_ASSERT_TRUE( listPlain.empty() == false );
    SW_EXPECT_TRUE( listPlain.front()._kind == JrpgBattleEvent::Kind::Attack && listPlain.front()._bEnemyActor == false ); // 빠른 쪽 먼저
    SW_EXPECT_EQUAL( 1, countEvents( listPlain, JrpgBattleEvent::Kind::Damage, 0 ) );
    SW_EXPECT_EQUAL( 1, countEvents( listTimed, JrpgBattleEvent::Kind::TimedHit ) );
    SW_EXPECT_EQUAL( 2, countEvents( listTimed, JrpgBattleEvent::Kind::Damage, 0 ) );
    SW_EXPECT_EQUAL( 0, countEvents( listLate, JrpgBattleEvent::Kind::TimedHit ) );

    // 타이밍 방어: 같은 씨앗 · 같은 공격이면 받는 피해가 절반(내림)이다.
    vector<JrpgBattleEvent> listBlocked;
    int32                   hpBlocked = 0;
    (void)runRound( false, 0.0f, true, JrpgCommandKind::Attack, listBlocked, hpBlocked );
    const int32 heroMax = 30;
    SW_EXPECT_EQUAL( 1, countEvents( listBlocked, JrpgBattleEvent::Kind::TimedBlock ) );
    SW_EXPECT_EQUAL( ( heroMax - hpPlain ) / 2, heroMax - hpBlocked );
    SW_EXPECT_TRUE( heroMax - hpPlain > 1 );

    // 방어 명령은 먼저 움직이고(우선도) 피해를 절반으로 — 타이밍 방어와 겹치면 둘 다 곱한다.
    vector<JrpgBattleEvent> listDefend;
    int32                   hpDefend = 0;
    (void)runRound( false, 0.0f, false, JrpgCommandKind::Defend, listDefend, hpDefend );
    SW_ASSERT_TRUE( listDefend.empty() == false );
    SW_EXPECT_TRUE( listDefend.front()._kind == JrpgBattleEvent::Kind::Defending );
    // 골렘 → 용사: 기본 20 / 2 − 5 / 4 = 9, 변동 뒤 7..10. 방어하면 3..5, 타이밍 방어까지 겹치면 1..2.
    SW_EXPECT_TRUE( heroMax - hpPlain >= 7 && heroMax - hpPlain <= 10 );
    SW_EXPECT_TRUE( heroMax - hpDefend >= 3 && heroMax - hpDefend <= 5 );
    vector<JrpgBattleEvent> listBoth;
    int32                   hpBoth = 0;
    (void)runRound( false, 0.0f, true, JrpgCommandKind::Defend, listBoth, hpBoth );
    SW_EXPECT_TRUE( heroMax - hpBoth >= 1 && heroMax - hpBoth <= 2 );

    // 물리 피해 공식(DQ): 공격 40 · 방어 20 → (20 − 5) × 0.875..1.125, 기본이 1 미만이면 0 또는 1.
    GameRandom random( 3 );
    for ( int32 trial = 0; trial < 200; ++trial )
    {
        const int32 damage = JrpgBattle::computePhysicalDamage( 40, 20, random );
        SW_EXPECT_TRUE( damage >= 13 && damage <= 16 );
        const int32 chip = JrpgBattle::computePhysicalDamage( 2, 40, random );
        SW_EXPECT_TRUE( chip == 0 || chip == 1 );
    }
}

SW_TEST_CASE( ClassicJrpgTest, CastingEnemyLocksBreakCancelAndWeaken )
{
    JrpgTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );

    // 시전 적(민첩 2): 1 라운드 끝에 시전 시작(잠금 Sword · Moon · Sword), 3 라운드 제 차례에 터진다.
    // plan[라운드][멤버] — 'A' 공격(zale = Sword) · 'M' 문어랭(Moon) · 'D' 방어.
    const auto runCast = [&]( const utf8* pRound2, const utf8* pRound3, vector<JrpgBattleEvent>& outListEvent ) -> int32
    {
        JrpgParty party;
        party.initialize( &world._catalog, &world._itemCatalog, world.makeRefs() );
        (void)party.addMember( hashed_string( "zale" ), "Zale", hashed_string( "sunblade" ), 10 );
        (void)party.addMember( hashed_string( "valere" ), "Valere", hashed_string( "moonstaff" ), 10 );
        JrpgBattle battle;
        battle.initialize( &world._catalog, &world._judge, JrpgBattleSettings{}, 17 );
        if ( battle.start( &party, { hashed_string( "wyrd" ) } ) == false )
            return -1;
        const utf8* arrPlan[3] = { "DD", pRound2, pRound3 };
        outListEvent.clear();
        vector<JrpgBattleEvent> listRound;
        for ( const utf8* pPlan : arrPlan )
        {
            for ( int32 memberIndex = 0; memberIndex < 2; ++memberIndex )
            {
                const utf8  code    = pPlan[memberIndex];
                JrpgCommand command = JrpgCommand::makeDefend();
                if ( code == 'A' )
                    command = JrpgCommand::makeAttack( 0 );
                else if ( code == 'M' )
                    command = JrpgCommand::makeSpell( hashed_string( "moonerang" ), 0 );
                if ( battle.setCommand( memberIndex, command ) == false )
                    return -1;
            }
            battle.resolveRound();
            listRound.clear();
            battle.drainEvents( listRound );
            outListEvent.insert( outListEvent.end(), listRound.begin(), listRound.end() );
        }
        return findEventValue( outListEvent, JrpgBattleEvent::Kind::CastReleased );
    };

    vector<JrpgBattleEvent> listEvent;
    // 잠금을 하나도 깨지 않으면 온 위력(60)으로 터진다.
    SW_EXPECT_EQUAL( 60, runCast( "DD", "DD", listEvent ) );
    SW_EXPECT_EQUAL( 3, findEventValue( listEvent, JrpgBattleEvent::Kind::CastStarted ) );
    SW_EXPECT_TRUE( countEvents( listEvent, JrpgBattleEvent::Kind::Damage, 1 ) >= 2 ); // 모두에게
    // 하나 깨면 60 × (2 + 1) / (3 + 1) = 45.
    SW_EXPECT_EQUAL( 45, runCast( "AD", "DD", listEvent ) );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, JrpgBattleEvent::Kind::LockBroken ) );
    // Sword 둘을 깨면 60 × (1 + 1) / 4 = 30. 맞지 않는 유형(valere 의 기본 공격 = Blunt)은 아무 잠금도 깨지 못한다.
    SW_EXPECT_EQUAL( 30, runCast( "AD", "AD", listEvent ) );
    SW_EXPECT_EQUAL( 60, runCast( "DA", "DA", listEvent ) );
    SW_EXPECT_EQUAL( 0, countEvents( listEvent, JrpgBattleEvent::Kind::LockBroken ) );
    // 모두 깨면 취소 — 터지지 않는다.
    SW_EXPECT_EQUAL( -1, runCast( "AM", "AD", listEvent ) );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, JrpgBattleEvent::Kind::CastCancelled ) );
    SW_EXPECT_EQUAL( 3, countEvents( listEvent, JrpgBattleEvent::Kind::LockBroken ) );
}

SW_TEST_CASE( ClassicJrpgTest, ComboPointsJointTechniqueAndFleeChance )
{
    JrpgTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );
    JrpgParty party;
    party.initialize( &world._catalog, &world._itemCatalog, world.makeRefs() );
    (void)party.addMember( hashed_string( "zale" ), "Zale", hashed_string( "sunblade" ), 10 );
    (void)party.addMember( hashed_string( "valere" ), "Valere", hashed_string( "moonstaff" ), 10 );
    ScriptedTiming timing;
    timing._onlyMember     = 0;
    timing._bAttackPressed = false;
    JrpgBattle battle;
    battle.initialize( &world._catalog, &world._judge, JrpgBattleSettings{}, 23 );
    battle.setTimingInput( &timing );
    SW_ASSERT_TRUE( battle.start( &party, { hashed_string( "golem" ) } ) );

    SW_EXPECT_FALSE( battle.setCommand( 0, JrpgCommand::makeCombo( hashed_string( "solstice" ), 0 ) ) ); // 포인트 0
    SW_ASSERT_TRUE( battle.setCommand( 0, JrpgCommand::makeAttack( 0 ) ) );
    SW_ASSERT_TRUE( battle.setCommand( 1, JrpgCommand::makeAttack( 0 ) ) );
    battle.resolveRound();
    SW_EXPECT_EQUAL( 2, battle.getComboPoints() );
    SW_EXPECT_FALSE( battle.setCommand( 0, JrpgCommand::makeCombo( hashed_string( "solstice" ), 0 ) ) ); // 2 < 3
    timing._bAttackPressed = true;                                                                       // zale 만 타이밍을 맞힌다 → +2
    SW_ASSERT_TRUE( battle.setCommand( 0, JrpgCommand::makeAttack( 0 ) ) );
    SW_ASSERT_TRUE( battle.setCommand( 1, JrpgCommand::makeAttack( 0 ) ) );
    battle.resolveRound();
    SW_EXPECT_EQUAL( 5, battle.getComboPoints() );

    // 합동기 — 참여한 valere 의 이번 라운드 행동도 쓴다(따로 공격하지 않는다). 약점(Sun)이라 1.5 배.
    vector<JrpgBattleEvent> listEvent;
    battle.drainEvents( listEvent );
    SW_ASSERT_TRUE( battle.setCommand( 1, JrpgCommand::makeAttack( 0 ) ) );
    SW_ASSERT_TRUE( battle.setCommand( 0, JrpgCommand::makeCombo( hashed_string( "solstice" ), 0 ) ) );
    const int32 golemHp = battle.getEnemies()[0]._hp;
    battle.resolveRound();
    listEvent.clear();
    battle.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, JrpgBattleEvent::Kind::ComboUsed ) );
    SW_EXPECT_EQUAL( 0, countEvents( listEvent, JrpgBattleEvent::Kind::Attack, 0, 1 ) );
    SW_EXPECT_EQUAL( 2, battle.getComboPoints() );
    const int32 comboBase = 30 + ( party.computeAttack( 0 ) + party.computeAttack( 1 ) ) / 2;
    const int32 dealt     = golemHp - battle.getEnemies()[0]._hp;
    SW_EXPECT_TRUE( dealt >= comboBase * 900 / 1000 * 3 / 2 - 1 && dealt <= comboBase * 1100 / 1000 * 3 / 2 + 1 );
    // 참여 멤버가 쓰러지면 쓸 수 없다.
    const int32 valereHp     = party.getMember( 1 )._hp;
    party.getMember( 1 )._hp = 0;
    SW_EXPECT_FALSE( battle.setCommand( 0, JrpgCommand::makeCombo( hashed_string( "solstice" ), 0 ) ) );
    party.getMember( 1 )._hp = valereHp;

    // 도망: 0.5 + (평균 민첩 차) × 0.02 + 실패 × 0.1. 보스 앞에서는 0 이고 실패하면 적만 행동한다.
    JrpgParty runners;
    runners.initialize( &world._catalog, &world._itemCatalog, world.makeRefs() );
    (void)runners.addMember( hashed_string( "zale" ), "Zale", hashed_string( "sunblade" ), 1 );
    JrpgBattle bossBattle;
    bossBattle.initialize( &world._catalog, &world._judge, JrpgBattleSettings{}, 5 );
    SW_ASSERT_TRUE( bossBattle.start( &runners, { hashed_string( "dweller" ) } ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, bossBattle.computeFleeChance(), 1.0e-6f );
    SW_EXPECT_FALSE( bossBattle.tryFlee() );
    listEvent.clear();
    bossBattle.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countEvents( listEvent, JrpgBattleEvent::Kind::Attack, 1 ) );
    SW_EXPECT_EQUAL( 0, countEvents( listEvent, JrpgBattleEvent::Kind::Attack, 0 ) );

    const auto runFlee = [&]( uint32 seed, float32& outFirstChance ) -> int32
    {
        JrpgParty fleeParty;
        fleeParty.initialize( &world._catalog, &world._itemCatalog, world.makeRefs() );
        (void)fleeParty.addMember( hashed_string( "zale" ), "Zale", hashed_string( "sunblade" ), 1 );
        JrpgBattle fleeBattle;
        fleeBattle.initialize( &world._catalog, &world._judge, JrpgBattleSettings{}, seed );
        if ( fleeBattle.start( &fleeParty, { hashed_string( "slime" ) } ) == false )
            return -1;
        outFirstChance = fleeBattle.computeFleeChance();
        int32 attempts = 1;
        while ( fleeBattle.tryFlee() == false && attempts < 20 )
        {
            ++attempts;
        }
        return fleeBattle.getOutcome() == JrpgBattleOutcome::Fled ? attempts : -1;
    };
    float32     firstChance = 0.0f;
    const int32 attemptsA   = runFlee( 77, firstChance );
    SW_EXPECT_NEAR_EQUAL( 0.5f + ( 10.0f - 3.0f ) * 0.02f, firstChance, 1.0e-5f );
    SW_EXPECT_TRUE( attemptsA >= 1 && attemptsA <= 5 ); // 0.64 → 0.74 → ... 0.95
    SW_EXPECT_EQUAL( attemptsA, runFlee( 77, firstChance ) );
}

SW_TEST_CASE( ClassicJrpgTest, WuxiaInnerEnergyAndManualProficiencyUnlockTechniques )
{
    JrpgTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );
    JrpgBattleSettings settings;
    settings._bWuxia = true;
    JrpgParty party;
    party.initialize( &world._catalog, &world._itemCatalog, world.makeRefs() );
    (void)party.addMember( hashed_string( "li" ), "Li", hashed_string( "swordsman" ), 5 );
    SW_EXPECT_FALSE( party.canUseSpell( 0, hashed_string( "pine_cut" ) ) );
    party.learnManual( 0, hashed_string( "pine_sword" ) );
    vector<JrpgPartyEvent> listPartyEvent;
    party.drainEvents( listPartyEvent );
    SW_EXPECT_TRUE( listPartyEvent.size() == 1 && listPartyEvent[0]._kind == JrpgPartyEvent::Kind::TechniqueUnlocked &&
                    listPartyEvent[0]._id == hashed_string( "pine_cut" ) );
    SW_EXPECT_TRUE( party.canUseSpell( 0, hashed_string( "pine_cut" ) ) );
    SW_EXPECT_FALSE( party.canUseSpell( 0, hashed_string( "pine_storm" ) ) );

    JrpgBattle battle;
    battle.initialize( &world._catalog, &world._judge, settings, 41 );
    SW_ASSERT_TRUE( battle.start( &party, { hashed_string( "golem" ) } ) );
    SW_EXPECT_FALSE( battle.setCommand( 0, JrpgCommand::makeSpell( hashed_string( "pine_cut" ), 0 ) ) ); // 내공 0 < 20

    // 공격 한 번 +10, 맞으면 +5.
    SW_ASSERT_TRUE( battle.setCommand( 0, JrpgCommand::makeAttack( 0 ) ) );
    battle.resolveRound();
    SW_EXPECT_EQUAL( 15, party.getMember( 0 )._inner );
    SW_ASSERT_TRUE( battle.setCommand( 0, JrpgCommand::makeAttack( 0 ) ) );
    battle.resolveRound();
    SW_EXPECT_EQUAL( 30, party.getMember( 0 )._inner );

    // 초식: 내공 20 을 쓰고 숙련 15 — 두 번 쓰면 30 에 닿아 다음 초식이 열린다.
    SW_ASSERT_TRUE( battle.setCommand( 0, JrpgCommand::makeSpell( hashed_string( "pine_cut" ), 0 ) ) );
    battle.resolveRound();
    SW_EXPECT_EQUAL( 15, party.getMember( 0 ).findProficiency( hashed_string( "pine_sword" ) ) );
    SW_EXPECT_EQUAL( 15, party.getMember( 0 )._inner ); // 30 − 20 + 맞은 5
    SW_EXPECT_FALSE( battle.setCommand( 0, JrpgCommand::makeSpell( hashed_string( "pine_cut" ), 0 ) ) );
    SW_ASSERT_TRUE( battle.setCommand( 0, JrpgCommand::makeAttack( 0 ) ) );
    battle.resolveRound();
    SW_ASSERT_TRUE( battle.setCommand( 0, JrpgCommand::makeSpell( hashed_string( "pine_cut" ), 0 ) ) );
    battle.resolveRound();
    SW_EXPECT_EQUAL( 30, party.getMember( 0 ).findProficiency( hashed_string( "pine_sword" ) ) );
    listPartyEvent.clear();
    party.drainEvents( listPartyEvent );
    bool bStormUnlocked = false;
    for ( const JrpgPartyEvent& event : listPartyEvent )
    {
        bStormUnlocked = bStormUnlocked || ( event._kind == JrpgPartyEvent::Kind::TechniqueUnlocked && event._id == hashed_string( "pine_storm" ) );
    }
    SW_EXPECT_TRUE( bStormUnlocked );
    SW_EXPECT_TRUE( party.canUseSpell( 0, hashed_string( "pine_storm" ) ) );
    SW_EXPECT_TRUE( party.getMember( 0 )._inner <= JrpgParty::kInnerMax );

    // 무협 옵션을 끄면 내공은 차지 않고 초식의 내공 비용도 보지 않는다.
    JrpgParty plain;
    plain.initialize( &world._catalog, &world._itemCatalog, world.makeRefs() );
    (void)plain.addMember( hashed_string( "li" ), "Li", hashed_string( "swordsman" ), 5 );
    plain.learnManual( 0, hashed_string( "pine_sword" ) );
    JrpgBattle plainBattle;
    plainBattle.initialize( &world._catalog, &world._judge, JrpgBattleSettings{}, 41 );
    SW_ASSERT_TRUE( plainBattle.start( &plain, { hashed_string( "golem" ) } ) );
    SW_ASSERT_TRUE( plainBattle.setCommand( 0, JrpgCommand::makeAttack( 0 ) ) );
    plainBattle.resolveRound();
    SW_EXPECT_EQUAL( 0, plain.getMember( 0 )._inner );
    SW_EXPECT_TRUE( plainBattle.setCommand( 0, JrpgCommand::makeSpell( hashed_string( "pine_cut" ), 0 ) ) );
}

SW_TEST_CASE( ClassicJrpgTest, StepEncounterRewardSplitAndDeterminism )
{
    JrpgTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );

    // 걸음 수 인카운터: 조우 뒤 3 걸음은 없다, 확률 0.25 · 무리 가중치 3 : 1, 같은 씨앗이면 같은 걸음.
    JrpgEncounterWalker walkerA;
    JrpgEncounterWalker walkerB;
    walkerA.initialize( &world._catalog, 12 );
    walkerB.initialize( &world._catalog, 12 );
    int32 encounters  = 0;
    int32 slimeGroups = 0;
    int32 lastStep    = -100;
    bool  bGraceHeld  = true;
    bool  bSame       = true;
    for ( int32 step = 0; step < 4000; ++step )
    {
        const JrpgEncounterGroup* pGroupA = walkerA.step( hashed_string( "field" ) );
        const JrpgEncounterGroup* pGroupB = walkerB.step( hashed_string( "field" ) );
        bSame                             = bSame && pGroupA == pGroupB;
        if ( pGroupA == nullptr )
            continue;
        bGraceHeld = bGraceHeld && step - lastStep > 3;
        lastStep   = step;
        ++encounters;
        slimeGroups += pGroupA->_listEnemyID.size() == 2 ? 1 : 0;
    }
    SW_EXPECT_TRUE( bSame );
    SW_EXPECT_TRUE( bGraceHeld );
    SW_EXPECT_TRUE( encounters > 480 && encounters < 680 ); // 기대 4000 / (3 + 4) ≈ 571
    SW_EXPECT_TRUE( slimeGroups > encounters * 65 / 100 && slimeGroups < encounters * 85 / 100 );
    SW_EXPECT_TRUE( walkerA.step( hashed_string( "nowhere" ) ) == nullptr );

    // 승리 보상: 경험치 6 × 2 = 12 를 살아 있는 셋이 4 씩, 골드 8 은 지갑으로. 쓰러진 멤버는 받지 않는다.
    const auto runBattle = [&]( uint32 seed, vector<JrpgBattleEvent>& outListEvent, JrpgParty& outParty ) -> JrpgBattleOutcome
    {
        outParty.initialize( &world._catalog, &world._itemCatalog, world.makeRefs() );
        (void)outParty.addMember( hashed_string( "hero" ), "Hero", hashed_string( "hero" ), 1 );
        (void)outParty.addMember( hashed_string( "sol" ), "Sol", hashed_string( "warrior" ), 1 );
        (void)outParty.addMember( hashed_string( "mia" ), "Mia", hashed_string( "mage" ), 1 );
        (void)outParty.addMember( hashed_string( "ted" ), "Ted", hashed_string( "warrior" ), 1 );
        outParty.getMember( 3 )._hp = 0;
        JrpgBattle battle;
        battle.initialize( &world._catalog, &world._judge, JrpgBattleSettings{}, seed );
        if ( battle.start( &outParty, { hashed_string( "slime" ), hashed_string( "slime" ) } ) == false )
            return JrpgBattleOutcome::Ongoing;
        outListEvent.clear();
        vector<JrpgBattleEvent> listRound;
        for ( int32 round = 0; round < 30 && battle.getOutcome() == JrpgBattleOutcome::Ongoing; ++round )
        {
            (void)battle.setCommand( 0, JrpgCommand::makeAttack( 0 ) );
            (void)battle.setCommand( 1, JrpgCommand::makeAttack( 1 ) );
            (void)battle.setCommand( 2, JrpgCommand::makeSpell( hashed_string( "frizz" ), 1 ) ); // 약점 Fire
            SW_EXPECT_FALSE( battle.setCommand( 3, JrpgCommand::makeAttack( 0 ) ) );             // 쓰러진 멤버
            battle.resolveRound();
            listRound.clear();
            battle.drainEvents( listRound );
            outListEvent.insert( outListEvent.end(), listRound.begin(), listRound.end() );
        }
        if ( battle.getRewardExp() != 12 || battle.getRewardGold() != 8 )
            return JrpgBattleOutcome::Ongoing;
        return battle.getOutcome();
    };
    vector<JrpgBattleEvent> listA;
    vector<JrpgBattleEvent> listB;
    JrpgParty               partyA;
    JrpgParty               partyB;
    SW_EXPECT_TRUE( runBattle( 64, listA, partyA ) == JrpgBattleOutcome::Victory );
    SW_EXPECT_EQUAL( 4, findEventValue( listA, JrpgBattleEvent::Kind::Victory ) );
    SW_EXPECT_EQUAL( 8, static_cast<int32>( world._wallet.getBalance( Wallet::getDefaultCurrency() ) ) );
    SW_EXPECT_EQUAL( 4, static_cast<int32>( partyA.getMember( 0 )._level.getTotalXp() ) );
    SW_EXPECT_EQUAL( 0, static_cast<int32>( partyA.getMember( 3 )._level.getTotalXp() ) );
    SW_EXPECT_TRUE( countEvents( listA, JrpgBattleEvent::Kind::SpellCast ) >= 1 );

    SW_EXPECT_TRUE( runBattle( 64, listB, partyB ) == JrpgBattleOutcome::Victory );
    SW_ASSERT_TRUE( listA.size() == listB.size() );
    bool bSameBattle = true;
    for ( size_t index = 0; index < listA.size(); ++index )
    {
        bSameBattle = bSameBattle && listA[index]._kind == listB[index]._kind && listA[index]._actor == listB[index]._actor &&
                      listA[index]._target == listB[index]._target && listA[index]._value == listB[index]._value;
    }
    SW_EXPECT_TRUE( bSameBattle );
}

/**
 * @brief [ClassicJrpgTest] 파티 · 전투 · 걸음 상태 바이트 — 멤버 · 장비 · 비급, 적 · 둔 명령 · 턴 순서 · 난수, 걸음 수가 그대로 와서 같은 라운드 · 같은 걸음이 이어진다. 잘린 바이트는 거절하고 그대로 둔다
 */
SW_TEST_CASE( ClassicJrpgTest, StateRoundTripContinuesTheSameBattle )
{
    JrpgTestWorld world;
    SW_ASSERT_TRUE( world.initialize() );
    JrpgParty party;
    party.initialize( &world._catalog, &world._itemCatalog, world.makeRefs() );
    (void)party.addMember( hashed_string( "hero" ), "Hero", hashed_string( "hero" ), 5 );
    (void)party.addMember( hashed_string( "mia" ), "Mia", hashed_string( "mage" ), 3 );
    party.learnManual( 0, hashed_string( "pine_sword" ) );
    party.addProficiency( 0, hashed_string( "pine_sword" ), 7 );
    SW_EXPECT_EQUAL( 1, world._inventory.addItem( hashed_string( "copper_sword" ), 1 ) );
    const int32 inventorySlot = world._inventory.findFirstSlot( hashed_string( "copper_sword" ) );
    SW_ASSERT_TRUE( inventorySlot >= 0 );
    SW_ASSERT_TRUE( party.getMember( 0 )._equipment.equipFromInventory( world._inventory, inventorySlot ) == EquipResult::Ok );

    JrpgBattle battle;
    battle.initialize( &world._catalog, &world._judge, JrpgBattleSettings{}, 31 );
    SW_ASSERT_TRUE( battle.start( &party, { hashed_string( "slime" ), hashed_string( "golem" ) } ) );
    SW_ASSERT_TRUE( battle.setCommand( 0, JrpgCommand::makeAttack( 1 ) ) );
    SW_ASSERT_TRUE( battle.setCommand( 1, JrpgCommand::makeAttack( 1 ) ) );
    battle.resolveRound();
    SW_ASSERT_TRUE( battle.getOutcome() == JrpgBattleOutcome::Ongoing );
    SW_ASSERT_TRUE( battle.setCommand( 0, JrpgCommand::makeDefend() ) ); // 라운드 중간 — 둔 명령도 싣는다

    JrpgEncounterWalker walker;
    walker.initialize( &world._catalog, 12 );
    for ( int32 stepIndex = 0; stepIndex < 6; ++stepIndex )
    {
        (void)walker.step( hashed_string( "field" ) );
    }

    // 되살린 쪽은 같은 가방 · 지갑을 빌린다(가방을 다시 열지 않는다).
    GameStateRefs refs;
    refs._pInventory = &world._inventory;
    refs._pWallet    = &world._wallet;

    const vector<uint8> partyBytes = captureJrpgBytes( party );
    JrpgParty           restoredParty;
    restoredParty.initialize( &world._catalog, &world._itemCatalog, refs );
    Archive partyReader( partyBytes.data(), partyBytes.size() );
    SW_ASSERT_TRUE( restoredParty.readState( partyReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, partyReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureJrpgBytes( restoredParty ) == partyBytes );
    SW_EXPECT_EQUAL( party.computeAttack( 0 ), restoredParty.computeAttack( 0 ) ); // 장비가 그대로 낀다
    SW_EXPECT_EQUAL( 7, restoredParty.getMember( 0 ).findProficiency( hashed_string( "pine_sword" ) ) );

    const vector<uint8> battleBytes = captureJrpgBytes( battle );
    JrpgBattle          restoredBattle;
    restoredBattle.initialize( &world._catalog, &world._judge, JrpgBattleSettings{}, 999 );
    restoredBattle.bindParty( &restoredParty );
    Archive battleReader( battleBytes.data(), battleBytes.size() );
    SW_ASSERT_TRUE( restoredBattle.readState( battleReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, battleReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureJrpgBytes( restoredBattle ) == battleBytes );
    SW_EXPECT_EQUAL( battle.getRound(), restoredBattle.getRound() );

    const vector<uint8> walkerBytes = captureJrpgBytes( walker );
    JrpgEncounterWalker restoredWalker;
    restoredWalker.initialize( &world._catalog, 1 );
    Archive walkerReader( walkerBytes.data(), walkerBytes.size() );
    SW_ASSERT_TRUE( restoredWalker.readState( walkerReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, walkerReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureJrpgBytes( restoredWalker ) == walkerBytes );

    // 같은 걸음을 둘 다 더 돌리면 바이트가 같다 — 빠진 칸이 있으면 여기서 갈린다.
    SW_ASSERT_TRUE( battle.setCommand( 1, JrpgCommand::makeAttack( 1 ) ) );
    SW_ASSERT_TRUE( restoredBattle.setCommand( 1, JrpgCommand::makeAttack( 1 ) ) );
    battle.resolveRound();
    restoredBattle.resolveRound();
    for ( int32 stepIndex = 0; stepIndex < 10; ++stepIndex )
    {
        const JrpgEncounterGroup* pGroup         = walker.step( hashed_string( "field" ) );
        const JrpgEncounterGroup* pRestoredGroup = restoredWalker.step( hashed_string( "field" ) );
        SW_EXPECT_TRUE( pGroup == pRestoredGroup );
    }
    SW_EXPECT_TRUE( captureJrpgBytes( battle ) == captureJrpgBytes( restoredBattle ) );
    SW_EXPECT_TRUE( captureJrpgBytes( party ) == captureJrpgBytes( restoredParty ) );
    SW_EXPECT_TRUE( captureJrpgBytes( walker ) == captureJrpgBytes( restoredWalker ) );

    JrpgParty truncatedParty;
    truncatedParty.initialize( &world._catalog, &world._itemCatalog, refs );
    Archive partyCut( partyBytes.data(), partyBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedParty.readState( partyCut ) );
    SW_EXPECT_EQUAL( 0, truncatedParty.getMemberCount() );
    JrpgBattle truncatedBattle;
    truncatedBattle.initialize( &world._catalog, &world._judge, JrpgBattleSettings{}, 999 );
    truncatedBattle.bindParty( &restoredParty );
    Archive battleCut( battleBytes.data(), battleBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedBattle.readState( battleCut ) );
    SW_EXPECT_EQUAL( 0, truncatedBattle.getRound() );
    JrpgEncounterWalker truncatedWalker;
    truncatedWalker.initialize( &world._catalog, 1 );
    Archive walkerCut( walkerBytes.data(), walkerBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedWalker.readState( walkerCut ) );
    SW_EXPECT_EQUAL( 0, truncatedWalker.getTotalSteps() );
}
