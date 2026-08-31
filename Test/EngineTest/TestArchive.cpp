#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Save/ISaveGame.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// Engine_Archive — 바이너리 스트림·TLV·체크섬·수학 타입·세이브 슬롯 직렬화 검증
// ------------------------------------------------------------------------------
/**
 * @brief [Engine_Archive] Archive 바이너리 기본 타입 직렬화/역직렬화
 */

SW_TEST_CASE( Engine_Archive, MemoryArchiveBinarySerialization )
{
	sw::Archive arch;

	int32	intVal	 = 42;
	float32 floatVal = 3.14159f;

	arch << intVal << floatVal;
	arch.setReadModeAndResetPos( true );

	int32	readInt{ 0 };
	float32 readFloat{ 0.0f };

	arch >> readInt >> readFloat;

	SW_EXPECT_EQUAL( 42, readInt );
	SW_EXPECT_NEAR_EQUAL( 3.14159f, readFloat, 1e-4f );
}

/**
 * @brief [Engine_Archive] 모든 원시 자료형(bool, u8~u64, i8~i64, f32, f64, string) 직렬화 라운드트립
 */
SW_TEST_CASE( Engine_Archive, ArchiveAllPrimitiveTypesStreaming )
{
	sw::Archive writeArch;

	const bool		 inBool		= true;
	const uint8		 inU8		= 0xEFu;
	const uint16	 inU16		= 0xBEEFu;
	const uint32	 inU32		= 0xDEADBEEFu;
	const uint64	 inU64		= 0x0123456789ABCDEFull;
	const int8		 inI8		= -120;
	const int16		 inI16		= -32000;
	const int32		 inI32		= -12345678;
	const int64		 inI64		= -987654321012345ll;
	const float32	 inF32		= -123.456f;
	const float64	 inF64		= 9876543.2109876;
	const sw::string inStrEmpty = "";
	const sw::string inStrText	= "SW_Engine_Archive_Stream_Test_12345";

	writeArch << inBool << inU8 << inU16 << inU32 << inU64;
	writeArch << inI8 << inI16 << inI32 << inI64;
	writeArch << inF32 << inF64;
	writeArch << string_view( inStrEmpty ) << string_view( inStrText );

	sw::vector<uint8> rawBytes;
	writeArch.writeData( rawBytes );
	SW_EXPECT_FALSE( rawBytes.empty() );

	sw::Archive readArch( rawBytes.data(), rawBytes.size() );
	SW_EXPECT_TRUE( readArch.isReadMode() );
	SW_EXPECT_EQUAL( static_cast<uint64>( rawBytes.size() ), readArch.getSize() );
	SW_EXPECT_EQUAL( 0ull, readArch.getOffset() );

	bool	   outBool{ false };
	uint8	   outU8{ 0 };
	uint16	   outU16{ 0 };
	uint32	   outU32{ 0 };
	uint64	   outU64{ 0 };
	int8	   outI8{ 0 };
	int16	   outI16{ 0 };
	int32	   outI32{ 0 };
	int64	   outI64{ 0 };
	float32	   outF32{ 0.0f };
	float64	   outF64{ 0.0 };
	sw::string outStrEmpty{ "dummy" };
	sw::string outStrText;

	readArch >> outBool >> outU8 >> outU16 >> outU32 >> outU64;
	readArch >> outI8 >> outI16 >> outI32 >> outI64;
	readArch >> outF32 >> outF64;
	readArch >> outStrEmpty >> outStrText;

	SW_EXPECT_EQUAL( inBool, outBool );
	SW_EXPECT_EQUAL( inU8, outU8 );
	SW_EXPECT_EQUAL( inU16, outU16 );
	SW_EXPECT_EQUAL( inU32, outU32 );
	SW_EXPECT_EQUAL( inU64, outU64 );
	SW_EXPECT_EQUAL( inI8, outI8 );
	SW_EXPECT_EQUAL( inI16, outI16 );
	SW_EXPECT_EQUAL( inI32, outI32 );
	SW_EXPECT_EQUAL( inI64, outI64 );
	SW_EXPECT_NEAR_EQUAL( inF32, outF32, 1e-4f );
	SW_EXPECT_NEAR_EQUAL( inF64, outF64, 1e-6 );
	SW_EXPECT_TRUE( outStrEmpty.empty() );
	SW_EXPECT_EQUAL( inStrText, outStrText );
	SW_EXPECT_EQUAL( readArch.getSize(), readArch.getOffset() );
}

/**
 * @brief [Engine_Archive] 수학 벡터/행렬(float2, float3, float4, float4x4) 직렬화 검증
 */
