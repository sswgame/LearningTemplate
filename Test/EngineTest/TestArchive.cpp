#include "pch.h"

#include "Core/Common/VarIntUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Serialization/Core/BinaryStream.h"
#include "Engine/Serialization/Core/SerializerUtil.h"
#include "Engine/Serialization/Core/StringPool.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

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

/**
 * @brief [Engine_Archive] Archive 에러 추적 및 안전성 헬퍼 (isOk, isError, hasBytesAvailable, getRemainingBytes)
 */
SW_TEST_CASE( Engine_Archive, ArchiveErrorTrackingAndSafetyHelpers )
{
	sw::Archive writeArch;
	SW_EXPECT_TRUE( writeArch.isOk() );
	SW_EXPECT_FALSE( writeArch.isError() );
	SW_EXPECT_TRUE( static_cast<bool>( writeArch ) );

	writeArch << 12345 << sw::string( "Hello" );
	SW_EXPECT_TRUE( writeArch.isOk() );

	sw::Archive readArch( writeArch.getData(), writeArch.getSize() );
	SW_EXPECT_TRUE( readArch.isOk() );
	SW_EXPECT_EQUAL( writeArch.getSize(), readArch.getRemainingBytes() );
	SW_EXPECT_TRUE( readArch.hasBytesAvailable( sizeof( int32 ) ) );

	int32 val{ 0 };
	readArch >> val;
	SW_EXPECT_EQUAL( 12345, val );
	SW_EXPECT_TRUE( readArch.isOk() );

	// 남은 크기를 초과하는 데이터 읽기 시도 시 에러 플래그 설정 검증
	uint64 tooBig = 0;
	SW_EXPECT_FALSE( readArch.readBytes( &tooBig, 999999 ) );
	SW_EXPECT_TRUE( readArch.isError() );
	SW_EXPECT_FALSE( readArch.isOk() );
	SW_EXPECT_FALSE( static_cast<bool>( readArch ) );

	readArch.clearError();
	SW_EXPECT_TRUE( readArch.isOk() );
}

/**
 * @brief [Engine_Archive] Archive 섹션 및 서브 아카이브 분할 슬라이싱 검증
 */
SW_TEST_CASE( Engine_Archive, ArchiveSubArchiveAndSectionSlice )
{
	sw::Archive writeArch;

	// 1) 섹션 기록 (크기 접두사 + 페이로드)
	const sw::string sectionA = "HeaderSection_01";
	const sw::string sectionB = "PayloadBodySection_02_With_More_Data";

	writeArch.writeSection( sectionA.data(), static_cast<uint32>( sectionA.size() ) );
	writeArch.writeSection( nullptr, 0 ); // 빈 섹션
	writeArch.writeSection( sectionB.data(), static_cast<uint32>( sectionB.size() ) );

	sw::Archive readArch( writeArch.getData(), writeArch.getSize() );

	sw::vector<uint8> outSecA;
	SW_EXPECT_TRUE( readArch.readSection( outSecA ) );
	SW_EXPECT_EQUAL( sectionA.size(), outSecA.size() );
	SW_EXPECT_EQUAL( sectionA, sw::string( reinterpret_cast<const utf8*>( outSecA.data() ), outSecA.size() ) );

	sw::vector<uint8> outSecEmpty;
	SW_EXPECT_TRUE( readArch.readSection( outSecEmpty ) );
	SW_EXPECT_EQUAL( 0u, static_cast<uint32>( outSecEmpty.size() ) );

	sw::vector<uint8> outSecB;
	SW_EXPECT_TRUE( readArch.readSection( outSecB ) );
	SW_EXPECT_EQUAL( sectionB.size(), outSecB.size() );
	SW_EXPECT_EQUAL( sectionB, sw::string( reinterpret_cast<const utf8*>( outSecB.data() ), outSecB.size() ) );

	// 2) 서브 아카이브 분할 검증
	sw::Archive packetArch;
	packetArch << 100 << 200 << 300;

	sw::Archive streamArch( packetArch.getData(), packetArch.getSize() );
	sw::Archive sub = streamArch.readSubArchive( sizeof( int32 ) * 2 );
	SW_EXPECT_TRUE( sub.isOk() );
	SW_EXPECT_EQUAL( sizeof( int32 ) * 2, sub.getSize() );

	int32 a{ 0 };
	int32 b{ 0 };
	sub >> a >> b;
	SW_EXPECT_EQUAL( 100, a );
	SW_EXPECT_EQUAL( 200, b );

	int32 c{ 0 };
	streamArch >> c;
	SW_EXPECT_EQUAL( 300, c );
}

namespace
{
	struct TestReflectedPlayer
	{
		int32	   _level{ 1 };
		sw::string _name{};
		int64	   _gold{ 0 };

