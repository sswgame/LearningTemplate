// LedgerTypesTest — 원장 공유 타입의 순수 규칙: 글자 규칙(자산 id · 사유 · 에스크로 도메인 · 토큰 · 분개 키)의 경계, 보유자 키 · 같음 · 유효성,
// 보유자 · 다리의 와이어 왕복과 깨진 몸 거절, 분개 키 만들기(멱등 키 · 바깥 토큰)와 그 결과가 원장이 받는 키인지. 원장 적용 자체는 원장 서버 쪽이 본다.
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"

using namespace sw;

namespace
{
    struct LedgerTypesTestUtil
    {
        static sw::string repeat( int32 count, utf8 ch ) { return sw::string( static_cast<size_t>( count ), ch ); }

        static BitReader makeReader( const BitWriter& writer ) { return BitReader( writer.getBytes().data(), writer.getByteCount() ); }

        /** @brief @p holder 를 쓰고 다시 읽은 결과입니다. 읽기가 거절하면 비어 있습니다. */
        static optional<LedgerHolder> roundTrip( const LedgerHolder& holder )
        {
            BitWriter writer;
            LedgerHolder::write( writer, holder );
            BitReader    reader = makeReader( writer );
            LedgerHolder read;
            if ( LedgerHolder::read( reader, read ) == false )
                return nullopt;
            return read;
        }
    };
    using Util = LedgerTypesTestUtil;
} // namespace

/** @brief [LedgerTypesTest] 글자 규칙은 소문자 · 숫자와 규칙마다 허락한 기호만, 길이 1..상한이다 */
SW_TEST_CASE( LedgerTypesTest, TextRulesAcceptOnlyTheirAlphabetAndLength )
{
    // 틀리면: 대문자 · 남의 기호 · 빈 글 · 상한+1 을 받으면 해당 거짓 단언이, 상한 정확히를 거절하면 참 단언이 진다.
    SW_EXPECT_TRUE( LedgerUtil::isValidAssetID( "cur.gold" ) );
    SW_EXPECT_TRUE( LedgerUtil::isValidAssetID( "item.sword-2_b" ) );
    SW_EXPECT_FALSE( LedgerUtil::isValidAssetID( "Cur.gold" ) );
    SW_EXPECT_FALSE( LedgerUtil::isValidAssetID( "" ) );
    SW_EXPECT_FALSE( LedgerUtil::isValidAssetID( "cur gold" ) );
    SW_EXPECT_TRUE( LedgerUtil::isValidAssetID( Util::repeat( LedgerConstant::kMaxAssetIDSize, 'a' ) ) );
    SW_EXPECT_FALSE( LedgerUtil::isValidAssetID( Util::repeat( LedgerConstant::kMaxAssetIDSize + 1, 'a' ) ) );

    SW_EXPECT_TRUE( LedgerUtil::isValidReasonCode( "shop.buy" ) );
    SW_EXPECT_FALSE( LedgerUtil::isValidReasonCode( "shop-buy" ) ); // '-' 는 자산 · 토큰에만
    SW_EXPECT_TRUE( LedgerUtil::isValidReasonCode( Util::repeat( LedgerConstant::kMaxReasonSize, 'r' ) ) );
    SW_EXPECT_FALSE( LedgerUtil::isValidReasonCode( Util::repeat( LedgerConstant::kMaxReasonSize + 1, 'r' ) ) );

    SW_EXPECT_TRUE( LedgerUtil::isValidEscrowDomain( "mail_box" ) );
    SW_EXPECT_FALSE( LedgerUtil::isValidEscrowDomain( "mail.box" ) ); // 도메인은 '_' 만
    SW_EXPECT_FALSE( LedgerUtil::isValidEscrowDomain( Util::repeat( 17, 'd' ) ) );
    SW_EXPECT_TRUE( LedgerUtil::isValidEscrowToken( "t-1.a_b" ) );
    SW_EXPECT_FALSE( LedgerUtil::isValidEscrowToken( "t/1" ) );
    SW_EXPECT_TRUE( LedgerUtil::isValidEscrowToken( Util::repeat( 64, 't' ) ) );
    SW_EXPECT_FALSE( LedgerUtil::isValidEscrowToken( Util::repeat( 65, 't' ) ) );
}

