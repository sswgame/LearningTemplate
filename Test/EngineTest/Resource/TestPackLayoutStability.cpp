#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Resource/Pack/ResourcePakReader.h"
#include "Engine/Resource/Pack/ResourcePakTypes.h"
#include "Engine/Resource/Pack/ResourcePakWriter.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestTempPath.h"

#include <cstring>
#include <unordered_map>

/**
 * @file TestPackLayoutStability.cpp
 * @brief 팩 바이트 배치의 안정성을 굳히는 테스트입니다 — 출시는 스팀 기준이라 빌드 사이에 바뀌지 않은 항목의 팩 바이트가 그대로여야 업데이트가 작다.
 * @details - 결정성: 같은 입력이면 담는 순서가 달라도 팩 바이트가 같다.
 *          - 항목 하나만 바뀌면 그 항목의 페이로드와 FAT 칸 밖 바이트는 그대로다(같은 섹터 안에서 크기가 바뀌어도 뒤 항목이 밀리지 않는다).
 *          - 항목을 더해도 바뀌지 않은 항목의 페이로드 바이트는 그대로다(자리는 밀릴 수 있다 — 경로 해시 순 배치라 삽입 지점 뒤가 밀린다).
 *          측정 숫자와 이 배치의 한계는 docs/decisions/5-9_OnlineNet.md P7 이 기준이다.
 */
namespace sw
{
    namespace
    {
        struct TestFile
        {
            string        _path;
            vector<uint8> _bytes;
        };

        /** @brief 압축이 반쯤 먹는 본문입니다(절반은 반복 · 절반은 난수). */
        vector<uint8> makeContent( uint64 seed, size_t size )
        {
            uint64        state = seed * 0x9E3779B97F4A7C15ull + 1;
            vector<uint8> bytes( size );
            for ( size_t index = 0; index < size; ++index )
            {
                state ^= state << 13;
                state ^= state >> 7;
                state ^= state << 17;
                bytes[index] = ( index / 64 ) % 2 == 0 ? static_cast<uint8>( index % 251 ) : static_cast<uint8>( state );
            }
            return bytes;
        }

        /** @brief 크기가 제각각인 파일 @p count 개입니다. */
        vector<TestFile> makeFiles( uint32 count )
        {
            vector<TestFile> list;
            for ( uint32 index = 0; index < count; ++index )
            {
                TestFile file;
                file._path  = "assets/dir" + std::to_string( index % 7 ) + "/file_" + std::to_string( index ) + ".bin";
                file._bytes = makeContent( index + 1, 300 + static_cast<size_t>( index ) * 977 % 30000 );
                list.push_back( std::move( file ) );
            }
            return list;
        }

        [[nodiscard]] bool makePack( const vector<TestFile>& listFile, PackCompressionType compression, bool bReverse, vector<uint8>& outBytes, ResourcePakWriteStats* pOutStats = nullptr )
        {
            ResourcePakWriterDesc desc;
            desc._compression = compression;
            ResourcePakWriter writer( desc );
            for ( size_t index = 0; index < listFile.size(); ++index )
            {
                const TestFile& file = bReverse ? listFile[listFile.size() - 1 - index] : listFile[index];
                writer.addBytes( file._path, file._bytes );
            }
            return writer.makePackBytes( outBytes, pOutStats );
        }

        /** @brief 팩의 FAT 를 경로 해시로 찾아 읽습니다. */
        [[nodiscard]] bool findEntry( const vector<uint8>& pack, const string& path, PackFileEntryOnDisk& outEntry, size_t& outEntryFileOffset )
        {
            PackHeader header{};
            Memory::copy( &header, pack.data(), sizeof( PackHeader ) );
            const uint64 hash = HashUtil::computeStringHash64( path );
            for ( uint32 index = 0; index < header._fileCount; ++index )
            {
                outEntryFileOffset = static_cast<size_t>( header._indexOffset ) + static_cast<size_t>( index ) * sizeof( PackFileEntryOnDisk );
                Memory::copy( &outEntry, pack.data() + outEntryFileOffset, sizeof( PackFileEntryOnDisk ) );
                if ( outEntry._pathHash == hash )
                    return true;
            }
            return false;
        }

