/**
 * @file LocalStoreContract.h
 * @brief `ILocalStore` 계약 시험 — 메모리 · 파일 · SQLite 저장소가 같은 케이스를 같은 결과로 통과해야 합니다.
 * @details 픽스처는 `ILocalStore& getStore()` · `ILocalSlotStorage& getStorage()`(바닥 — 저장소가 쉬는 동안 봉투를 직접 고친다) ·
 *          `test::LocalStoreTestKey& getKey()`(봉인 키 — 바꾸면 WrongKey) · `void restart()`(앞을 내리고 같은 데이터로 다시 — 다시 켠 게임)를 준다.
 *          봉인 케이스는 Engine 의 암호 제공자(OpenSSL)를 쓴다.
 */
#pragma once
#include "Core/Compression/CompressionCodecRegistry.h"
#include "Core/Compression/ICompressionCodec.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Base/Online/Local/LocalSlotEnvelope.h"
#include "GameFramework/Base/Online/Local/LocalSlotStorage.h"
#include "GameFramework/Base/Online/Local/LocalStore.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"

#include "TestFramework/TestFramework.h"

#include <algorithm>
#include <cstring>
#include <thread>

namespace test
{
    /** @brief 시험의 봉인 키 — 바이트 하나로 키 전체를 채운다(바꾸면 다른 장치). */
    class LocalStoreTestKey final : public sw::ILocalStoreKeyProvider
    {
    public:
        bool getSealKey( uint8 ( &outKey )[kKeySize] ) override
        {
            std::memset( outKey, _fillByte, kKeySize );
            return true;
        }

        uint8 _fillByte{ 0x5A };
    };
} // namespace test

namespace test
{
    /** @brief 계약 케이스 몸들입니다(시험 스레드). */
    struct LocalStoreContract
    {
        static sw::LocalSealContext makeSealContext( LocalStoreTestKey& key )
        {
            sw::LocalSealContext context;
            context._pSecurityProvider = &sw::NetSecurity::getProvider();
            context._pKeyProvider      = &key;
            return context;
        }