/** @brief [LedgerTypesTest] 분개 키는 가운데 '/' 가 있어야 하고(앞뒤 '/' 는 범위나 꼬리가 빈 것) 길이 상한을 넘지 않는다 */
SW_TEST_CASE( LedgerTypesTest, JournalKeyNeedsAScopeAndATail )
{
    // 틀리면: '/' 검사 · 앞뒤 검사 · 길이 상한 중 하나를 빼면 해당 거짓 단언이 진다.
    SW_EXPECT_TRUE( LedgerUtil::isValidJournalKey( "acct.1/abc" ) );
    SW_EXPECT_FALSE( LedgerUtil::isValidJournalKey( "acct.1abc" ) );
    SW_EXPECT_FALSE( LedgerUtil::isValidJournalKey( "/abc" ) );
    SW_EXPECT_FALSE( LedgerUtil::isValidJournalKey( "abc/" ) );
    SW_EXPECT_FALSE( LedgerUtil::isValidJournalKey( "" ) );
    sw::string longest = "s/";
    longest += Util::repeat( LedgerConstant::kMaxJournalKeySize - 2, 'k' );
    SW_EXPECT_TRUE( LedgerUtil::isValidJournalKey( longest ) );
    longest += 'k';
    SW_EXPECT_FALSE( LedgerUtil::isValidJournalKey( longest ) );
}

/** @brief [LedgerTypesTest] 보유자 키 — 계정은 소문자 16 진 16 자, 에스크로는 도메인/토큰, 시스템은 고정 이름이다 */
SW_TEST_CASE( LedgerTypesTest, HolderKeysFollowTheirFormat )
{
    // 틀리면: 16 진 폭 · 대소문자 · 접두가 바뀌면 키 문장 단언이 진다(저장된 잔액 레코드를 못 찾는다).
    SW_EXPECT_EQUAL( sw::string( "acct/00000000000000ab" ), LedgerHolder::makeAccount( 0xAB ).makeKey() );
    SW_EXPECT_EQUAL( sw::string( "esc/mail/tok1" ), LedgerHolder::makeEscrow( "mail", "tok1" ).makeKey() );
    SW_EXPECT_EQUAL( sw::string( "sys/mint" ), LedgerHolder::makeMint().makeKey() );
    SW_EXPECT_EQUAL( sw::string( "sys/sink" ), LedgerHolder::makeSink().makeKey() );
}

/** @brief [LedgerTypesTest] 유효성 · 같음은 종류가 쓰는 칸만 본다 — 계정 0, 규칙 밖 에스크로, Count 종류는 무효이다 */
SW_TEST_CASE( LedgerTypesTest, HolderValidityAndEqualityLookOnlyAtTheirKindProperties )
{
    // 틀리면: 계정 0 · 대문자 도메인 · Count 를 받으면 무효 단언이, 종류를 안 보고 비교하면 발행 ≠ 소각이, 안 쓰는 칸까지 비교하면 마지막 같음 단언이 진다.
    SW_EXPECT_TRUE( LedgerHolder::makeAccount( 1 ).isValid() );
    SW_EXPECT_FALSE( LedgerHolder::makeAccount( 0 ).isValid() );
    SW_EXPECT_FALSE( LedgerHolder::makeEscrow( "Mail", "tok" ).isValid() );
    SW_EXPECT_FALSE( LedgerHolder::makeEscrow( "mail", "" ).isValid() );
    LedgerHolder unknown;
    unknown._kind = LedgerHolderKind::Count;
    SW_EXPECT_FALSE( unknown.isValid() );

    SW_EXPECT_TRUE( LedgerHolder::makeAccount( 5 ) == LedgerHolder::makeAccount( 5 ) );
    SW_EXPECT_TRUE( LedgerHolder::makeAccount( 5 ) != LedgerHolder::makeAccount( 6 ) );
    SW_EXPECT_TRUE( LedgerHolder::makeEscrow( "mail", "a" ) != LedgerHolder::makeEscrow( "mail", "b" ) );
    SW_EXPECT_TRUE( LedgerHolder::makeMint() != LedgerHolder::makeSink() );
    SW_EXPECT_TRUE( LedgerHolder::makeMint().isSystem() );
    SW_EXPECT_FALSE( LedgerHolder::makeAccount( 1 ).isSystem() );
    LedgerHolder mintWithStray = LedgerHolder::makeMint();
    mintWithStray._accountID   = 99;
    SW_EXPECT_TRUE( mintWithStray == LedgerHolder::makeMint() );
}