        /** @brief 항목 하나의 페이로드 바이트입니다. */
        [[nodiscard]] vector<uint8> readPayload( const vector<uint8>& pack, const string& path )
        {
            PackFileEntryOnDisk entry{};
            size_t              entryOffset{ 0 };
            if ( findEntry( pack, path, entry, entryOffset ) == false )
                return {};
            return vector<uint8>( pack.begin() + static_cast<std::ptrdiff_t>( entry._dataOffset ),
                                  pack.begin() + static_cast<std::ptrdiff_t>( entry._dataOffset + entry._compressedSize ) );
        }
        /** @brief `Slot` 배치로 팩 바이트를 만듭니다. @p previousPath 가 비면 앞 빌드가 없는 첫 배치입니다. */
        [[nodiscard]] bool makeSlotPack( const vector<TestFile>& listFile, PackCompressionType compression, const string& previousPath, uint32 maxHolePercent, vector<uint8>& outBytes )
        {
            ResourcePakWriterDesc desc;
            desc._compression      = compression;
            desc._layoutMode       = PackLayoutMode::Slot;
            desc._previousPackPath = previousPath;
            desc._maxHolePercent   = maxHolePercent;
            ResourcePakWriter writer( desc );
            for ( const TestFile& file : listFile )
            {
                writer.addBytes( file._path, file._bytes );
            }
            return writer.makePackBytes( outBytes );
        }
    } // namespace
} // namespace sw

using namespace sw;

/** @brief [PackLayoutTest] 같은 입력이면 담는 순서와 상관없이 팩 바이트가 같은가(경로 해시 순 배치). */
SW_TEST_CASE( PackLayoutTest, SameInputGivesIdenticalBytesRegardlessOfAddOrder )
{
    const vector<TestFile> listFile = makeFiles( 60 );
    for ( PackCompressionType type : { PackCompressionType::None, PackCompressionType::Zlib, PackCompressionType::LZ4, PackCompressionType::Zstd } )
    {
        vector<uint8> first;
        vector<uint8> second;
        vector<uint8> reversed;
        SW_ASSERT_TRUE( makePack( listFile, type, false, first ) );
        SW_ASSERT_TRUE( makePack( listFile, type, false, second ) );
        SW_ASSERT_TRUE( makePack( listFile, type, true, reversed ) );
        SW_EXPECT_TRUE( first == second );
        SW_EXPECT_TRUE( first == reversed );
    }
}

/**
 * @brief [PackLayoutTest] 압축 · CRC 를 워커에서 병렬로 돌려도 팩 바이트가 직렬로 돌린 것과 비트 단위로 같은가
 * @details 직렬은 엔진 서비스를 푼 범위(`engine::runParallel` 이 호출 스레드에서 차례로 돈다)이고, 병렬은 워커 넷을 가진 태스크 매니저를 묶은 범위다. 빈 항목과 큰 항목이 섞여 있고
 *          합이 병렬 문턱(`kMinParallelBytes`)을 넘는다. 단계별 시간 통계의 항목 수 · 입력 바이트도 둘이 같아야 한다.
 */
SW_TEST_CASE( PackLayoutTest, ParallelCompressionGivesTheSerialBytes )
{
    vector<TestFile> listFile = makeFiles( 80 );
    listFile.push_back( { "assets/empty/zero.bin", {} } );
    listFile.push_back( { "assets/big/large.bin", makeContent( 777, 400000 ) } );

    for ( PackCompressionType type : { PackCompressionType::None, PackCompressionType::Zlib, PackCompressionType::LZ4, PackCompressionType::Zstd } )
    {
        vector<uint8>         serial;
        ResourcePakWriteStats serialStats;
        {
            const test::ScopedUnboundEngineServices unbound;
            SW_ASSERT_TRUE( makePack( listFile, type, false, serial, &serialStats ) );
        }

        vector<uint8>         parallel;
        ResourcePakWriteStats parallelStats;
        {
            test::ScopedEngineServiceOverride scope;
            TaskManager&                      tasks = scope.add( sw::make_unique<TaskManager>() );
            SW_ASSERT_TRUE( tasks.initialize( 4 ) );
            SW_ASSERT_TRUE( scope.bind() == nullptr );
            SW_ASSERT_TRUE( tasks.getWorkerCount() > 0 );
            SW_ASSERT_TRUE( makePack( listFile, type, false, parallel, &parallelStats ) );
            // 담는 순서를 뒤집어도 같다(정렬 키가 경로 해시).
            vector<uint8> parallelReversed;
            SW_ASSERT_TRUE( makePack( listFile, type, true, parallelReversed, nullptr ) );
            SW_EXPECT_TRUE( parallelReversed == serial );
        }

        SW_EXPECT_TRUE( parallel == serial );
        SW_EXPECT_EQUAL( parallelStats._entryCount, serialStats._entryCount );
        SW_EXPECT_EQUAL( parallelStats._inputBytes, serialStats._inputBytes );
        SW_EXPECT_EQUAL( serialStats._entryCount, static_cast<uint32>( listFile.size() ) );
    }
}