SW_TEST_CASE( Engine_Archive, ArchiveMathTypesStreaming )
{
	sw::Archive writeArch;

	const sw::float2   inV2{ 1.5f, -2.5f };
	const sw::float3   inV3{ 10.0f, 20.0f, 30.0f };
	const sw::float4   inV4{ 0.1f, 0.2f, 0.3f, 0.4f };
	const sw::float4x4 inM4{
		1.0f, 2.0f, 3.0f, 4.0f,
		5.0f, 6.0f, 7.0f, 8.0f,
		9.0f, 10.0f, 11.0f, 12.0f,
		13.0f, 14.0f, 15.0f, 16.0f };

	writeArch << inV2 << inV3 << inV4 << inM4;

	sw::Archive readArch( writeArch.getData(), writeArch.getSize() );

	sw::float2	 outV2{};
	sw::float3	 outV3{};
	sw::float4	 outV4{};
	sw::float4x4 outM4{};

	readArch >> outV2 >> outV3 >> outV4 >> outM4;

	SW_EXPECT_NEAR_EQUAL( inV2._x, outV2._x, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inV2._y, outV2._y, 1e-5f );

	SW_EXPECT_NEAR_EQUAL( inV3._x, outV3._x, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inV3._y, outV3._y, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inV3._z, outV3._z, 1e-5f );

	SW_EXPECT_NEAR_EQUAL( inV4._x, outV4._x, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inV4._y, outV4._y, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inV4._z, outV4._z, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inV4._w, outV4._w, 1e-5f );

	SW_EXPECT_NEAR_EQUAL( inM4._11, outM4._11, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._12, outM4._12, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._13, outM4._13, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._14, outM4._14, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._21, outM4._21, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._22, outM4._22, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._23, outM4._23, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._24, outM4._24, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._31, outM4._31, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._32, outM4._32, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._33, outM4._33, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._34, outM4._34, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._41, outM4._41, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._42, outM4._42, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._43, outM4._43, 1e-5f );
	SW_EXPECT_NEAR_EQUAL( inM4._44, outM4._44, 1e-5f );
}

/**
 * @brief [Engine_Archive] 디스크 파일 저장/로드(saveFile 및 Archive(file, true)) 라운드트립
 */
SW_TEST_CASE( Engine_Archive, ArchiveFileIORoundTrip )
{
	const sw::string tempFilePath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_archive_io.bin" );

	sw::Archive writeArch;
	writeArch << 0x12345678u;
	writeArch << sw::string( "TempArchiveFilePayload" );
	writeArch << -999.5f;

	SW_EXPECT_TRUE( writeArch.saveFile( tempFilePath ) );
	SW_EXPECT_TRUE( sw::FileUtil::fileExists( tempFilePath ) );

	sw::Archive readArch( tempFilePath, true );
	SW_EXPECT_TRUE( readArch.isReadMode() );
	SW_EXPECT_TRUE( readArch.getSize() > 0 );

	uint32	   outMagic{ 0 };
	sw::string outText;
	float32	   outVal{ 0.0f };

	readArch >> outMagic >> outText >> outVal;

	SW_EXPECT_EQUAL( 0x12345678u, outMagic );
	SW_EXPECT_EQUAL( sw::string( "TempArchiveFilePayload" ), outText );
	SW_EXPECT_NEAR_EQUAL( -999.5f, outVal, 1e-4f );

	sw::FileUtil::removeFile( tempFilePath );
}

/**
 * @brief [Engine_Archive] CRC32 체크섬 계산 및 데이터 변조(Corruption) 감지 검증
 */
SW_TEST_CASE( Engine_Archive, ArchiveChecksumAndCorruptionDetection )
{
	sw::Archive writeArch;
	writeArch << sw::string( "SafeSecurePayloadData" );
	writeArch << 424242;

	const uint32 crc = writeArch.calculateChecksum();
	SW_EXPECT_TRUE( crc != 0 );

	writeArch.writeChecksum();
	SW_EXPECT_TRUE( writeArch.validateChecksum() );

	// 1) 정상 역직렬화
	sw::vector<uint8> buffer;
	writeArch.writeData( buffer );

	sw::Archive normalArch( buffer.data(), buffer.size() );
	SW_EXPECT_TRUE( normalArch.validateChecksum() );

	// 2) 바이트 변조 시뮬레이션
	buffer[4] ^= 0xFF; // 페이로드 내부 1바이트 반전
	const uint32 tamperedCrc = sw::StringUtil::computeCrc32( buffer.data(), buffer.size() - sizeof( uint32 ) );
	SW_EXPECT_TRUE( crc != tamperedCrc );
}