        static sw::vector<uint8> makeText( const utf8* pText )
        {
            const sw::string_view text{ pText };
            return sw::vector<uint8>( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
        }

        /** @brief 요청 id 의 완료를 기다립니다(10 초 상한). 받았으면 true. */
        static bool waitCompletion( sw::ILocalStore& store, uint64 requestId, sw::LocalStoreCompletion& outCompletion )
        {
            const sw::Deadline                   deadline = sw::Deadline::afterMilliseconds( 10000 );
            sw::vector<sw::LocalStoreCompletion> listCompletion;
            while ( deadline.isExpired() == false )
            {
                listCompletion.clear();
                if ( store.pollCompletions( listCompletion ) == 0 )
                {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
                    continue;
                }
                for ( sw::LocalStoreCompletion& completion : listCompletion )
                {
                    if ( completion._requestId != requestId )
                        continue;
                    outCompletion = std::move( completion );
                    return true;
                }
            }
            return false;
        }

        static sw::LocalStoreResult write( sw::ILocalStore& store, const utf8* pSlot, const sw::vector<uint8>& bytes,
                                           const sw::LocalStoreWriteOptions& options = sw::LocalStoreWriteOptions{} )
        {
            sw::LocalStoreCompletion completion;
            if ( waitCompletion( store, store.submitWrite( pSlot, bytes, options ), completion ) == false )
                return sw::LocalStoreResult::IOError;
            return completion._result;
        }

        static sw::LocalStoreCompletion read( sw::ILocalStore& store, const utf8* pSlot )
        {
            sw::LocalStoreCompletion completion;
            if ( waitCompletion( store, store.submitRead( pSlot ), completion ) == false )
                completion._result = sw::LocalStoreResult::IOError;
            return completion;
        }

        template <typename FixtureType>
        static void runWriteReadOverwriteErase( FixtureType& fixture )
        {
            using namespace sw;
            ILocalStore&           store = fixture.getStore();
            LocalStoreWriteOptions options;
            options._formatVersion = 7;
            SW_EXPECT_TRUE( write( store, "save/slot0", makeText( "first" ), options ) == LocalStoreResult::Ok );
            LocalStoreCompletion completion = read( store, "save/slot0" );
            SW_EXPECT_TRUE( completion._result == LocalStoreResult::Ok );
            SW_EXPECT_TRUE( completion._bytes == makeText( "first" ) );
            SW_EXPECT_EQUAL( uint32( 7 ), completion._formatVersion );
            SW_EXPECT_TRUE( read( store, "save/missing" )._result == LocalStoreResult::NotFound );

            SW_EXPECT_TRUE( write( store, "save/slot0", makeText( "second, longer" ) ) == LocalStoreResult::Ok );
            SW_EXPECT_TRUE( read( store, "save/slot0" )._bytes == makeText( "second, longer" ) );
            SW_EXPECT_TRUE( write( store, "empty", vector<uint8>{} ) == LocalStoreResult::Ok ); // 빈 몸도 슬롯이다
            SW_EXPECT_TRUE( read( store, "empty" )._bytes.empty() );

            LocalStoreCompletion erased;
            SW_ASSERT_TRUE( waitCompletion( store, store.submitErase( "save/slot0" ), erased ) );
            SW_EXPECT_TRUE( erased._result == LocalStoreResult::Ok );
            SW_ASSERT_TRUE( waitCompletion( store, store.submitErase( "save/slot0" ), erased ) );
            SW_EXPECT_TRUE( erased._result == LocalStoreResult::NotFound );
            SW_EXPECT_TRUE( read( store, "save/slot0" )._result == LocalStoreResult::NotFound );
        }

        template <typename FixtureType>
        static void runListByGroupAndSurviveRestart( FixtureType& fixture )
        {
            using namespace sw;
            SW_EXPECT_TRUE( write( fixture.getStore(), "save/b", makeText( "b" ) ) == LocalStoreResult::Ok );
            SW_EXPECT_TRUE( write( fixture.getStore(), "save/a", makeText( "a" ) ) == LocalStoreResult::Ok );
            SW_EXPECT_TRUE( write( fixture.getStore(), "save/c", makeText( "c" ) ) == LocalStoreResult::Ok );
            SW_EXPECT_TRUE( write( fixture.getStore(), "cfg/keys", makeText( "k" ) ) == LocalStoreResult::Ok );
            fixture.restart(); // 다시 켠 게임 — 같은 데이터
            LocalStoreCompletion listed;
            SW_ASSERT_TRUE( waitCompletion( fixture.getStore(), fixture.getStore().submitList( "save/" ), listed ) );
            SW_ASSERT_TRUE( listed._result == LocalStoreResult::Ok );
            SW_ASSERT_EQUAL( size_t( 3 ), listed._listSlotInfo.size() );
            SW_EXPECT_TRUE( listed._listSlotInfo[0]._slot == "save/a" && listed._listSlotInfo[2]._slot == "save/c" );
            SW_EXPECT_TRUE( listed._listSlotInfo[0]._byteCount > 0 );
            SW_ASSERT_TRUE( waitCompletion( fixture.getStore(), fixture.getStore().submitList( "" ), listed ) );
            SW_EXPECT_EQUAL( size_t( 4 ), listed._listSlotInfo.size() );
            SW_EXPECT_TRUE( read( fixture.getStore(), "cfg/keys" )._bytes == makeText( "k" ) );
        }

        template <typename FixtureType>
        static void runCompressionRoundTrips( FixtureType& fixture )
        {
            using namespace sw;
            const CompressionCodecRegistry* pRegistry = CompressionCodecRegistry::getActive();
            const bool                      bZstd     = pRegistry != nullptr && pRegistry->isCodecRegistered( CompressionCodecType::Zstd );
            LocalStoreWriteOptions          options;
            options._compressionCodec = static_cast<uint8>( bZstd ? CompressionCodecType::Zstd : CompressionCodecType::RLE );
            vector<uint8> repetitiveBytes( 20000, 'x' );
            SW_EXPECT_TRUE( write( fixture.getStore(), "packed", repetitiveBytes, options ) == LocalStoreResult::Ok );
            vector<uint8> envelopeBytes;
            SW_ASSERT_TRUE( fixture.getStorage().readSlot( "packed", envelopeBytes ) == LocalStoreResult::Ok );
            SW_EXPECT_TRUE( envelopeBytes.size() < repetitiveBytes.size() / 4 ); // 줄었다
            SW_EXPECT_TRUE( read( fixture.getStore(), "packed" )._bytes == repetitiveBytes );

            vector<uint8> noise( 4096 );
            uint32        state = 2463534242u;
            for ( uint8& value : noise )
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                value = static_cast<uint8>( state );
            }
            SW_EXPECT_TRUE( write( fixture.getStore(), "noise", noise, options ) == LocalStoreResult::Ok ); // 줄지 않으면 원문으로 둔다
            SW_EXPECT_TRUE( read( fixture.getStore(), "noise" )._bytes == noise );
        }