/** @brief [PackLayoutTest] 항목 하나의 본문이 바뀌면 그 항목의 페이로드 · FAT 칸 · 헤더 밖 바이트는 그대로인가(같은 섹터 안 크기 변경 포함). */
SW_TEST_CASE( PackLayoutTest, EditingOneEntryLeavesEveryOtherByteInPlace )
{
    const vector<TestFile> listBase = makeFiles( 60 );
    const size_t           target   = 25;
    for ( PackCompressionType type : { PackCompressionType::None, PackCompressionType::Zstd } )
    {
        vector<uint8> base;
        SW_ASSERT_TRUE( makePack( listBase, type, false, base ) );

        for ( int32 edit = 0; edit < 2; ++edit )
        {
            vector<TestFile> listNext = listBase;
            if ( edit == 0 )
                listNext[target]._bytes[10] ^= 0xFF; // 같은 크기
            else
                listNext[target]._bytes.resize( listNext[target]._bytes.size() + 7, 9 ); // 같은 섹터 안에서 늘어난다(None)
            vector<uint8> next;
            SW_ASSERT_TRUE( makePack( listNext, type, false, next ) );
            // 압축 크기가 섹터를 넘어 달라지면 뒤가 밀린다 — 이 테스트는 밀리지 않는 경우만 본다. 압축 없는 팩은 밀리지 않아야 한다(입력이 그렇게 고른 것).
            if ( type == PackCompressionType::None )
                SW_ASSERT_EQUAL( base.size(), next.size() );
            if ( base.size() != next.size() )
                continue;

            PackFileEntryOnDisk entry{};
            size_t              entryOffset{ 0 };
            SW_ASSERT_TRUE( findEntry( next, listNext[target]._path, entry, entryOffset ) );
            size_t outside = 0;
            for ( size_t index = 0; index < base.size(); ++index )
            {
                const bool bInHeader    = index < sizeof( PackHeader );
                const bool bInEntry     = index >= entryOffset && index < entryOffset + sizeof( PackFileEntryOnDisk );
                const bool bInPayload   = index >= entry._dataOffset && index < entry._dataOffset + entry._compressedSize;
                const bool bOwnedByEdit = bInHeader || bInEntry || bInPayload;
                if ( bOwnedByEdit == false && base[index] != next[index] )
                    ++outside;
            }
            SW_EXPECT_EQUAL( outside, static_cast<size_t>( 0 ) );
        }
    }
}

/** @brief [PackLayoutTest] 항목을 더하거나 지워도 남은 항목의 페이로드 바이트는 그대로인가(자리는 밀려도 내용 기준 청크 비교에는 같은 청크로 남는다). */
SW_TEST_CASE( PackLayoutTest, AddingOrRemovingAnEntryKeepsOtherPayloadBytes )
{
    const vector<TestFile> listBase = makeFiles( 60 );
    for ( PackCompressionType type : { PackCompressionType::None, PackCompressionType::Zstd } )
    {
        vector<uint8> base;
        SW_ASSERT_TRUE( makePack( listBase, type, false, base ) );

        vector<TestFile> listAdded = listBase;
        listAdded.push_back( { "assets/added/new.bin", makeContent( 4242, 5000 ) } );
        vector<TestFile> listRemoved = listBase;
        listRemoved.erase( listRemoved.begin() + 30 );

        vector<uint8> added;
        vector<uint8> removed;
        SW_ASSERT_TRUE( makePack( listAdded, type, false, added ) );
        SW_ASSERT_TRUE( makePack( listRemoved, type, false, removed ) );
        for ( size_t index = 0; index < listBase.size(); ++index )
        {
            const vector<uint8> payload = readPayload( base, listBase[index]._path );
            SW_EXPECT_FALSE( payload.empty() );
            SW_EXPECT_TRUE( payload == readPayload( added, listBase[index]._path ) );
            if ( index != 30 )
                SW_EXPECT_TRUE( payload == readPayload( removed, listBase[index]._path ) );
        }
    }
}