/** @brief [LedgerTypesTest] 보유자는 종류마다 왕복하고, 모르는 종류 · 무효 보유자 · 잘린 몸은 읽기가 거절한다 */
SW_TEST_CASE( LedgerTypesTest, HolderWireRoundTripsAndRefusesBadBodies )
{
    // 틀리면: 종류의 칸을 빠뜨려 쓰면 왕복 같음이, 종류 상한 · 유효성 · 넘침 검사를 빼면 해당 거절 단언이 진다.
    const LedgerHolder listHolder[] = { LedgerHolder::makeAccount( 0x1234567890ull ), LedgerHolder::makeEscrow( "auction", "lot-7" ), LedgerHolder::makeMint(),
                                        LedgerHolder::makeSink() };
    for ( const LedgerHolder& holder : listHolder )
    {
        const optional<LedgerHolder> read = Util::roundTrip( holder );
        SW_REQUIRE_TRUE( read.has_value() );
        SW_EXPECT_TRUE( *read == holder );
    }
    {
        BitWriter writer;
        writer.writeVarUint( static_cast<uint64>( LedgerHolderKind::Count ) );
        BitReader    reader = Util::makeReader( writer );
        LedgerHolder read;
        SW_EXPECT_FALSE( LedgerHolder::read( reader, read ) );
    }
    SW_EXPECT_FALSE( Util::roundTrip( LedgerHolder::makeAccount( 0 ) ).has_value() );
    SW_EXPECT_FALSE( Util::roundTrip( LedgerHolder::makeEscrow( "Bad", "tok" ) ).has_value() );
    {
        BitWriter writer;
        LedgerHolder::write( writer, LedgerHolder::makeEscrow( "auction", "lot-7" ) );
        BitReader    reader( writer.getBytes().data(), 2 ); // 종류와 도메인 길이만 남긴다
        LedgerHolder read;
        SW_EXPECT_FALSE( LedgerHolder::read( reader, read ) );
    }
}

/** @brief [LedgerTypesTest] 다리는 왕복하고, 자산 id 가 상한을 넘으면 읽기가 거절한다 */
SW_TEST_CASE( LedgerTypesTest, PostingWireRoundTripsAndCapsTheAssetID )
{
    // 틀리면: 금액 부호 · 자산 칸이 왕복에서 바뀌면 같음 단언이, 자산 상한을 읽기에서 보지 않으면 거절 단언이 진다.
    LedgerPosting posting;
    posting._from    = LedgerHolder::makeAccount( 7 );
    posting._to      = LedgerHolder::makeEscrow( "mail", "m1" );
    posting._assetID = "cur.gold";
    posting._amount  = -25; // 음수도 그대로 싣는다(금액 규칙은 원장이 본다)
    {
        BitWriter writer;
        LedgerPosting::write( writer, posting );
        BitReader     reader = Util::makeReader( writer );
        LedgerPosting read;
        SW_REQUIRE_TRUE( LedgerPosting::read( reader, read ) );
        SW_EXPECT_TRUE( read._from == posting._from );
        SW_EXPECT_TRUE( read._to == posting._to );
        SW_EXPECT_EQUAL( posting._assetID, read._assetID );
        SW_EXPECT_EQUAL( posting._amount, read._amount );
    }
    posting._assetID = Util::repeat( LedgerConstant::kMaxAssetIDSize + 1, 'a' );
    {
        BitWriter writer;
        LedgerPosting::write( writer, posting );
        BitReader     reader = Util::makeReader( writer );
        LedgerPosting read;
        SW_EXPECT_FALSE( LedgerPosting::read( reader, read ) );
    }
}

