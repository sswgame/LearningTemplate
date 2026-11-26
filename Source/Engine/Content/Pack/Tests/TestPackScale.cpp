// PackScaleTest — 30 만 항목 합성 팩의 마운트 · 색인 메모리 · 조회와 1 GiB 패치 확인 비용을 잰다(감사 A-9 ~ A-12, 설계 PK-1).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Content/Pack/PackReader.h"
#include "Engine/Content/Pack/PackWriter.h"
#include "Engine/Content/Pack/Tests/PackTestUtil.h"
#include "Engine/Foundation/EngineSubsystems.h"

#include <algorithm>

/**
 * @file TestPackScale.cpp
 * @brief 팩 규모 벤치입니다. 시간 문턱은 두지 않고 값을 로그(`Pack.Scale.*`)로 남긴다 — 팩 형식을 고친 앞뒤를 견줄 때 쓴다.
 * @details 재는 법: Release `App --test --test-run-disabled --test-filter=PackScaleTest.*` 를 따뜻한 캐시로 5 번, 값마다 중앙값.
 *          합성 팩은 스크래치 폴더에 짓는다(`index300k` 약 190 MB, `patch1g` 1 GiB). 케이스가 무거워 평소 실행에서 끈다.
 *          - `index300k`: 30 만 × 256 ~ 1024 B, 세 단계 폴더(20 × 20 × 20) · 경로 49 자.
 *          - `patch1g`: 1 GiB 패치 팩 하나(256 × 4 MiB). 지금의 확인은 팩 전체를 읽어 SHA-256 을 센다(`PackManager::verifyPatchSignatures`).
 */
namespace sw
{
    namespace
    {
        struct PackScaleInternal
        {
            static constexpr uint32 kIndexEntryCount  = 300000;
            static constexpr uint32 kFolderFanOut     = 20;
            static constexpr uint32 kMountRepeatCount = 5;
            static constexpr uint32 kLookupCount      = 1000000;
            static constexpr uint32 kPatchEntryCount  = 256;
            static constexpr uint32 kPatchEntryBytes  = 4 * 1024 * 1024;

            static uint64 nextRandom( uint64& inoutState )
            {
                inoutState ^= inoutState << 13;
                inoutState ^= inoutState >> 7;
                inoutState ^= inoutState << 17;
                return inoutState;
            }

            /** @brief 항목 @p index 의 팩 안 경로입니다(`folder_07/sub_13/leaf_21/entry_123456_payload.bin`, 49 자). */
            static string makeEntryPath( uint32 index )
            {
                string text;
                appendPadded( text.append( "folder_" ), index % kFolderFanOut, 2 );
                appendPadded( text.append( "/sub_" ), ( index / kFolderFanOut ) % kFolderFanOut, 2 );
                appendPadded( text.append( "/leaf_" ), ( index / ( kFolderFanOut * kFolderFanOut ) ) % kFolderFanOut, 2 );
                appendPadded( text.append( "/entry_" ), index, 6 );
                return text.append( "_payload.bin" );
            }

            /** @brief @p value 를 @p digitCount 자리 0 채움 10 진으로 덧붙입니다. */
            static void appendPadded( string& inoutText, uint32 value, uint32 digitCount )
            {
                utf8 arrDigit[10];
                for ( uint32 digit = digitCount; digit > 0; --digit )
                {
                    arrDigit[digit - 1] = static_cast<utf8>( '0' + value % 10 );
                    value /= 10;
                }
                inoutText.append( arrDigit, digitCount );
            }

            /** @brief 결정적인 바이트 @p size 개입니다(절반은 같은 바이트가 이어져 압축되는 모양). */
            static vector<uint8> makeEntryBytes( uint64 seed, size_t size )
            {
                vector<uint8> bytes( size );
                uint64        state = seed | 1u;
                for ( size_t index = 0; index < size; ++index )
                {
                    bytes[index] = index < size / 2 ? static_cast<uint8>( nextRandom( state ) ) : static_cast<uint8>( seed );
                }
                return bytes;
            }

