#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Compile/ShaderBatchRecord.h"

using namespace sw;

/**
 * @brief [ShaderBatchRecordTest] 쓰고 읽은 기록이 동작마다 같은 입력 목록 · 해시를 주고, 같은 입력 목록은 한 번만 든다
 */
SW_TEST_CASE( ShaderBatchRecordTest, RoundTripSharesEqualInputLists )
{
    CookInput header;
    header._source      = "engine/shaders/common.hlsli";
    header._contentHash = 7;
    CookInput missing;
    missing._kind   = CookInputKind::DiskFile;
    missing._source = "D:/x/none.hlsli";

    ShaderBatchRecord record;
    record.add( 10, 100, { header, missing } );
    record.add( 11, 101, { header, missing } );
    record.add( 12, 100, { header } );
    vector<uint8> bytes;
    record.write( bytes );

    ShaderBatchRecord loaded;
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

/** @brief [ShaderBatchRecordTest] 묶음의 서술은 동작 키의 차례 · 중복과 무관하고, 키가 하나라도 다르면 다르다 */
SW_TEST_CASE( ShaderBatchRecordTest, KeyDependsOnTheActionSet )
{
    const string description = ShaderBatchRecord::makeKeyDescription( { 3, 1, 2, 1 } );
    SW_EXPECT_EQUAL( description, ShaderBatchRecord::makeKeyDescription( { 1, 2, 3 } ) );
    SW_EXPECT_TRUE( description != ShaderBatchRecord::makeKeyDescription( { 1, 2 } ) );
    SW_EXPECT_TRUE( description != ShaderBatchRecord::makeKeyDescription( { 1, 2, 4 } ) );
}