/** @brief [LedgerTypesTest] 분개 키 만들기 — 멱등 키는 범위/16 진 32 자, 토큰은 범위/토큰 바이트의 16 진이고 범위 · 빈 토큰은 거절한다 */
SW_TEST_CASE( LedgerTypesTest, JournalKeysFromIdempotencyAndTokens )
{
    // 틀리면: 16 진 순서(높은 쪽 먼저) · 바이트 펴기가 바뀌면 문장 단언이, 범위 · 빈 토큰 검사를 빼면 거절 단언이, 실패 때 글을 남기면 빈 글 단언이 진다.
    SW_EXPECT_EQUAL( sw::string( "acct.0000000000000001/00000000000000020000000000000003" ),
                     LedgerJournalKey::makeFromIdempotency( LedgerJournalKey::makeAccountScope( 1 ), 2, 3 ) );
    SW_EXPECT_EQUAL( sw::string( "gm.00000000000000ff" ), LedgerJournalKey::makeAdminScope( 0xFF ) );

    sw::string key;
    SW_REQUIRE_TRUE( LedgerJournalKey::makeFromToken( "mail.claim", "ab", key ) );
    SW_EXPECT_EQUAL( sw::string( "mail.claim/6162" ), key );
    SW_EXPECT_TRUE( LedgerUtil::isValidJournalKey( key ) );

    SW_EXPECT_FALSE( LedgerJournalKey::makeFromToken( "Mail", "ab", key ) );
    SW_EXPECT_TRUE( key.empty() );
    SW_EXPECT_FALSE( LedgerJournalKey::makeFromToken( "mail", "", key ) );
    SW_EXPECT_FALSE( LedgerJournalKey::makeFromToken( "mail", Util::repeat( LedgerConstant::kMaxTokenBytes + 1, 'x' ), key ) );
}

/**
 * @brief [LedgerTypesTest] 토큰으로 만든 분개 키는 언제나 원장이 받는 키다 — 만들 수 있다고 하고 원장이 Invalid 로 거절하는 키를 내지 않는다.
 * @details 토큰 상한은 분개 키 상한에서 낸다(범위 상한 + '/' + 16 진). 예전 상한(96 바이트, 16 진 192 자)은 긴 범위와 합치면 키 상한(200)을 넘어,
 *          와이어가 받은 청약철회 거래 id 가 원장 앞에서 거절됐다. 범위가 상한을 넘으면 만들기가 거절한다.
 */
SW_TEST_CASE( LedgerTypesTest, TokenJournalKeyIsAlwaysAValidJournalKey )
{
    // 틀리면: 토큰 상한이 키 상한과 갈리면 상한 범위 + 상한 토큰이 거절되거나 원장이 받지 않는 키라 REQUIRE · 유효 단언이, 범위 상한 검사를 빼면 거짓 단언이 진다.
    const sw::string longestScope = Util::repeat( LedgerConstant::kMaxJournalScopeSize, 's' );
    const sw::string longestToken = Util::repeat( LedgerConstant::kMaxTokenBytes, 'x' );
    sw::string       key;
    SW_REQUIRE_TRUE( LedgerJournalKey::makeFromToken( longestScope, longestToken, key ) );
    SW_EXPECT_TRUE( LedgerUtil::isValidJournalKey( key ) );
    SW_EXPECT_FALSE( LedgerJournalKey::makeFromToken( Util::repeat( LedgerConstant::kMaxJournalScopeSize + 1, 's' ), "ab", key ) );
    SW_EXPECT_TRUE( key.empty() );
}