/**
 * @brief [PackLayoutTest] 슬롯 배치는 앞 빌드의 팩이 있으면 항목을 더하거나 지워도 안 바뀐 항목의 오프셋 · 페이로드가 그대로이고 모든 항목을 읽을 수 있으며,
 *        빈 자리가 한도를 넘으면 앞 빌드 없이 만든 것과 같은 촘촘한 배치로 돌아간다
 */
SW_TEST_CASE( PackLayoutTest, SlotLayoutKeepsUnchangedEntriesInPlaceWhenEntriesAreAddedOrRemoved )
{
    const string           folder       = test::makeTempDirectory( "packslotlayout" );
    const string           previousPath = FileUtil::joinPath( folder, "previous.pack" );
    const string           nextPath     = FileUtil::joinPath( folder, "next.pack" );
    const vector<TestFile> listBase     = makeFiles( 60 );
    for ( PackCompressionType type : { PackCompressionType::None, PackCompressionType::Zstd } )
    {
        vector<uint8> base;
        SW_ASSERT_TRUE( makeSlotPack( listBase, type, "", 25, base ) ); // 앞 빌드가 없는 첫 배치
        SW_ASSERT_TRUE( FileUtil::writeFile( previousPath, base.data(), base.size() ) );

        vector<TestFile> listNext = listBase;
        listNext.erase( listNext.begin() + 30 );
        listNext.push_back( { "assets/added/new.bin", makeContent( 4242, 5000 ) } );

        vector<uint8> next;
        SW_ASSERT_TRUE( makeSlotPack( listNext, type, previousPath, 100, next ) );
        for ( size_t index = 0; index < listBase.size(); ++index )
        {
            if ( index == 30 )
                continue;
            PackFileEntryOnDisk baseEntry{};
            PackFileEntryOnDisk nextEntry{};
            size_t              entryOffset{ 0 };
            SW_ASSERT_TRUE( findEntry( base, listBase[index]._path, baseEntry, entryOffset ) );
            SW_ASSERT_TRUE( findEntry( next, listBase[index]._path, nextEntry, entryOffset ) );
            SW_EXPECT_EQUAL( baseEntry._dataOffset, nextEntry._dataOffset ); // 앞 빌드의 자리를 지킨다
            SW_EXPECT_TRUE( readPayload( base, listBase[index]._path ) == readPayload( next, listBase[index]._path ) );
        }
        SW_EXPECT_FALSE( readPayload( next, "assets/added/new.bin" ).empty() );

        // 리더가 다음 팩의 모든 항목을 읽는다(FAT 이 페이로드 뒤인 배치).
        SW_ASSERT_TRUE( FileUtil::writeFile( nextPath, next.data(), next.size() ) );
        ResourcePakReader reader;
        SW_ASSERT_TRUE( reader.open( nextPath ) );
        SW_EXPECT_EQUAL( static_cast<uint32>( listNext.size() ), reader.getFileCount() );
        for ( const TestFile& file : listNext )
        {
            vector<uint8> readBack;
            SW_ASSERT_TRUE( reader.readFile( file._path, readBack ) );
            SW_EXPECT_TRUE( readBack == file._bytes );
        }
        reader.close();

        // 빈 자리 한도 0 % 에서는 지운 항목의 빈 자리가 한도를 넘어 앞 빌드를 버리고, 앞 빌드 없이 만든 것과 같은 바이트가 된다.
        vector<uint8> compact;
        vector<uint8> fresh;
        SW_ASSERT_TRUE( makeSlotPack( listNext, type, previousPath, 0, compact ) );
        SW_ASSERT_TRUE( makeSlotPack( listNext, type, "", 0, fresh ) );
        SW_EXPECT_TRUE( compact == fresh );
    }
}