            /**
             * @brief 항목 @p entryCount 개를 경로 해시 순으로 @p packPath 에 스트림으로 씁니다. 항목 i 의 바이트는 `makeEntryBytes( i, listSize[i] )` 다.
             * @details 스트림 쓰기는 경로 해시 오름차순만 받는다 — 번호를 해시로 세운 뒤 그 순서로 바이트를 만든다(팩 전체를 들지 않는다).
             */
            [[nodiscard]] static optional<PackWriteResult> writeEntries( const AbsolutePath& packPath, const PackWriterDesc& desc, const vector<size_t>& listSize, vector<uint64>& outListHash )
            {
                const uint32   entryCount = static_cast<uint32>( listSize.size() );
                vector<uint32> listOrder( entryCount );
                outListHash.resize( entryCount );
                for ( uint32 index = 0; index < entryCount; ++index )
                {
                    listOrder[index]   = index;
                    outListHash[index] = PackStreamWriter::makePathHash( makeEntryPath( index ) );
                }
                VectorUtil::sort( listOrder, [&outListHash]( uint32 left, uint32 right )
                { return outListHash[left] < outListHash[right]; } );
                PackStreamWriter writer;
                expected<void>   step = writer.open( packPath, desc );
                for ( size_t order = 0; order < listOrder.size() && step.has_value(); ++order )
                {
                    const uint32 index = listOrder[order];
                    step               = writer.append( makeEntryPath( index ), PackEntryStorage::Blocks, makeEntryBytes( index, listSize[index] ) );
                }
                if ( step.has_value() == false )
                    return nullopt;
                expected<PackWriteResult> closed = writer.close();
                return closed.has_value() ? optional<PackWriteResult>{ std::move( *closed ) } : nullopt;
            }

            /** @brief `index300k` 를 @p packPath 에 쓰고 항목의 경로 해시를 @p outListHash 에 담습니다. */
            [[nodiscard]] static bool writeIndexPack( const AbsolutePath& packPath, vector<uint64>& outListHash )
            {
                uint64         state = 0x9E3779B97F4A7C15ull;
                vector<size_t> listSize( kIndexEntryCount );
                for ( size_t& size : listSize )
                {
                    size = 256 + nextRandom( state ) % 769;
                }
                return writeEntries( packPath, PackTestUtil::makeDesc( "game/scale" ), listSize, outListHash ).has_value();
            }

            /** @brief 활성 메모리 프로파일러의 살아 있는 바이트입니다. 프로파일러가 없으면(배포본) 0 입니다. */
            static uint64 readLiveBytes()
            {
                const MemoryProfiler* pProfiler = MemoryProfiler::getActive();
                return pProfiler != nullptr ? pProfiler->getLiveAllocatedBytes() : 0;
            }
        };
    } // namespace
} // namespace sw

using namespace sw;

/**
 * @brief [PackScaleTest] `index300k` 의 마운트 시간(5 번 중앙값) · 항목당 색인 메모리 · 무작위 100 만 번 `hasFile` 의 ns 입니다.
 * @details 값: `Pack.Scale.mountMs`(문턱 60 ms 이하이고 고치기 전의 절반 이하), `Pack.Scale.indexBytesPerEntry`(48 B + 풀 바이트 이하),
 *          `Pack.Scale.lookupNs`(고치기 전 해시 맵의 1.5 배 이내 — 넘으면 앞 8 비트 버킷 표).
 */