        template <typename FixtureType>
        static void runAuthenticatedRejectsATamperedByte( FixtureType& fixture )
        {
            using namespace sw;
            LocalStoreWriteOptions options;
            options._seal = LocalStoreSeal::Authenticated;
            SW_EXPECT_TRUE( write( fixture.getStore(), "profile", makeText( "gold=100" ), options ) == LocalStoreResult::Ok );
            vector<uint8> envelopeBytes;
            SW_ASSERT_TRUE( fixture.getStorage().readSlot( "profile", envelopeBytes ) == LocalStoreResult::Ok );
            const vector<uint8> body = makeText( "gold=100" );
            SW_EXPECT_TRUE( std::search( envelopeBytes.begin(), envelopeBytes.end(), body.begin(), body.end() ) != envelopeBytes.end() ); // 내용은 보인다
            SW_EXPECT_TRUE( read( fixture.getStore(), "profile" )._bytes == body );
            envelopeBytes[LocalSlotEnvelope::kHeaderSize + 5] ^= 0x01; // "gold=1" → "gold=0"
            SW_ASSERT_TRUE( fixture.getStorage().writeSlot( "profile", envelopeBytes ) == LocalStoreResult::Ok );
            SW_EXPECT_TRUE( read( fixture.getStore(), "profile" )._result == LocalStoreResult::Corrupt );
        }

        template <typename FixtureType>
        static void runEncryptedHidesTheBodyAndNeedsTheKey( FixtureType& fixture )
        {
            using namespace sw;
            LocalStoreWriteOptions options;
            options._seal             = LocalStoreSeal::Encrypted;
            options._compressionCodec = static_cast<uint8>( CompressionCodecType::RLE );
            const vector<uint8> body  = makeText( "link-token:abcdefabcdefabcdefabcdef" );
            SW_EXPECT_TRUE( write( fixture.getStore(), "account/link", body, options ) == LocalStoreResult::Ok );
            vector<uint8> envelopeBytes;
            SW_ASSERT_TRUE( fixture.getStorage().readSlot( "account/link", envelopeBytes ) == LocalStoreResult::Ok );
            const vector<uint8> marker = makeText( "link-token" );
            SW_EXPECT_TRUE( std::search( envelopeBytes.begin(), envelopeBytes.end(), marker.begin(), marker.end() ) == envelopeBytes.end() ); // 평문이 없다
            SW_EXPECT_TRUE( read( fixture.getStore(), "account/link" )._bytes == body );

            fixture.getKey()._fillByte = 0x11; // 다른 장치의 키
            SW_EXPECT_TRUE( read( fixture.getStore(), "account/link" )._result == LocalStoreResult::WrongKey );
            fixture.getKey()._fillByte = 0x5A;
            envelopeBytes.back() ^= 0x80; // 태그
            SW_ASSERT_TRUE( fixture.getStorage().writeSlot( "account/link", envelopeBytes ) == LocalStoreResult::Ok );
            SW_EXPECT_TRUE( read( fixture.getStore(), "account/link" )._result == LocalStoreResult::Corrupt );
        }

        template <typename FixtureType>
        static void runNoneSealCatchesATamperedByteByCrc( FixtureType& fixture )
        {
            using namespace sw;
            SW_EXPECT_TRUE( write( fixture.getStore(), "plain", makeText( "readable save" ) ) == LocalStoreResult::Ok );
            vector<uint8> envelopeBytes;
            SW_ASSERT_TRUE( fixture.getStorage().readSlot( "plain", envelopeBytes ) == LocalStoreResult::Ok );
            envelopeBytes[LocalSlotEnvelope::kHeaderSize] ^= 0x20;
            SW_ASSERT_TRUE( fixture.getStorage().writeSlot( "plain", envelopeBytes ) == LocalStoreResult::Ok );
            SW_EXPECT_TRUE( read( fixture.getStore(), "plain" )._result == LocalStoreResult::Corrupt );
            envelopeBytes.resize( envelopeBytes.size() / 2 ); // 반쪽 파일
            SW_ASSERT_TRUE( fixture.getStorage().writeSlot( "plain", envelopeBytes ) == LocalStoreResult::Ok );
            SW_EXPECT_TRUE( read( fixture.getStore(), "plain" )._result == LocalStoreResult::Corrupt );
        }

        template <typename FixtureType>
        static void runRejectsBadNamesAndOversizedSlots( FixtureType& fixture )
        {
            using namespace sw;
            ILocalStore& store = fixture.getStore();
            SW_EXPECT_TRUE( write( store, "Save/Upper", makeText( "x" ) ) == LocalStoreResult::Invalid );
            SW_EXPECT_TRUE( write( store, "../escape", makeText( "x" ) ) == LocalStoreResult::Invalid );
            SW_EXPECT_TRUE( write( store, "a/b/c", makeText( "x" ) ) == LocalStoreResult::Invalid );
            SW_EXPECT_TRUE( write( store, "a/.hidden", makeText( "x" ) ) == LocalStoreResult::Invalid );
            SW_EXPECT_TRUE( write( store, "", makeText( "x" ) ) == LocalStoreResult::Invalid );
            SW_EXPECT_TRUE( write( store, "save/", makeText( "x" ) ) == LocalStoreResult::Invalid );
            LocalStoreCompletion listed;
            SW_ASSERT_TRUE( waitCompletion( store, store.submitList( "save" ), listed ) ); // 접두는 `묶음/`
            SW_EXPECT_TRUE( listed._result == LocalStoreResult::Invalid );
            vector<uint8> oversizedBytes( static_cast<size_t>( ILocalStore::kMaxSlotSize ) + 1 );
            SW_EXPECT_TRUE( write( store, "huge", oversizedBytes ) == LocalStoreResult::Invalid );
            SW_EXPECT_TRUE( read( store, "huge" )._result == LocalStoreResult::NotFound );
        }