		static const sw::TypeInfo* StaticType()
		{
			static sw::TypeInfo s_typeInfo{};
			if ( s_typeInfo._name.empty() )
			{
				s_typeInfo._name			   = sw::hashed_string( "TestReflectedPlayer" );
				s_typeInfo._fullyQualifiedName = sw::hashed_string( "TestReflectedPlayer" );
				s_typeInfo._size			   = sizeof( TestReflectedPlayer );
				s_typeInfo._destroyInstance	   = []( void* pMemory )
				{
					static_cast<TestReflectedPlayer*>( pMemory )->~TestReflectedPlayer();
				};

				sw::FunctionInfo ctorInfo;
				ctorInfo._name	   = "$ctor";
				ctorInfo._hashName = sw::hashed_string( "$ctor" );
				ctorInfo._invoker  = []( void* pPtr, const sw::TaskArgs& ) -> sw::TaskValue
				{
					new ( pPtr ) TestReflectedPlayer();
					return sw::TaskValue{};
				};
				s_typeInfo._listMethod.push_back( ctorInfo );

				s_typeInfo._listProperty = {
					{sw::hashed_string( "_level" ),	 sw::hashed_string( "int32" ),
					  SW_OFFSET_OF( TestReflectedPlayer, _level ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
					{ sw::hashed_string( "_name" ), sw::hashed_string( "string" ),
					  SW_OFFSET_OF( TestReflectedPlayer,	 _name ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
					{ sw::hashed_string( "_gold" ),	sw::hashed_string( "int64" ),
					  SW_OFFSET_OF( TestReflectedPlayer,	 _gold ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr}
				  };
			}
			return &s_typeInfo;
		}
	};

	struct TestSparseReflectedConfig
	{
		int32 _v0{ 0 };
		int32 _v1{ 0 };
		int32 _v2{ 0 };
		int32 _v3{ 0 };
		int32 _v4{ 0 };
		int32 _v5{ 0 };
		int32 _v6{ 0 };
		int32 _v7{ 0 };

		static const sw::TypeInfo* StaticType()
		{
			static sw::TypeInfo s_typeInfo{};
			if ( s_typeInfo._name.empty() )
			{
				s_typeInfo._name			   = sw::hashed_string( "TestSparseReflectedConfig" );
				s_typeInfo._fullyQualifiedName = sw::hashed_string( "TestSparseReflectedConfig" );
				s_typeInfo._size			   = sizeof( TestSparseReflectedConfig );
				s_typeInfo._destroyInstance	   = []( void* pMemory )
				{
					static_cast<TestSparseReflectedConfig*>( pMemory )->~TestSparseReflectedConfig();
				};

				sw::FunctionInfo ctorInfo;
				ctorInfo._name	   = "$ctor";
				ctorInfo._hashName = sw::hashed_string( "$ctor" );
				ctorInfo._invoker  = []( void* pPtr, const sw::TaskArgs& ) -> sw::TaskValue
				{
					new ( pPtr ) TestSparseReflectedConfig();
					return sw::TaskValue{};
				};
				s_typeInfo._listMethod.push_back( ctorInfo );

				s_typeInfo._listProperty = {
					{sw::hashed_string( "_v0" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( TestSparseReflectedConfig, _v0 ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
					{sw::hashed_string( "_v1" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( TestSparseReflectedConfig, _v1 ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
					{sw::hashed_string( "_v2" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( TestSparseReflectedConfig, _v2 ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
					{sw::hashed_string( "_v3" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( TestSparseReflectedConfig, _v3 ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
					{sw::hashed_string( "_v4" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( TestSparseReflectedConfig, _v4 ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
					{sw::hashed_string( "_v5" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( TestSparseReflectedConfig, _v5 ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
					{sw::hashed_string( "_v6" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( TestSparseReflectedConfig, _v6 ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
					{sw::hashed_string( "_v7" ), sw::hashed_string( "int32" ), SW_OFFSET_OF( TestSparseReflectedConfig, _v7 ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr}
				};
			}
			return &s_typeInfo;
		}
	};
} // namespace

/**
 * @brief [Engine_Archive] Archive 바이트 벡터(vector<uint8>) 스트리밍 및 템플릿 직렬화 검증
 */
SW_TEST_CASE( Engine_Archive, ArchiveByteVectorAndTemplateObjectSerialization )
{
	sw::Archive writeArch;

	sw::vector<uint8> sampleBytes = { 0x01, 0x02, 0x03, 0x04, 0xFF, 0xFE, 0xFD };
	writeArch << sampleBytes;

	TestReflectedPlayer playerWrite;
	playerWrite._level = 99;
	playerWrite._name  = "HeroKnight";
	playerWrite._gold  = 9876543210123ll;
	writeArch.serializeObject( playerWrite );

	sw::Archive readArch( writeArch.getData(), writeArch.getSize() );

	sw::vector<uint8> outBytes;
	readArch >> outBytes;
	SW_EXPECT_EQUAL( sampleBytes.size(), outBytes.size() );
	SW_EXPECT_EQUAL( 0x01, outBytes[0] );
	SW_EXPECT_EQUAL( 0xFF, outBytes[4] );
	SW_EXPECT_EQUAL( 0xFD, outBytes[6] );

	TestReflectedPlayer playerRead;
	SW_EXPECT_TRUE( readArch.deserializeObject( playerRead ) );
	SW_EXPECT_EQUAL( 99, playerRead._level );
	SW_EXPECT_EQUAL( sw::string( "HeroKnight" ), playerRead._name );
	SW_EXPECT_EQUAL( 9876543210123ll, playerRead._gold );
}

/**
 * @brief [Engine_Archive] Archive 압축 섹션 및 압축 리플렉션 객체 직렬화 검증
 */
SW_TEST_CASE( Engine_Archive, ArchiveCompressedObjectSerialization )
{
	// 1) 압축 섹션 I/O
	sw::Archive		  writeArch;
	sw::vector<uint8> largeData( 1024, 0xAA );
	SW_EXPECT_TRUE( writeArch.writeCompressedSection( largeData.data(), static_cast<uint32>( largeData.size() ) ) );

	sw::Archive		  readArch( writeArch.getData(), writeArch.getSize() );
	sw::vector<uint8> decompressedData;
	SW_EXPECT_TRUE( readArch.readCompressedSection( decompressedData ) );
	SW_EXPECT_EQUAL( largeData.size(), decompressedData.size() );
	SW_EXPECT_EQUAL( 0xAA, decompressedData[0] );
	SW_EXPECT_EQUAL( 0xAA, decompressedData[1023] );

	// 2) 압축 객체 직렬화/역직렬화
	sw::Archive			compObjArch;
	TestReflectedPlayer playerSrc;
	playerSrc._level = 50;
	playerSrc._name	 = "CompressedWarrior";
	playerSrc._gold	 = 7777777;

	SW_EXPECT_TRUE( compObjArch.serializeCompressedObject( playerSrc ) );

	sw::Archive			compObjRead( compObjArch.getData(), compObjArch.getSize() );
	TestReflectedPlayer playerDst;
	SW_EXPECT_TRUE( compObjRead.deserializeCompressedObject( playerDst ) );
	SW_EXPECT_EQUAL( 50, playerDst._level );
	SW_EXPECT_EQUAL( sw::string( "CompressedWarrior" ), playerDst._name );
	SW_EXPECT_EQUAL( 7777777, playerDst._gold );
}

/**
 * @brief [Engine_Archive] Archive 버전 관리 객체 직렬화 및 마이그레이션 연계 검증
 */
SW_TEST_CASE( Engine_Archive, ArchiveVersionedObjectSerialization )
{
	sw::Archive			verArch;
	TestReflectedPlayer playerVer;
	playerVer._level = 10;
	playerVer._name	 = "VersionedMage";
	playerVer._gold	 = 1000;

	SW_EXPECT_TRUE( verArch.serializeVersionedObject( 2, playerVer ) );

	sw::Archive			verRead( verArch.getData(), verArch.getSize() );
	uint32				outVer = 0;
	TestReflectedPlayer playerRestored;
	SW_EXPECT_TRUE( verRead.deserializeVersionedObject( outVer, playerRestored, 2 ) );
	SW_EXPECT_EQUAL( 2u, outVer );
	SW_EXPECT_EQUAL( 10, playerRestored._level );
	SW_EXPECT_EQUAL( sw::string( "VersionedMage" ), playerRestored._name );
	SW_EXPECT_EQUAL( 1000, playerRestored._gold );
}

/**
 * @brief [Engine_Archive] Archive JSON 임베딩 및 상호 트랜스코딩(JSON ↔ 바이너리) 검증
 */
SW_TEST_CASE( Engine_Archive, ArchiveJsonObjectEmbeddingAndTranscoding )
{
	TestReflectedPlayer player;
	player._level = 77;
	player._name  = "JsonRogue";
	player._gold  = 55555;

	// 1) Archive 내 JSON 객체 임베딩 직렬화
	sw::Archive jsonArch;
	SW_EXPECT_TRUE( jsonArch.serializeJsonObject( player, true ) );

	sw::Archive			jsonRead( jsonArch.getData(), jsonArch.getSize() );
	TestReflectedPlayer playerFromJson;
	SW_EXPECT_TRUE( jsonRead.deserializeJsonObject( playerFromJson ) );
	SW_EXPECT_EQUAL( 77, playerFromJson._level );
	SW_EXPECT_EQUAL( sw::string( "JsonRogue" ), playerFromJson._name );
	SW_EXPECT_EQUAL( 55555, playerFromJson._gold );

	// 2) JSON 문자열 -> Archive 바이너리 변환(Transcoding)
	sw::Archive		 transcodeArch;
	const sw::string sampleJson = "{\"_level\":88, \"_name\":\"TranscodedHero\", \"_gold\":99999}";
	SW_EXPECT_TRUE( transcodeArch.convertJsonToBinary( sampleJson, *TestReflectedPlayer::StaticType() ) );

	sw::Archive			transcodeRead( transcodeArch.getData(), transcodeArch.getSize() );
	TestReflectedPlayer playerFromTranscode;
	SW_EXPECT_TRUE( transcodeRead.deserializeObject( playerFromTranscode ) );
	SW_EXPECT_EQUAL( 88, playerFromTranscode._level );
	SW_EXPECT_EQUAL( sw::string( "TranscodedHero" ), playerFromTranscode._name );
	SW_EXPECT_EQUAL( 99999, playerFromTranscode._gold );

	// 3) Archive 바이너리 -> JSON 문자열 변환
	const sw::string exportedJson = transcodeRead.convertBinaryToJson( *TestReflectedPlayer::StaticType() );
	SW_EXPECT_TRUE( exportedJson.find( "TranscodedHero" ) != sw::string::npos );
}

/**
 * @brief [Engine_Archive] Archive XML 임베딩 및 상호 트랜스코딩(XML ↔ 바이너리) 검증
 */
SW_TEST_CASE( Engine_Archive, ArchiveXmlObjectEmbeddingAndTranscoding )
{
	TestReflectedPlayer player;
	player._level = 33;
	player._name  = "XmlPaladin";
	player._gold  = 33333;

	// 1) Archive 내 XML 객체 임베딩 직렬화
	sw::Archive xmlArch;
	SW_EXPECT_TRUE( xmlArch.serializeXmlObject( player ) );

	sw::Archive			xmlRead( xmlArch.getData(), xmlArch.getSize() );
	TestReflectedPlayer playerFromXml;
	SW_EXPECT_TRUE( xmlRead.deserializeXmlObject( playerFromXml ) );
	SW_EXPECT_EQUAL( 33, playerFromXml._level );
	SW_EXPECT_EQUAL( sw::string( "XmlPaladin" ), playerFromXml._name );
	SW_EXPECT_EQUAL( 33333, playerFromXml._gold );

	// 2) XML 문자열 -> Archive 바이너리 변환(Transcoding)
	sw::Archive		 transcodeArch;
	const sw::string sampleXml = "<TestReflectedPlayer _level=\"44\" _name=\"TranscodedCleric\" _gold=\"44444\"/>";
	SW_EXPECT_TRUE( transcodeArch.convertXmlToBinary( sampleXml, *TestReflectedPlayer::StaticType() ) );

	sw::Archive			transcodeRead( transcodeArch.getData(), transcodeArch.getSize() );
	TestReflectedPlayer playerFromTranscode;
	SW_EXPECT_TRUE( transcodeRead.deserializeObject( playerFromTranscode ) );
	SW_EXPECT_EQUAL( 44, playerFromTranscode._level );
	SW_EXPECT_EQUAL( sw::string( "TranscodedCleric" ), playerFromTranscode._name );
	SW_EXPECT_EQUAL( 44444, playerFromTranscode._gold );
}

/**
 * @brief [Engine_Archive] 단일 Archive 내 복합 포맷(바이너리 + 압축 + JSON 메타 + XML 설정) 하이브리드 번들링 검증
 */
SW_TEST_CASE( Engine_Archive, ArchiveMultiFormatHybridBundle )
{
	sw::Archive bundleArch;

	// 헤더
	const uint32 bundleMagic = 0x5357424E; // 'SWBN'
	bundleArch << bundleMagic;

	// 1) 압축 바이너리 상태
	TestReflectedPlayer statePlayer;
	statePlayer._level = 99;
	statePlayer._name  = "MasterChief";
	statePlayer._gold  = 1234567890;
	SW_EXPECT_TRUE( bundleArch.serializeCompressedObject( statePlayer ) );

	// 2) JSON 디버그/메타데이터
	TestReflectedPlayer metaPlayer;
	metaPlayer._level = 1;
	metaPlayer._name  = "MetaInspector";
	metaPlayer._gold  = 0;
	SW_EXPECT_TRUE( bundleArch.serializeJsonObject( metaPlayer ) );

	// 3) XML 퀘스트/설정
	TestReflectedPlayer configPlayer;
	configPlayer._level = 5;
	configPlayer._name	= "QuestConfig";
	configPlayer._gold	= 500;
	SW_EXPECT_TRUE( bundleArch.serializeXmlObject( configPlayer ) );

	// 역직렬화 및 전수 일치성 검증
	sw::Archive bundleRead( bundleArch.getData(), bundleArch.getSize() );

	uint32 readMagic = 0;
	bundleRead >> readMagic;
	SW_EXPECT_EQUAL( bundleMagic, readMagic );

	TestReflectedPlayer readState;
	SW_EXPECT_TRUE( bundleRead.deserializeCompressedObject( readState ) );
	SW_EXPECT_EQUAL( 99, readState._level );
	SW_EXPECT_EQUAL( sw::string( "MasterChief" ), readState._name );
	SW_EXPECT_EQUAL( 1234567890, readState._gold );

	TestReflectedPlayer readMeta;
	SW_EXPECT_TRUE( bundleRead.deserializeJsonObject( readMeta ) );
	SW_EXPECT_EQUAL( 1, readMeta._level );
	SW_EXPECT_EQUAL( sw::string( "MetaInspector" ), readMeta._name );
	SW_EXPECT_EQUAL( 0, readMeta._gold );

	TestReflectedPlayer readConfig;
	SW_EXPECT_TRUE( bundleRead.deserializeXmlObject( readConfig ) );
	SW_EXPECT_EQUAL( 5, readConfig._level );
	SW_EXPECT_EQUAL( sw::string( "QuestConfig" ), readConfig._name );
	SW_EXPECT_EQUAL( 500, readConfig._gold );

	SW_EXPECT_TRUE( bundleRead.isOk() );
}

/**
 * @brief [Engine_Archive] 가변 길이 정수(VarInt / ZigZag) 경계값 및 압축율 전수 검증
 */
SW_TEST_CASE( Engine_Archive, VarIntAndZigZagBoundaryEncoding )
{
	sw::Archive writeArch;

	// 1) VarUInt 경계값
	const uint64 u0		  = 0;
	const uint64 u1		  = 1;
	const uint64 u127	  = 127;
	const uint64 u128	  = 128;
	const uint64 u16383	  = 16383;
	const uint64 u16384	  = 16384;
	const uint64 u32Max	  = 0xFFFFFFFFull;
	const uint64 u64Large = 0x123456789ABCDEFull;
	const uint64 u64Max	  = 0xFFFFFFFFFFFFFFFFull;

	writeArch.writeVarUInt( u0 );
	writeArch.writeVarUInt( u1 );
	writeArch.writeVarUInt( u127 );
	writeArch.writeVarUInt( u128 );
	writeArch.writeVarUInt( u16383 );
	writeArch.writeVarUInt( u16384 );
	writeArch.writeVarUInt( u32Max );
	writeArch.writeVarUInt( u64Large );
	writeArch.writeVarUInt( u64Max );

	// 2) VarInt (ZigZag) 경계값 (음수 포함)
	const int64 i0			 = 0;
	const int64 iNeg1		 = -1;
	const int64 iNeg2		 = -2;
	const int64 i63			 = 63;
	const int64 iNeg64		 = -64;
	const int64 iNeg128		 = -128;
	const int64 iNeg32768	 = -32768;
	const int64 iNeg2Billion = -2147483648ll;
	const int64 iNegLarge	 = -9223372036854775807ll;

	writeArch.writeVarInt( i0 );
	writeArch.writeVarInt( iNeg1 );
	writeArch.writeVarInt( iNeg2 );
	writeArch.writeVarInt( i63 );
	writeArch.writeVarInt( iNeg64 );
	writeArch.writeVarInt( iNeg128 );
	writeArch.writeVarInt( iNeg32768 );
	writeArch.writeVarInt( iNeg2Billion );
	writeArch.writeVarInt( iNegLarge );

	// 3) 개별 바이트 수 검증
	sw::vector<uint8> testBuffer;
	testBuffer.clear();
	SW_EXPECT_EQUAL( 1ULL, sw::VarIntUtil::encodeVarUInt64( 0, testBuffer ) );
	testBuffer.clear();
	SW_EXPECT_EQUAL( 1ULL, sw::VarIntUtil::encodeVarUInt64( 127, testBuffer ) );
	testBuffer.clear();
	SW_EXPECT_EQUAL( 2ULL, sw::VarIntUtil::encodeVarUInt64( 128, testBuffer ) );
	testBuffer.clear();
	SW_EXPECT_EQUAL( 2ULL, sw::VarIntUtil::encodeVarUInt64( 16383, testBuffer ) );
	testBuffer.clear();
	SW_EXPECT_EQUAL( 3ULL, sw::VarIntUtil::encodeVarUInt64( 16384, testBuffer ) );
	testBuffer.clear();
	SW_EXPECT_EQUAL( 1ULL, sw::VarIntUtil::encodeVarInt64( -1, testBuffer ) ); // ZigZag(-1) = 1 (1 byte!)

	// 4) 스트림 역직렬화 전수 일치 검증
	sw::Archive readArch( writeArch.getData(), writeArch.getSize() );

	uint64 outU0 = 99, outU1 = 99, outU127 = 99, outU128 = 99, outU16383 = 99, outU16384 = 99, outU32Max = 99, outU64Large = 99, outU64Max = 99;
	SW_EXPECT_TRUE( readArch.readVarUInt( outU0 ) );
	SW_EXPECT_TRUE( readArch.readVarUInt( outU1 ) );
	SW_EXPECT_TRUE( readArch.readVarUInt( outU127 ) );
	SW_EXPECT_TRUE( readArch.readVarUInt( outU128 ) );
	SW_EXPECT_TRUE( readArch.readVarUInt( outU16383 ) );
	SW_EXPECT_TRUE( readArch.readVarUInt( outU16384 ) );
	SW_EXPECT_TRUE( readArch.readVarUInt( outU32Max ) );
	SW_EXPECT_TRUE( readArch.readVarUInt( outU64Large ) );
	SW_EXPECT_TRUE( readArch.readVarUInt( outU64Max ) );

	SW_EXPECT_EQUAL( u0, outU0 );
	SW_EXPECT_EQUAL( u1, outU1 );
	SW_EXPECT_EQUAL( u127, outU127 );
	SW_EXPECT_EQUAL( u128, outU128 );
	SW_EXPECT_EQUAL( u16383, outU16383 );
	SW_EXPECT_EQUAL( u16384, outU16384 );
	SW_EXPECT_EQUAL( u32Max, outU32Max );
	SW_EXPECT_EQUAL( u64Large, outU64Large );
	SW_EXPECT_EQUAL( u64Max, outU64Max );

	int64 outI0 = 99, outINeg1 = 99, outINeg2 = 99, outI63 = 99, outINeg64 = 99, outINeg128 = 99, outINeg32768 = 99, outINeg2Billion = 99, outINegLarge = 99;
	SW_EXPECT_TRUE( readArch.readVarInt( outI0 ) );
	SW_EXPECT_TRUE( readArch.readVarInt( outINeg1 ) );
	SW_EXPECT_TRUE( readArch.readVarInt( outINeg2 ) );
	SW_EXPECT_TRUE( readArch.readVarInt( outI63 ) );
	SW_EXPECT_TRUE( readArch.readVarInt( outINeg64 ) );
	SW_EXPECT_TRUE( readArch.readVarInt( outINeg128 ) );
	SW_EXPECT_TRUE( readArch.readVarInt( outINeg32768 ) );
	SW_EXPECT_TRUE( readArch.readVarInt( outINeg2Billion ) );
	SW_EXPECT_TRUE( readArch.readVarInt( outINegLarge ) );

	SW_EXPECT_EQUAL( i0, outI0 );
	SW_EXPECT_EQUAL( iNeg1, outINeg1 );
	SW_EXPECT_EQUAL( iNeg2, outINeg2 );
	SW_EXPECT_EQUAL( i63, outI63 );
	SW_EXPECT_EQUAL( iNeg64, outINeg64 );
	SW_EXPECT_EQUAL( iNeg128, outINeg128 );
	SW_EXPECT_EQUAL( iNeg32768, outINeg32768 );
	SW_EXPECT_EQUAL( iNeg2Billion, outINeg2Billion );
	SW_EXPECT_EQUAL( iNegLarge, outINegLarge );
}

/**
 * @brief [Engine_Archive] StringPool 문자열 인터닝 및 중복 제거 대규모 압축률 검증
 */
SW_TEST_CASE( Engine_Archive, StringPoolInterningAndDeduplication )
{
	sw::Archive pooledArch;
	sw::Archive unpooledArch;

	const sw::vector<sw::string> listUniqueArchetypes = {
		"Warrior_Class_Archetype_Master_Champion",
		"Mage_Elemental_Sorcerer_Prime_Grandmaster",
		"Rogue_Shadow_Assassin_Elite_Infiltrator",
		"Paladin_Holy_Crusader_Knight_Templar",
		"Archer_Ranger_Marksman_Legend_Sniper" };

	const size_t totalRecords = 500;
	for ( size_t recordIndex = 0; recordIndex < totalRecords; ++recordIndex )
	{
		const sw::string& str = listUniqueArchetypes[recordIndex % listUniqueArchetypes.size()];
		pooledArch.writePooledString( str );
		unpooledArch.writeString( str );
	}

	// 1) 고유 풀 크기 검증 (Predefined 25개 + 동적 5개 등록됨)
	SW_EXPECT_EQUAL( 5ULL, pooledArch.getStringPool().getDynamicCount() );
	SW_EXPECT_EQUAL( static_cast<size_t>( sw::StringPool::kPredefinedCount + 5 ), pooledArch.getStringPool().getCount() );

	// 2) 아카이브에 풀 테이블 저장 후 전체 크기 비교 (압축률 > 70% 절감 검증)
	sw::Archive packageArch;
	packageArch.getStringPool() = pooledArch.getStringPool();
	packageArch.saveStringPool();
	packageArch.writeBytes( pooledArch.getData(), pooledArch.getSize() );

	SW_EXPECT_TRUE( packageArch.getSize() < unpooledArch.getSize() / 3 ); // 3배 이상 압축

	// 3) 역직렬화 및 100% 원본 복원 검증
	sw::Archive readPackage( packageArch.getData(), packageArch.getSize() );
	SW_EXPECT_TRUE( readPackage.loadStringPool() );

	for ( size_t recordIndex = 0; recordIndex < totalRecords; ++recordIndex )
	{
		sw::string restoredStr;
		SW_EXPECT_TRUE( readPackage.readPooledString( restoredStr ) );
		SW_EXPECT_EQUAL( listUniqueArchetypes[recordIndex % listUniqueArchetypes.size()], restoredStr );
	}
}

/**
 * @brief [Engine_Archive] PredefinedNameType 표준 사전 기반 0-바이트 스트링 풀 연동 검증
 */
SW_TEST_CASE( Engine_Archive, PredefinedTypesStringPoolIntegration )
{
	sw::StringPool pool;

	// 1) 표준 Predefined 타입들은 풀 생성 즉시 정확한 고정 인덱스를 반환해야 함
	SW_EXPECT_EQUAL( static_cast<uint32>( sw::PredefinedNameType::NameType_int32 ), pool.internString( "int32" ) );
	SW_EXPECT_EQUAL( static_cast<uint32>( sw::PredefinedNameType::NameType_float3 ), pool.internString( "float3" ) );
	SW_EXPECT_EQUAL( static_cast<uint32>( sw::PredefinedNameType::NameType_string ), pool.internString( "string" ) );
	SW_EXPECT_EQUAL( static_cast<uint32>( sw::PredefinedNameType::NameType_TagID ), pool.internString( "TagID" ) );

	// 2) 표준 타입만 사용했을 때의 동적 카운트는 0이어야 함
	SW_EXPECT_EQUAL( 0ULL, pool.getDynamicCount() );

	// 3) 아카이브에 풀을 저장할 때 동적 문자열이 없으면 헤더에 0만 기록됨 (문자열 페이로드 0바이트!)
	sw::Archive arch;
	pool.saveToArchive( arch );
	SW_EXPECT_EQUAL( 1ULL, arch.getSize() ); // VarUInt(0) = 1바이트만 기록됨

	// 4) 신규 동적 문자열 추가 시 kPredefinedCount 번호부터 순차 할당 검증
	const uint32 customId1 = pool.internString( "CustomDynamicArchetype_A" );
	const uint32 customId2 = pool.internString( "CustomDynamicArchetype_B" );
	SW_EXPECT_EQUAL( sw::StringPool::kPredefinedCount, customId1 );
	SW_EXPECT_EQUAL( sw::StringPool::kPredefinedCount + 1, customId2 );
	SW_EXPECT_EQUAL( 2ULL, pool.getDynamicCount() );

	// 5) 아카이브 저장 및 로드 라운드트립 검증
	sw::Archive dynamicArch;
	pool.saveToArchive( dynamicArch );

	sw::Archive	   readArch( dynamicArch.getData(), dynamicArch.getSize() );
	sw::StringPool restoredPool;
	SW_EXPECT_TRUE( restoredPool.loadFromArchive( readArch ) );
	SW_EXPECT_EQUAL( 2ULL, restoredPool.getDynamicCount() );
	SW_EXPECT_EQUAL( sw::string( "int32" ), sw::string( restoredPool.getString( static_cast<uint32>( sw::PredefinedNameType::NameType_int32 ) ) ) );
	SW_EXPECT_EQUAL( sw::string( "float3" ), sw::string( restoredPool.getString( static_cast<uint32>( sw::PredefinedNameType::NameType_float3 ) ) ) );
	SW_EXPECT_EQUAL( sw::string( "CustomDynamicArchetype_A" ), sw::string( restoredPool.getString( customId1 ) ) );
	SW_EXPECT_EQUAL( sw::string( "CustomDynamicArchetype_B" ), sw::string( restoredPool.getString( customId2 ) ) );
}

/**
 * @brief [Engine_Archive] SerializeContext 기반 객체 포인터 그래프 및 순환/공유 참조 중복 제거 검증
 */
SW_TEST_CASE( Engine_Archive, ObjectGraphAndCyclicPointerDeduplication )
{
	sw::SerializeContext ctx = sw::SerializeContext::getDefault();
	ctx.setEnableObjectDeduplication( true );
	SW_EXPECT_TRUE( ctx.isObjectDeduplicationEnabled() );

	int32 objA = 100;
	int32 objB = 200;
	int32 objC = 300;

	// 1) 포인터 ID 등록 일관성 검증
	const uint32 idA1 = ctx.registerOrFindObjectId( &objA );
	const uint32 idA2 = ctx.registerOrFindObjectId( &objA );
	const uint32 idB  = ctx.registerOrFindObjectId( &objB );
	const uint32 idC  = ctx.registerOrFindObjectId( &objC );

	SW_EXPECT_EQUAL( idA1, idA2 );
	SW_EXPECT_TRUE( idA1 != idB );
	SW_EXPECT_TRUE( idB != idC );

	uint32 queryId = 0;
	SW_EXPECT_TRUE( ctx.findObjectId( &objA, queryId ) );
	SW_EXPECT_EQUAL( idA1, queryId );

	// 2) 역직렬화 ID 매핑 테이블 검증
	ctx.registerObjectWithId( idA1, &objA );
	ctx.registerObjectWithId( idB, &objB );
	ctx.registerObjectWithId( idC, &objC );

	SW_EXPECT_EQUAL( reinterpret_cast<void*>( &objA ), ctx.findObjectById( idA1 ) );
	SW_EXPECT_EQUAL( reinterpret_cast<void*>( &objB ), ctx.findObjectById( idB ) );
	SW_EXPECT_EQUAL( reinterpret_cast<void*>( &objC ), ctx.findObjectById( idC ) );
	SW_EXPECT_EQUAL( nullptr, ctx.findObjectById( 99999 ) );

	// 3) 테이블 초기화 검증
	ctx.clearObjectTable();
	SW_EXPECT_EQUAL( nullptr, ctx.findObjectById( idA1 ) );
	uint32 idAfterClear = 0;
	SW_EXPECT_FALSE( ctx.findObjectId( &objA, idAfterClear ) );
}

/**
 * @brief [Engine_Archive] BinarySerializer 적응형 밀집 비트마스크(Dense Bitmask) 직렬화 검증
 */
SW_TEST_CASE( Engine_Archive, BinarySerializerAdaptiveDenseBitmask )
{
	TestReflectedPlayer player;
	player._level = 88;
	player._name  = "DenseCrusader";
	player._gold  = 7777777;

	sw::vector<uint8> denseBytes;
	sw::BinarySerializer::serializeCompact( &player, *TestReflectedPlayer::StaticType(), denseBytes );

	// 1) 3개 프로퍼티(100% 밀집도)이므로 Dense 모드 매직(0x01)으로 시작하는지 검증
	SW_EXPECT_TRUE( denseBytes.empty() == false );
	SW_EXPECT_EQUAL( static_cast<uint8>( 0x01 ), denseBytes[0] );

	// 2) 역직렬화 검증
	TestReflectedPlayer restoredPlayer;
	SW_EXPECT_TRUE( sw::BinarySerializer::deserializeCompact( &restoredPlayer, *TestReflectedPlayer::StaticType(), denseBytes.data(), denseBytes.size() ) );
	SW_EXPECT_EQUAL( 88, restoredPlayer._level );
	SW_EXPECT_EQUAL( sw::string( "DenseCrusader" ), restoredPlayer._name );
	SW_EXPECT_EQUAL( 7777777, restoredPlayer._gold );
}

/**
 * @brief [Engine_Archive] BinarySerializer 적응형 희소 인덱스(Sparse Index) 직렬화 검증
 */
SW_TEST_CASE( Engine_Archive, BinarySerializerAdaptiveSparseIndex )
{
	TestSparseReflectedConfig config;
	config._v3 = 12345;

	sw::Archive arch;
	SW_EXPECT_TRUE( arch.serializeCompactObject( config ) );

	sw::Archive				  readArch( arch.getData(), arch.getSize() );
	TestSparseReflectedConfig restoredConfig;
	SW_EXPECT_TRUE( readArch.deserializeCompactObject( restoredConfig ) );
	SW_EXPECT_EQUAL( 12345, restoredConfig._v3 );
	SW_EXPECT_EQUAL( 0, restoredConfig._v0 );
	SW_EXPECT_EQUAL( 0, restoredConfig._v7 );
}

/**
 * @brief [Engine_Archive] Archive 컴팩트 직렬화(Compact Object) 대규모 스트레스 테스트 (1,000회)
 */
SW_TEST_CASE( Engine_Archive, ArchiveCompactObjectStress1000 )
{
	sw::Archive	 stressArch;
	const size_t iterationCount = 1000;

	for ( size_t index = 0; index < iterationCount; ++index )
	{
		TestReflectedPlayer p;
		p._level = static_cast<int32>( index + 1 );
		p._name	 = "Player_" + sw::to_string( index );
		p._gold	 = static_cast<int64>( index * 100 );
		SW_EXPECT_TRUE( stressArch.serializeCompactObject( p ) );
	}

	sw::Archive readArch( stressArch.getData(), stressArch.getSize() );
	for ( size_t index = 0; index < iterationCount; ++index )
	{
		TestReflectedPlayer restored;
		SW_EXPECT_TRUE( readArch.deserializeCompactObject( restored ) );
		SW_EXPECT_EQUAL( static_cast<int32>( index + 1 ), restored._level );
		SW_EXPECT_EQUAL( sw::string( "Player_" + sw::to_string( index ) ), restored._name );
		SW_EXPECT_EQUAL( static_cast<int64>( index * 100 ), restored._gold );
	}
	SW_EXPECT_TRUE( readArch.isOk() );
}

/**
 * @brief [Engine_Archive] 멀티플레이어 넷코드 시뮬레이션: 50개 액터 상태 델타 패킷 압축 및 MTU 한도 내 전송 검증
 */
SW_TEST_CASE( Engine_Archive, SimulatedNetworkPacketDeltaReplication )
{
	const size_t					numActors = 50;
	sw::vector<TestReflectedPlayer> listServerActor;
	listServerActor.reserve( numActors );

	for ( size_t actorIndex = 0; actorIndex < numActors; ++actorIndex )
	{
		TestReflectedPlayer actor;
		actor._level = static_cast<int32>( ( actorIndex % 5 ) + 1 );
		actor._name	 = "Actor_" + sw::to_string( actorIndex );
		actor._gold	 = static_cast<int64>( actorIndex * 250 );
		listServerActor.push_back( std::move( actor ) );
	}

	// 1) 서버 틱: 50개 액터 상태를 단일 네트워크 패킷 버퍼에 직렬화
	sw::Archive networkPacketArch;
	networkPacketArch.writeVarUInt( static_cast<uint64>( numActors ) );

	for ( const auto& actor : listServerActor )
	{
		SW_EXPECT_TRUE( networkPacketArch.serializeCompactObject( actor ) );
	}
	networkPacketArch.writeChecksum();

	// 2) 패킷 크기 검증: 50개 액터 전체가 액터당 50바이트 미만(총 2.5KB 미만)으로 컴팩트 패킹되는지 검증
	SW_EXPECT_TRUE( networkPacketArch.getSize() < 2500 );
	SW_EXPECT_TRUE( networkPacketArch.getSize() < ( numActors * 50 ) );

	// 3) 클라이언트 수신: 체크섬 검증 및 50개 액터 100% 무결성 복원
	sw::Archive clientPacketArch( networkPacketArch.getData(), networkPacketArch.getSize() );
	SW_EXPECT_TRUE( clientPacketArch.validateChecksum() );

	uint64 receivedActorCount = 0;
	SW_EXPECT_TRUE( clientPacketArch.readVarUInt( receivedActorCount ) );
	SW_EXPECT_EQUAL( static_cast<uint64>( numActors ), receivedActorCount );

	for ( size_t actorIndex = 0; actorIndex < numActors; ++actorIndex )
	{
		TestReflectedPlayer clientActor;
		SW_EXPECT_TRUE( clientPacketArch.deserializeCompactObject( clientActor ) );
		SW_EXPECT_EQUAL( listServerActor[actorIndex]._level, clientActor._level );
		SW_EXPECT_EQUAL( listServerActor[actorIndex]._name, clientActor._name );
		SW_EXPECT_EQUAL( listServerActor[actorIndex]._gold, clientActor._gold );
	}

	uint32 packetChecksum = 0;
	clientPacketArch >> packetChecksum;
	SW_EXPECT_TRUE( clientPacketArch.isOk() );
}

/**
 * @brief [Engine_Archive] 손상된 버퍼 및 경계 초과 결함 주입(Fault Injection) 내결함성 검증
 */
SW_TEST_CASE( Engine_Archive, CorruptedBufferFaultInjectionAndGracefulHandling )
{
	// 1) 잘못된 모드 매직 바이트(0xFF) 결함 주입
	const sw::vector<uint8> corruptedMagic = { 0xFF, 0x03, 0x00, 0x00 };
	TestReflectedPlayer		dummyPlayer;
	SW_EXPECT_FALSE( sw::BinarySerializer::deserializeCompact( &dummyPlayer, *TestReflectedPlayer::StaticType(), corruptedMagic.data(), corruptedMagic.size() ) );

	// 2) 잘린(Truncated) 페이로드 바이트 결함 주입
	const sw::vector<uint8> truncatedDense = { 0x01, 0x03, 0x07, 0x04 }; // Dense mode, 3 props, bitmask 0x07, payload length 4 but no actual payload bytes
	SW_EXPECT_FALSE( sw::BinarySerializer::deserializeCompact( &dummyPlayer, *TestReflectedPlayer::StaticType(), truncatedDense.data(), truncatedDense.size() ) );

	// 3) 잘린 VarInt 스트림 결함 주입 (MSB 비트 0x80이 켜져 있으나 다음 바이트가 없는 경우)
	const sw::vector<uint8> truncatedVarInt = { 0x80, 0x80, 0x80 };
	sw::Archive				corruptArch( truncatedVarInt.data(), truncatedVarInt.size() );
	uint64					outVal = 0;
	SW_EXPECT_FALSE( corruptArch.readVarUInt( outVal ) );
	SW_EXPECT_TRUE( corruptArch.isError() );

	// 4) 빈 버퍼 역직렬화 안전성 검증
	sw::Archive emptyArch( nullptr, 0 );
	SW_EXPECT_FALSE( emptyArch.deserializeCompactObject( dummyPlayer ) );
	SW_EXPECT_TRUE( emptyArch.isError() );
}

/**
 * @brief [Engine_Archive] 복합 중복 제거 파이프라인 대규모 엔드-투-엔드 스트레스 테스트 (5,000회)
 */
SW_TEST_CASE( Engine_Archive, MassiveEndToEndDeduplicationPipelineStress5000 )
{
	sw::Archive	 streamArch;
	const size_t iterationCount = 5000;

	// Predefined 및 10개 고유 동적 문자열 준비
	const sw::vector<sw::string> listDynStrings = {
		"Dungeon_Floor_Boss_Titan",
		"Weapon_Excalibur_Holy_Blade",
		"Armor_Dragon_Scale_Cuirass",
		"Spell_Chain_Lightning_Overload",
		"Buff_Blessing_Of_Kings",
		"Quest_Slay_The_Demon_Lord",
		"NPC_Elder_Sage_Alderman",
		"Location_Sunken_Temple_Depths",
		"Inventory_Bag_Of_Holding",
		"Achievement_Master_Of_The_Realm" };

	// 1) 5,000개 복합 데이터 스트리밍 기록
	for ( size_t iterationIndex = 0; iterationIndex < iterationCount; ++iterationIndex )
	{
		// Predefined 심볼 ID
		streamArch.writePooledString( "int32" );
		streamArch.writePooledString( "float3" );

		// 동적 풀링 문자열
		streamArch.writePooledString( listDynStrings[iterationIndex % listDynStrings.size()] );

		// ZigZag 가변 정수 (양수/음수 교차)
		const int64 zigVal = ( ( iterationIndex % 2 ) == 0 ) ? static_cast<int64>( iterationIndex ) : -static_cast<int64>( iterationIndex );
		streamArch.writeVarInt( zigVal );

		// 컴팩트 객체
		TestReflectedPlayer player;
		player._level = static_cast<int32>( ( iterationIndex % 100 ) + 1 );
		player._name  = listDynStrings[iterationIndex % listDynStrings.size()];
		player._gold  = static_cast<int64>( iterationIndex * 50 );
		SW_EXPECT_TRUE( streamArch.serializeCompactObject( player ) );
	}

	// 2) 아카이브 헤더에 스트링 풀 테이블 저장
	sw::Archive bundledArch;
	bundledArch.getStringPool() = streamArch.getStringPool();
	bundledArch.saveStringPool();
	bundledArch.writeBytes( streamArch.getData(), streamArch.getSize() );
	bundledArch.writeChecksum();

	// 3) 역직렬화 및 5,000회 전수 일치성 검증
	sw::Archive readBundled( bundledArch.getData(), bundledArch.getSize() );
	SW_EXPECT_TRUE( readBundled.validateChecksum() );
	SW_EXPECT_TRUE( readBundled.loadStringPool() );

	for ( size_t iterationIndex = 0; iterationIndex < iterationCount; ++iterationIndex )
	{
		sw::string s1, s2, s3;
		SW_EXPECT_TRUE( readBundled.readPooledString( s1 ) );
		SW_EXPECT_TRUE( readBundled.readPooledString( s2 ) );
		SW_EXPECT_TRUE( readBundled.readPooledString( s3 ) );

		SW_EXPECT_EQUAL( sw::string( "int32" ), s1 );
		SW_EXPECT_EQUAL( sw::string( "float3" ), s2 );
		SW_EXPECT_EQUAL( listDynStrings[iterationIndex % listDynStrings.size()], s3 );

		int64 readZig = 0;
		SW_EXPECT_TRUE( readBundled.readVarInt( readZig ) );
		const int64 expectedZig = ( ( iterationIndex % 2 ) == 0 ) ? static_cast<int64>( iterationIndex ) : -static_cast<int64>( iterationIndex );
		SW_EXPECT_EQUAL( expectedZig, readZig );

		TestReflectedPlayer restoredPlayer;
		SW_EXPECT_TRUE( readBundled.deserializeCompactObject( restoredPlayer ) );
		SW_EXPECT_EQUAL( static_cast<int32>( ( iterationIndex % 100 ) + 1 ), restoredPlayer._level );
		SW_EXPECT_EQUAL( listDynStrings[iterationIndex % listDynStrings.size()], restoredPlayer._name );
		SW_EXPECT_EQUAL( static_cast<int64>( iterationIndex * 50 ), restoredPlayer._gold );
	}

	SW_EXPECT_TRUE( readBundled.isOk() );
}

SW_TEST_CASE( Engine_Archive, SerializerUtilTranscodingAndScopedScratch )
{
	const sw::TypeInfo* pPlayerType = TestReflectedPlayer::StaticType();
	SW_EXPECT_NOT_NULL( pPlayerType );

	// 1. Test ScopedScratchInstance RAII
	{
		sw::ScopedScratchInstance scratch( *pPlayerType );
		SW_EXPECT_TRUE( scratch.isValid() );
		SW_EXPECT_NOT_NULL( scratch.get() );
		TestReflectedPlayer* pPlayer = static_cast<TestReflectedPlayer*>( scratch.get() );
		pPlayer->_level				 = 99;
		pPlayer->_name				 = "ScratchHero";
		pPlayer->_gold				 = 7777;
	} // Automatically destroyed safely here

	// 2. Test JSON <-> Binary transcoding via SerializerUtil
	TestReflectedPlayer originalPlayer;
	originalPlayer._level = 42;
	originalPlayer._name  = "TranscodePlayer";
	originalPlayer._gold  = 99999;

	const sw::string jsonSource = sw::JsonSerializer::serialize( &originalPlayer, *pPlayerType );
	SW_EXPECT_FALSE( jsonSource.empty() );

	sw::vector<uint8> transcodedBinary;
	SW_EXPECT_TRUE( sw::SerializerUtil::transcodeJsonToBinary( jsonSource, *pPlayerType, transcodedBinary ) );
	SW_EXPECT_FALSE( transcodedBinary.empty() );

	const sw::string transcodedJson = sw::SerializerUtil::transcodeBinaryToJson( transcodedBinary.data(), transcodedBinary.size(), *pPlayerType );
	SW_EXPECT_FALSE( transcodedJson.empty() );

	TestReflectedPlayer deserializedFromJson;
	SW_EXPECT_TRUE( sw::JsonSerializer::deserialize( &deserializedFromJson, *pPlayerType, transcodedJson ) );
	SW_EXPECT_EQUAL( 42, deserializedFromJson._level );
	SW_EXPECT_EQUAL( "TranscodePlayer", deserializedFromJson._name );
	SW_EXPECT_EQUAL( 99999, deserializedFromJson._gold );

	// 3. Test XML <-> Binary transcoding via SerializerUtil
	const sw::string xmlSource = sw::XmlSerializer::serialize( &originalPlayer, *pPlayerType );
	SW_EXPECT_FALSE( xmlSource.empty() );

	sw::vector<uint8> transcodedXmlBinary;
	SW_EXPECT_TRUE( sw::SerializerUtil::transcodeXmlToBinary( xmlSource, *pPlayerType, transcodedXmlBinary ) );
	SW_EXPECT_FALSE( transcodedXmlBinary.empty() );

	const sw::string transcodedXml = sw::SerializerUtil::transcodeBinaryToXml( transcodedXmlBinary.data(), transcodedXmlBinary.size(), *pPlayerType );
	SW_EXPECT_FALSE( transcodedXml.empty() );

	TestReflectedPlayer deserializedFromXml;
	SW_EXPECT_TRUE( sw::XmlSerializer::deserialize( &deserializedFromXml, *pPlayerType, transcodedXml ) );
	SW_EXPECT_EQUAL( 42, deserializedFromXml._level );
	SW_EXPECT_EQUAL( "TranscodePlayer", deserializedFromXml._name );
	SW_EXPECT_EQUAL( 99999, deserializedFromXml._gold );
}

SW_TEST_CASE( Engine_Archive, ZeroCopyReadBytesView )
{
	sw::Archive writeArch;
	const uint8 samplePayload[8] = { 0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44 };
	writeArch.writeBytes( samplePayload, 8 );

	sw::Archive	 readArch( writeArch.getData(), writeArch.getSize() );
	const uint8* pView = readArch.readBytesView( 8 );
	SW_EXPECT_NOT_NULL( pView );
	SW_EXPECT_EQUAL( samplePayload[0], pView[0] );
	SW_EXPECT_EQUAL( samplePayload[7], pView[7] );
	SW_EXPECT_TRUE( readArch.isOk() );
	SW_EXPECT_EQUAL( 0ULL, readArch.getRemainingBytes() );

	// Over-read error check
	const uint8* pNullView = readArch.readBytesView( 1 );
	SW_EXPECT_NULL( pNullView );
	SW_EXPECT_TRUE( readArch.isError() );
}

SW_TEST_CASE( Engine_Archive, MalformedStreamOversizedAllocationFaultInjection )
{
	// 1. Corrupted Archive::operator>>( string& ) with huge length prefix (0xFFFFFFFF)
	{
		const uint8 malformedStringBytes[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
		sw::Archive readArch( malformedStringBytes, 4 );
		sw::string	outStr;
		readArch >> outStr;
		SW_EXPECT_TRUE( readArch.isError() );
		SW_EXPECT_TRUE( outStr.empty() );
	}

	// 2. Corrupted Archive::operator>>( vector<uint8>& ) with huge length prefix (0x7FFFFFFF)
	{
		const uint8		  malformedVectorBytes[8] = { 0xFF, 0xFF, 0xFF, 0x7F, 0x01, 0x02, 0x03, 0x04 };
		sw::Archive		  readArch( malformedVectorBytes, 8 );
		sw::vector<uint8> outBytes;
		readArch >> outBytes;
		SW_EXPECT_TRUE( readArch.isError() );
		SW_EXPECT_TRUE( outBytes.empty() );
	}

	// 3. Corrupted BinaryStreamReader::readBytes with huge length prefix (0x00FFFFFF)
	{
		const uint8			   malformedBytes[8] = { 0xFF, 0xFF, 0xFF, 0x00, 0x11, 0x22, 0x33, 0x44 };
		sw::BinaryStreamReader reader( malformedBytes, 8 );
		sw::vector<uint8>	   outBytes;
		SW_EXPECT_FALSE( reader.readBytes( outBytes ) );
		SW_EXPECT_TRUE( outBytes.empty() );
	}

	// 4. Corrupted BinaryStreamReader::readString with huge length prefix
	{
		const uint8			   malformedBytes[8] = { 0xFF, 0xFF, 0xFF, 0x00, 0x11, 0x22, 0x33, 0x44 };
		sw::BinaryStreamReader reader( malformedBytes, 8 );
		sw::string			   outStr;
		SW_EXPECT_FALSE( reader.readString( outStr ) );
		SW_EXPECT_TRUE( outStr.empty() );
	}
}

SW_TEST_CASE( Engine_Archive, CorruptedStringPoolAndOutofBoundsSymbolFaultInjection )
{
	// 1. StringPool::loadFromArchive with oversized dynCount (exceeding 1,000,000 limit)
	{
		sw::Archive writeArch;
		writeArch.writeVarUInt( 2000000ULL ); // 2 million dynamic strings
		sw::Archive	   readArch( writeArch.getData(), writeArch.getSize() );
		sw::StringPool pool;
		SW_EXPECT_FALSE( pool.loadFromArchive( readArch ) );
	}

	// 2. StringPool::loadFromBinaryBuffer with oversized dynCount
	{
		sw::vector<uint8> rawBytes;
		sw::VarIntUtil::encodeVarUInt64( 5000000ULL, rawBytes );
		size_t		   offset = 0;
		sw::StringPool pool;
		SW_EXPECT_FALSE( pool.loadFromBinaryBuffer( rawBytes.data(), rawBytes.size(), offset ) );
	}

	// 3. Archive::readPooledString with out-of-bounds poolId
	{
		sw::Archive writeArch;
		writeArch.writeVarUInt( 99999ULL ); // poolId not in string pool
		sw::Archive readArch( writeArch.getData(), writeArch.getSize() );
		sw::string	outStr;
		SW_EXPECT_FALSE( readArch.readPooledString( outStr ) );
		SW_EXPECT_TRUE( readArch.isError() );
		SW_EXPECT_TRUE( outStr.empty() );
	}
}

SW_TEST_CASE( Engine_Archive, TranscodingFailureCasesAndInvalidInputs )
{
	const sw::TypeInfo* pPlayerType = TestReflectedPlayer::StaticType();
	SW_EXPECT_NOT_NULL( pPlayerType );

	// 1. Malformed JSON transcoding to binary
	{
		const sw::string  brokenJson = "{\"_level\": 42, \"_name\": \"Unfinished";
		sw::vector<uint8> outBinary;
		SW_EXPECT_FALSE( sw::SerializerUtil::transcodeJsonToBinary( brokenJson, *pPlayerType, outBinary ) );
		SW_EXPECT_TRUE( outBinary.empty() );
	}

	// 2. Malformed XML transcoding to binary
	{
		const sw::string  brokenXml = "<TestReflectedPlayer _level=\"42\" <unclosed_tag";
		sw::vector<uint8> outBinary;
		SW_EXPECT_FALSE( sw::SerializerUtil::transcodeXmlToBinary( brokenXml, *pPlayerType, outBinary ) );
		SW_EXPECT_TRUE( outBinary.empty() );
	}

	// 3. Null / empty binary data transcoding to JSON and XML
	{
		const sw::string jsonResult = sw::SerializerUtil::transcodeBinaryToJson( nullptr, 0, *pPlayerType );
		SW_EXPECT_TRUE( jsonResult.empty() );

		const sw::string xmlResult = sw::SerializerUtil::transcodeBinaryToXml( nullptr, 0, *pPlayerType );
		SW_EXPECT_TRUE( xmlResult.empty() );
	}

	// 4. Truncated binary data transcoding to JSON
	{
		const uint8		 truncatedBytes[2] = { 0x01, 0x00 };
		const sw::string jsonResult		   = sw::SerializerUtil::transcodeBinaryToJson( truncatedBytes, 2, *pPlayerType );
		SW_EXPECT_TRUE( jsonResult.empty() );
	}
}

SW_TEST_CASE( Engine_Archive, CorruptedSaveSlotAndDocumentBinaryStreams )
{
	// 1. SaveSlot SAV1 CRC32 mismatch detection
	{
		sw::SaveSlot validSlot;
		validSlot._mapPath = "dungeon_boss";
		validSlot._playerX = 100;
		validSlot._playerY = 200;
		validSlot.setFlag( "boss_defeated", 1 );

		const sw::string savePath = "saved/test_corrupt_slot.sav";
		SW_EXPECT_TRUE( validSlot.saveCommonToBinaryFile( savePath ) );

		// Read and intentionally flip a byte in payload
		sw::vector<uint8> fileBytes;
		SW_EXPECT_TRUE( sw::FileUtil::readFile( savePath, fileBytes ) );
		SW_EXPECT_TRUE( fileBytes.size() > 20 );

		// Flip byte at the end of the file
		fileBytes.back() ^= 0xFF;
		SW_EXPECT_TRUE( sw::FileUtil::writeFile( savePath, fileBytes.data(), fileBytes.size() ) );

		// Load must detect CRC32 mismatch and reject
		sw::SaveSlot corruptSlot;
		SW_EXPECT_FALSE( corruptSlot.loadCommonFromBinaryFile( savePath ) );

		// Cleanup
		sw::FileUtil::removeFile( savePath );
	}

	// 2. SceneDocument corrupted binary magic
	{
		const uint8		 corruptedSceneBytes[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00 };
		const sw::string testScenePath			= "saved/test_corrupt_scene.bin";
		SW_EXPECT_TRUE( sw::FileUtil::writeFile( testScenePath, corruptedSceneBytes, 8 ) );

		sw::SceneDocument sceneDoc;
		SW_EXPECT_FALSE( sceneDoc.loadBinary( testScenePath ) );
		SW_EXPECT_FALSE( sceneDoc._bValid );

		// Cleanup
		sw::FileUtil::removeFile( testScenePath );
	}

	// 3. PrefabAsset corrupted binary magic
	{
		const uint8		 corruptedPrefabBytes[8] = { 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 };
		const sw::string testPrefabPath			 = "saved/test_corrupt_prefab.bin";
		SW_EXPECT_TRUE( sw::FileUtil::writeFile( testPrefabPath, corruptedPrefabBytes, 8 ) );

		sw::PrefabAsset prefabAsset;
		SW_EXPECT_FALSE( prefabAsset.loadFromBinaryFile( testPrefabPath ) );
		SW_EXPECT_FALSE( prefabAsset.isValid() );

		// Cleanup
		sw::FileUtil::removeFile( testPrefabPath );
	}
}