/**
 * @brief [Engine_Archive] 읽기 범위 초과(Out of Bounds) 및 에러 경계 검증
 */
SW_TEST_CASE( Engine_Archive, ArchiveBoundaryAndErrorHandling )
{
	sw::Archive emptyArch;
	emptyArch.setReadModeAndResetPos( true );

	int32	   val{ 0 };
	const bool bReadOk = emptyArch.readBytes( &val, sizeof( int32 ) );
	SW_EXPECT_FALSE( bReadOk );

	sw::Archive smallArch;
	smallArch << static_cast<uint8>( 7 );
	smallArch.setReadModeAndResetPos( true );

	uint32	   largeVal{ 0 };
	const bool bLargeReadOk = smallArch.readBytes( &largeVal, sizeof( uint32 ) );
	SW_EXPECT_FALSE( bLargeReadOk );
}

/**
 * @brief [Engine_Archive] Archive 객체 TLV 직렬화
 */
SW_TEST_CASE( Engine_Archive, ArchiveObjectTLVSerialization )
{
	struct DummyStruct
	{
		int32 _valA = 123;
		int32 _valB = 456;
	};

	sw::TypeInfo info;
	info._name				 = sw::hashed_string( "DummyStruct" );
	info._fullyQualifiedName = sw::hashed_string( "sw::DummyStruct" );
	info._size				 = sizeof( DummyStruct );
	info._listProperty		 = {
		  {sw::hashed_string( "_valA" ), sw::hashed_string( "int32" ),
			SW_OFFSET_OF( DummyStruct, _valA ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
		  {sw::hashed_string( "_valB" ), sw::hashed_string( "int32" ),
			SW_OFFSET_OF( DummyStruct, _valB ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr}
	};

	sw::Archive archWrite;
	DummyStruct src;
	bool		writeOk = archWrite.serializeObject( &src, info );
	SW_EXPECT_TRUE( writeOk );

	sw::vector<uint8> data;
	archWrite.writeData( data );

	sw::Archive archRead( data.data(), data.size() );
	archRead.setReadModeAndResetPos( true );

	sw::TypeInfo infoReordered = info;
	std::swap( infoReordered._listProperty[0], infoReordered._listProperty[1] );

	DummyStruct dst;
	dst._valA	= 0;
	dst._valB	= 0;
	bool readOk = archRead.deserializeObject( &dst, infoReordered );
	SW_EXPECT_TRUE( readOk );
	SW_EXPECT_EQUAL( 123, dst._valA );
	SW_EXPECT_EQUAL( 456, dst._valB );
}

/**
 * @brief [Engine_Archive] SaveSlot 바이너리 직렬화/역직렬화(SAV1 포맷 + CRC32) 라운드트립 검증
 */
SW_TEST_CASE( Engine_Archive, SaveSlotBinaryArchiveRoundTrip )
{
	const sw::string savePath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_save_slot.sav" );

	sw::SaveSlot writeSlot;
	writeSlot._mapPath = "Resource/game/demo/scenes/overworld.scene.xml";
	writeSlot._playerX = 150;
	writeSlot._playerY = -300;
	writeSlot.setFlag( "quest_dragon_defeated", 1 );
	writeSlot.setFlag( "gold_coins", 9999 );
	writeSlot.setFlag( "current_chapter", 3 );

	SW_EXPECT_TRUE( writeSlot.saveCommonToFile( savePath ) );
	SW_EXPECT_TRUE( sw::FileUtil::fileExists( savePath ) );

	sw::SaveSlot readSlot;
	SW_EXPECT_TRUE( readSlot.loadCommonFromFile( savePath ) );

	SW_EXPECT_EQUAL( writeSlot._mapPath, readSlot._mapPath );
	SW_EXPECT_EQUAL( 150, readSlot._playerX );
	SW_EXPECT_EQUAL( -300, readSlot._playerY );
	SW_EXPECT_EQUAL( 1, readSlot.getFlag( "quest_dragon_defeated" ) );
	SW_EXPECT_EQUAL( 9999, readSlot.getFlag( "gold_coins" ) );
	SW_EXPECT_EQUAL( 3, readSlot.getFlag( "current_chapter" ) );
	SW_EXPECT_EQUAL( 0, readSlot.getFlag( "non_existent_flag", 0 ) );

	sw::FileUtil::removeFile( savePath );
}

/**
 * @brief [Engine_Archive] SaveSlot 변조된 바이너리 세이브 파일 거부(CRC mismatch rejection) 검증
 */
SW_TEST_CASE( Engine_Archive, SaveSlotBinaryTamperRejection )
{
	const sw::string savePath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "tampered_save_slot.sav" );

	sw::SaveSlot writeSlot;
	writeSlot._mapPath = "world_level_1";
	writeSlot._playerX = 10;
	writeSlot._playerY = 20;
	writeSlot.setFlag( "admin_level", 0 );
	SW_EXPECT_TRUE( writeSlot.saveCommonToBinaryFile( savePath ) );

	// 파일 읽어서 페이로드 바이트 임의 변조
	sw::vector<uint8> saveBytes;
	SW_ASSERT_TRUE( sw::FileUtil::readFile( savePath, saveBytes ) );
	SW_ASSERT_TRUE( saveBytes.size() > 20 );

	saveBytes[saveBytes.size() - 2] ^= 0x7F; // 플래그 값 변조
	SW_ASSERT_TRUE( sw::FileUtil::writeFile( savePath, saveBytes.data(), saveBytes.size() ) );

	// 변조된 파일 로드시 CRC 불일치로 실패해야 함
	sw::SaveSlot tamperedSlot;
	SW_EXPECT_FALSE( tamperedSlot.loadCommonFromBinaryFile( savePath ) );

	sw::FileUtil::removeFile( savePath );
}

/**
 * @brief [Engine_Archive] SceneDocument 및 PrefabAsset 바이너리 아카이브 라운드트립 검증
 */
SW_TEST_CASE( Engine_Archive, SceneAndPrefabBinaryArchiveRoundTrip )
{
	const sw::string tempSceneBin  = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "temp_test_scene.bin" );
	const sw::string tempPrefabBin = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "temp_test_prefab.bin" );

	// 1) SceneDocument 바이너리 저장 및 로드
	{
		sw::SceneDocument sceneWrite;
		sceneWrite._name = "BinaryTestScene";

		sw::SceneDocument::EntityNode node1{};
		node1._name		   = "Player";
		node1._prefab	   = "prefabs/player.prefab.xml";
		node1._prefabGuid  = "guid-1234-abcd";
		node1._embeddedXml = "<Transform x=\"10\" y=\"20\"/>";

		sw::SceneDocument::EntityNode node2{};
		node2._name		   = "Monster";
		node2._prefab	   = "prefabs/goblin.prefab.xml";
		node2._prefabGuid  = "guid-5678-ef01";
		node2._embeddedXml = "<Transform x=\"50\" y=\"60\"/>";

		sceneWrite._listEntityNode.push_back( node1 );
		sceneWrite._listEntityNode.push_back( node2 );

		SW_EXPECT_TRUE( sceneWrite.saveBinary( tempSceneBin ) );

		sw::SceneDocument sceneRead;
		SW_EXPECT_TRUE( sceneRead.loadBinary( tempSceneBin ) );
		SW_EXPECT_EQUAL( sw::string( "BinaryTestScene" ), sceneRead._name );
		SW_EXPECT_EQUAL( 2u, static_cast<uint32>( sceneRead._listEntityNode.size() ) );
		SW_EXPECT_EQUAL( node1._name, sceneRead._listEntityNode[0]._name );
		SW_EXPECT_EQUAL( node1._prefab, sceneRead._listEntityNode[0]._prefab );
		SW_EXPECT_EQUAL( node2._name, sceneRead._listEntityNode[1]._name );
		SW_EXPECT_EQUAL( node2._embeddedXml, sceneRead._listEntityNode[1]._embeddedXml );
	}

	// 2) PrefabAsset 바이너리 저장 및 로드
	{
		sw::GameObject	testObj( sw::hashed_string( "DragonBoss" ) );
		sw::PrefabAsset prefabWrite;
		prefabWrite.setFromGameObject( &testObj );
		SW_EXPECT_TRUE( prefabWrite.isValid() );

		SW_EXPECT_TRUE( prefabWrite.saveToBinaryFile( tempPrefabBin ) );

		sw::PrefabAsset prefabRead;
		SW_EXPECT_TRUE( prefabRead.loadFromBinaryFile( tempPrefabBin ) );
		SW_EXPECT_EQUAL( sw::string( "DragonBoss" ), prefabRead.getName() );
		SW_EXPECT_TRUE( prefabRead.getStateData().find( "DragonBoss" ) != sw::string::npos );
		SW_EXPECT_TRUE( prefabRead.isValid() );
	}

	sw::FileUtil::removeFile( tempSceneBin );
	sw::FileUtil::removeFile( tempPrefabBin );
}