        template <typename FixtureType>
        static void runCompletionsComeInSubmitOrder( FixtureType& fixture )
        {
            using namespace sw;
            ILocalStore&   store = fixture.getStore();
            vector<uint64> listRequestId;
            for ( int32 index = 0; index < 40; ++index )
            {
                const uint8 value = static_cast<uint8>( index );
                listRequestId.push_back( index % 2 == 0 ? store.submitWrite( "order", vector<uint8>{ value }, LocalStoreWriteOptions{} ) : store.submitRead( "order" ) );
            }
            vector<LocalStoreCompletion> listCompletion;
            const Deadline               deadline = Deadline::afterMilliseconds( 10000 );
            while ( listCompletion.size() < listRequestId.size() && deadline.isExpired() == false )
            {
                if ( store.pollCompletions( listCompletion ) == 0 )
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
            SW_ASSERT_EQUAL( listRequestId.size(), listCompletion.size() );
            for ( size_t index = 0; index < listCompletion.size(); ++index )
            {
                SW_EXPECT_EQUAL( listRequestId[index], listCompletion[index]._requestId );
                if ( index % 2 == 1 )
                    SW_EXPECT_TRUE( listCompletion[index]._bytes == vector<uint8>{ static_cast<uint8>( index - 1 ) } ); // 바로 앞 쓰기를 본다
            }
            SW_EXPECT_EQUAL( 0, store.getPendingCount() );
        }

        template <typename FixtureType>
        static void runShutdownFinishesQueuedWrites( FixtureType& fixture )
        {
            using namespace sw;
            ILocalStore& store = fixture.getStore();
            for ( int32 index = 0; index < 20; ++index )
            {
                (void)store.submitWrite( "flush", vector<uint8>{ static_cast<uint8>( index ) }, LocalStoreWriteOptions{} );
            }
            store.shutdown(); // 남은 쓰기를 끝까지
            vector<LocalStoreCompletion> listCompletion;
            (void)store.pollCompletions( listCompletion );
            SW_EXPECT_EQUAL( size_t( 20 ), listCompletion.size() );
            LocalStoreCompletion afterShutdown;
            SW_ASSERT_TRUE( waitCompletion( store, store.submitRead( "flush" ), afterShutdown ) );
            SW_EXPECT_TRUE( afterShutdown._result == LocalStoreResult::IOError );
            fixture.restart();
            SW_EXPECT_TRUE( read( fixture.getStore(), "flush" )._bytes == vector<uint8>{ 19 } );
        }
    };
} // namespace test

/** @brief 계약 케이스 하나를 @p SuiteName 스위트에 만듭니다. */
#define SW_LOCAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, CaseName, RunFunction ) \
    SW_TEST_CASE( SuiteName, CaseName )                                               \
    {                                                                                 \
        FixtureType fixture;                                                          \
        test::LocalStoreContract::RunFunction( fixture );                             \
    }

/** @brief 계약 케이스 열을 @p SuiteName 스위트로 만듭니다. */
#define SW_LOCAL_STORE_CONTRACT_SUITE( SuiteName, FixtureType )                                                                         \
    SW_LOCAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, WriteReadOverwriteErase, runWriteReadOverwriteErase )                         \
    SW_LOCAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, ListByGroupAndSurviveRestart, runListByGroupAndSurviveRestart )               \
    SW_LOCAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, CompressionRoundTrips, runCompressionRoundTrips )                             \
    SW_LOCAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, AuthenticatedRejectsATamperedByte, runAuthenticatedRejectsATamperedByte )     \
    SW_LOCAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, EncryptedHidesTheBodyAndNeedsTheKey, runEncryptedHidesTheBodyAndNeedsTheKey ) \
    SW_LOCAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, NoneSealCatchesATamperedByteByCrc, runNoneSealCatchesATamperedByteByCrc )     \
    SW_LOCAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, RejectsBadNamesAndOversizedSlots, runRejectsBadNamesAndOversizedSlots )       \
    SW_LOCAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, CompletionsComeInSubmitOrder, runCompletionsComeInSubmitOrder )               \
    SW_LOCAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, ShutdownFinishesQueuedWrites, runShutdownFinishesQueuedWrites )
