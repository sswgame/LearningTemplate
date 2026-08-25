#include "pch.h"

#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Utility/File/Archive.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Core_File — 경로·읽기쓰기·아카이브
// ------------------------------------------------------------------------------
/**
 * @brief [Core_File] FileUtil 경로 동작
 */
SW_TEST_CASE( Core_File, FileUtilPathOperations )
{
	sw::string fullPath = "Projects/Sample/TestFile.txt";
	sw::string fileName = sw::FileUtil::getFileNamePart( fullPath );
	SW_EXPECT_EQUAL( sw::string( "TestFile.txt" ), fileName );

	sw::string dirName = sw::FileUtil::getDirectoryPart( fullPath );
	SW_EXPECT_FALSE( dirName.empty() );
	SW_EXPECT_TRUE( dirName.find( "Sample" ) != sw::string::npos );

	sw::string noExt = sw::FileUtil::removeExtension( fullPath );
	SW_EXPECT_EQUAL( sw::string( "Projects/Sample/TestFile" ), noExt );

	sw::string newExt = sw::FileUtil::replaceExtension( fullPath, "bin" );
	SW_EXPECT_EQUAL( sw::string( "Projects/Sample/TestFile.bin" ), newExt );

	SW_EXPECT_EQUAL( sw::string( "Foo/Bar" ), sw::FileUtil::normalizeSeparators( "Foo\\Bar" ) );
	SW_EXPECT_EQUAL( sw::string( "foo/bar" ), sw::FileUtil::normalizePath( "Foo\\Bar" ) );
	SW_EXPECT_TRUE( sw::FileUtil::pathsEqualNormalized( "Foo/Bar", "foo/bar" ) );
}

/**
 * @brief [Core_File] 읽기/쓰기가 경로 대소문자 유지
 */
SW_TEST_CASE( Core_File, ReadWritePreservesPathCase )
{
	const sw::string dir = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "SwPathCaseTestDir" );
	sw::FileUtil::ensureDirectoryExists( dir );
	const sw::string pathStr = sw::FileUtil::joinPath( dir, "MixedCaseFile.bin" );
	const sw::string content = "case-sensitive-io";

	SW_EXPECT_TRUE( sw::FileUtil::writeFile( pathStr, reinterpret_cast<const uint8*>( content.data() ), content.size() ) );
	SW_EXPECT_TRUE( sw::FileUtil::fileExists( pathStr ) );

	sw::vector<uint8> readBuffer;
	SW_EXPECT_TRUE( sw::FileUtil::readFile( pathStr, readBuffer ) );
	SW_EXPECT_EQUAL( content, sw::string( readBuffer.begin(), readBuffer.end() ) );

	sw::FileUtil::removeFile( pathStr );
}

/**
 * @brief [Core_File] 파일 쓰기와 읽기
 */
SW_TEST_CASE( Core_File, WriteAndReadFile )
{
	sw::string testPath	   = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_output_temp.bin" );
	sw::string testContent = "Hello C++ Workspace!";

	bool writeOk = sw::FileUtil::writeFile( testPath, reinterpret_cast<const uint8*>( testContent.data() ), testContent.size() );
	SW_EXPECT_TRUE( writeOk );
	SW_EXPECT_TRUE( sw::FileUtil::fileExists( testPath ) );

	sw::vector<uint8> readBuffer;
	bool			  readOk = sw::FileUtil::readFile( testPath, readBuffer );
	SW_EXPECT_TRUE( readOk );

	sw::string readContent( readBuffer.begin(), readBuffer.end() );
	SW_EXPECT_EQUAL( testContent, readContent );

	sw::FileUtil::removeFile( testPath );
}

// ------------------------------------------------------------------------------
// 2) Archive — 바이너리 TLV·체크섬
// ------------------------------------------------------------------------------
/**
 * @brief [Core_File] MemoryArchive 바이너리 직렬화
 */
SW_TEST_CASE( Core_File, MemoryArchiveBinarySerialization )
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
 * @brief [Core_File] Archive 객체 TLV 직렬화
 */
SW_TEST_CASE( Core_File, ArchiveObjectTLVSerialization )
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
	info._propertyList		 = {
		{sw::hashed_string( "_valA" ), sw::hashed_string( "int32" ),
		  offsetof( DummyStruct, _valA ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr},
		{sw::hashed_string( "_valB" ), sw::hashed_string( "int32" ),
		  offsetof( DummyStruct, _valB ), false, sw::ContainerKind::None, sw::hashed_string(), sw::hashed_string(), nullptr}
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
	std::swap( infoReordered._propertyList[0], infoReordered._propertyList[1] );

	DummyStruct dst;
	dst._valA	= 0;
	dst._valB	= 0;
	bool readOk = archRead.deserializeObject( &dst, infoReordered );
	SW_EXPECT_TRUE( readOk );
	SW_EXPECT_EQUAL( 123, dst._valA );
	SW_EXPECT_EQUAL( 456, dst._valB );
}

/**
 * @brief [Core_File] Archive 체크섬 검증
 */
SW_TEST_CASE( Core_File, ArchiveChecksumVerification )
{
	sw::Archive writeArc;
	writeArc << sw::string( "AntigravityData" );
	writeArc << 123456789;

	uint32 initialChecksum = writeArc.calculateChecksum();
	SW_EXPECT_TRUE( initialChecksum != 0 );

	writeArc.writeChecksum();
	SW_EXPECT_TRUE( writeArc.validateChecksum() );
}
