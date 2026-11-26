#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/Shader/Compile/ShaderSourceRecord.h"

using namespace sw;

/**
 * @brief [ShaderSourceRecordTest] 쓰고 읽은 기록이 동작마다 같은 입력 목록 · 해시를 주고, 같은 입력 목록은 한 번만 든다
 */
SW_TEST_CASE( ShaderSourceRecordTest, RoundTripSharesEqualInputLists )
{
    CookInput header;
    header._source      = "engine/shaders/common.hlsli";
    header._contentHash = 7;
    CookInput missing;
    missing._kind   = CookInputKind::DiskFile;
    missing._source = "D:/x/none.hlsli";

    ShaderSourceRecord record;
    record.add( 10, 100, { header, missing } );
    record.add( 11, 101, { header, missing } );
    record.add( 12, 100, { header } );
    vector<uint8> bytes;
    record.write( bytes );

    ShaderSourceRecord loaded;
    SW_REQUIRE_TRUE( loaded.read( bytes ) );
    SW_EXPECT_EQUAL( size_t( 3 ), loaded.getEntryCount() );
    SW_EXPECT_EQUAL( size_t( 2 ), loaded.getInputLists().size() );
    SW_REQUIRE_TRUE( loaded.find( 11 ) != nullptr );
    SW_EXPECT_EQUAL( uint64( 101 ), loaded.find( 11 )->_bytecodeHash );
    const vector<CookInput> listExpected{ header, missing };
    SW_EXPECT_TRUE( loaded.getInputLists()[loaded.find( 11 )->_inputListIndex] == listExpected );
    SW_EXPECT_TRUE( loaded.find( 13 ) == nullptr );

    bytes.pop_back();
    SW_EXPECT_FALSE( loaded.read( bytes ) );
}

/** @brief [ShaderSourceRecordTest] 있던 기록에서 입력이 지금도 맞는 항목만 이어받고, 서술은 소스 · 형식 · 도구 · 소스 내용이 하나라도 다르면 다르다 */
SW_TEST_CASE( ShaderSourceRecordTest, FreshEntriesCarryOver )
{
    CookInput header;
    header._source = "engine/shaders/common.hlsli";
    CookInput other;
    other._source = "engine/shaders/other.hlsli";
    ShaderSourceRecord old;
    old.add( 1, 100, { header } );
    old.add( 2, 200, { other } );
    ShaderSourceRecord record;
    record.addFresh( old, { 1, 0 } );
    record.add( 3, 300, { header } );
    SW_EXPECT_EQUAL( size_t( 2 ), record.getEntryCount() );
    SW_EXPECT_EQUAL( size_t( 1 ), record.getInputLists().size() );
    SW_EXPECT_TRUE( record.find( 1 ) != nullptr && record.find( 2 ) == nullptr && record.find( 3 ) != nullptr );

    const string description = ShaderSourceRecord::makeKeyDescription( "engine/shaders/a.hlsl", "dx12", 1, 2 );
    SW_EXPECT_TRUE( description != ShaderSourceRecord::makeKeyDescription( "engine/shaders/b.hlsl", "dx12", 1, 2 ) );
    SW_EXPECT_TRUE( description != ShaderSourceRecord::makeKeyDescription( "engine/shaders/a.hlsl", "vulkan", 1, 2 ) );
    SW_EXPECT_TRUE( description != ShaderSourceRecord::makeKeyDescription( "engine/shaders/a.hlsl", "dx12", 3, 2 ) );
    SW_EXPECT_TRUE( description != ShaderSourceRecord::makeKeyDescription( "engine/shaders/a.hlsl", "dx12", 1, 3 ) );
}