SW_TEST_CASE( PackScaleTest, Index300k, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다", ._timeoutSeconds = 900 )
{
    using Internal              = PackScaleInternal;
    const AbsolutePath packPath = test::makeScratchPath( "index300k.pack" );
    vector<uint64>     listHash;
    SW_REQUIRE_TRUE( Internal::writeIndexPack( packPath, listHash ) );

    vector<int64> listMountNanos;
    for ( uint32 repeat = 0; repeat < Internal::kMountRepeatCount; ++repeat )
    {
        PackReader      reader;
        const Stopwatch watch;
        SW_REQUIRE_TRUE( reader.open( packPath.str() ) );
        listMountNanos.push_back( watch.getElapsedNanoseconds() );
    }
    VectorUtil::sort( listMountNanos );
    const int64 mountMedianNanos = listMountNanos[listMountNanos.size() / 2];

    const uint64 liveBefore = Internal::readLiveBytes();
    PackReader   reader;
    SW_REQUIRE_TRUE( reader.open( packPath.str() ) );
    const uint64 liveAfter = Internal::readLiveBytes();
    SW_EXPECT_EQUAL( reader.getFileCount(), Internal::kIndexEntryCount );

    // 절반은 있는 해시, 절반은 없는 해시(있는 것에 1 을 더한 값 — 충돌은 사실상 없다).
    vector<uint64> listQuery;
    listQuery.reserve( Internal::kLookupCount );
    uint64 state = 0xD1B54A32D192ED03ull;
    for ( uint32 index = 0; index < Internal::kLookupCount; ++index )
    {
        const uint64 hash = listHash[Internal::nextRandom( state ) % listHash.size()];
        listQuery.push_back( ( index & 1u ) == 0 ? hash : hash + 1 );
    }
    uint32          hitCount = 0;
    const Stopwatch lookupWatch;
    for ( const uint64 hash : listQuery )
    {
        hitCount += reader.hasFile( hash ) ? 1u : 0u;
    }
    const int64 lookupNanos = lookupWatch.getElapsedNanoseconds();
    SW_EXPECT_TRUE( hitCount >= Internal::kLookupCount / 2 );

    const uint64 indexBytesPerEntry = liveAfter > liveBefore ? ( liveAfter - liveBefore ) / Internal::kIndexEntryCount : 0;
    SW_LOG_INFO_ID( "Pack.Scale.mountMs", "Pack.Scale.mountMs %# (median of %#, %# entries)", mountMedianNanos / 1000000, Internal::kMountRepeatCount, Internal::kIndexEntryCount );
    SW_LOG_INFO_ID( "Pack.Scale.indexBytesPerEntry", "Pack.Scale.indexBytesPerEntry %# (live bytes %# -> %#, 0 when no memory profiler)", indexBytesPerEntry, liveBefore,
                    liveAfter );
    SW_LOG_INFO_ID( "Pack.Scale.lookupNs", "Pack.Scale.lookupNs %# (%# hasFile, %# hits)", lookupNanos / Internal::kLookupCount, Internal::kLookupCount, hitCount );
    reader.close();
    SW_EXPECT_TRUE( FileUtil::removeFile( packPath ) );
}

/**
 * @brief [PackScaleTest] 1 GiB 패치 팩의 서명 확인 비용입니다 — 색인 서명(PK-5) 뒤에는 여는 핸들로 색인 구역만 요약한다(고치기 전은 팩 전체 읽기 + SHA-256).
 * @details 값: `Pack.Scale.patchVerifyMs`(문턱 10 ms 이하 — 색인 크기에만 비례). 암호 제공자(GameFramework OpenSSL)가 공개돼 있지 않으면 요약 없이 연 시간만 남긴다.
 */
SW_TEST_CASE( PackScaleTest, Patch1g, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다", ._timeoutSeconds = 900 )
{
    using Internal                        = PackScaleInternal;
    const AbsolutePath          packPath  = test::makeScratchPath( "patch1g.pack" );
    INetSecurityProvider* const pProvider = engine::findSubsystem<INetSecurityProvider>();
    PackWriterDesc              desc      = PackTestUtil::makeDesc( "" );
    desc._pDigestProvider                 = pProvider;
    vector<uint64>                  listHash;
    const optional<PackWriteResult> written = Internal::writeEntries( packPath, desc, vector<size_t>( Internal::kPatchEntryCount, Internal::kPatchEntryBytes ), listHash );
    SW_REQUIRE_TRUE( written.has_value() );

    const Stopwatch watch;
    PackReader      reader;
    if ( pProvider != nullptr )
    {
        const PackIndexTrust trust{ pProvider, *written->_indexDigest };
        SW_REQUIRE_TRUE( reader.open( packPath.str(), &trust ) );
    }
    else
    {
        SW_REQUIRE_TRUE( reader.open( packPath.str() ) );
    }
    const int64 verifyNanos = watch.getElapsedNanoseconds();

    SW_EXPECT_TRUE( written->_fileSize > static_cast<uint64>( Internal::kPatchEntryCount ) * Internal::kPatchEntryBytes );
    SW_LOG_INFO_ID( "Pack.Scale.patchVerifyMs", "Pack.Scale.patchVerifyMs %# (%# B pack, index %# B%#)", verifyNanos / 1000000, written->_fileSize, written->_fileSize - written->_indexOffset,
                    pProvider != nullptr ? " + SHA-256" : ", no security provider - opened without a digest" );
    reader.close();
    SW_EXPECT_TRUE( FileUtil::removeFile( packPath ) );
}
