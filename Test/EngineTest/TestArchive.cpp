#include "pch.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Serialization/Format/Archive.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// Engine_Archive — 바이너리 스트림·TLV·체크섬 직렬화
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
 * @brief [Engine_Archive] Archive 체크섬 검증
 */
SW_TEST_CASE( Engine_Archive, ArchiveChecksumVerification )
{
	sw::Archive writeArc;
	writeArc << sw::string( "AntigravityData" );
	writeArc << 123456789;

	uint32 initialChecksum = writeArc.calculateChecksum();
	SW_EXPECT_TRUE( initialChecksum != 0 );

	writeArc.writeChecksum();
	SW_EXPECT_TRUE( writeArc.validateChecksum() );
}
