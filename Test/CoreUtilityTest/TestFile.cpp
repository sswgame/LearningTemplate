/**
 * @file TestFile.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Reflection/ReflectionCore.h"

#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/File/Archive.h"

SW_TEST_CASE( Utility_File, FileUtilPathOperations )
{
	std::string fullPath = "Projects/Sample/TestFile.txt";
	std::string fileName = sw::FileUtil::getFileNamePart( fullPath );
	SW_EXPECT_EQUAL( std::string( "TestFile.txt" ), fileName );

	std::string dirName = sw::FileUtil::getDirectoryPart( fullPath );
	SW_EXPECT_FALSE( dirName.empty() );
	SW_EXPECT_TRUE( dirName.find( "Sample" ) != std::string::npos );

	std::string noExt = sw::FileUtil::removeExtension( fullPath );
	SW_EXPECT_EQUAL( std::string( "Projects/Sample/TestFile" ), noExt );

	std::string newExt = sw::FileUtil::replaceExtension( fullPath, "bin" );
	SW_EXPECT_EQUAL( std::string( "Projects/Sample/TestFile.bin" ), newExt );
}

SW_TEST_CASE( Utility_File, WriteAndReadFile )
{
	std::string testPath	= ( std::filesystem::temp_directory_path() / "test_output_temp.bin" ).string();
	std::string testContent = "Hello C++ Workspace!";

	bool writeOk = sw::FileUtil::writeFile( testPath, reinterpret_cast<const uint8*>( testContent.data() ), testContent.size() );
	SW_EXPECT_TRUE( writeOk );
	SW_EXPECT_TRUE( sw::FileUtil::isFileExist( testPath ) );

	std::vector<uint8> readBuffer;
	bool			   readOk = sw::FileUtil::readFile( testPath, readBuffer );
	SW_EXPECT_TRUE( readOk );

	std::string readContent( readBuffer.begin(), readBuffer.end() );
	SW_EXPECT_EQUAL( testContent, readContent );

	std::filesystem::remove( testPath );
}

SW_TEST_CASE( Utility_File, MemoryArchiveBinarySerialization )
{

	sw::Archive arch;

	int32	intVal	 = 42;
	float32 floatVal = 3.14159f;

	arch << intVal << floatVal;
	arch.setReadModeAndResetPos( true );

	int32	readInt	  = 0;
	float32 readFloat = 0.0f;

	arch >> readInt >> readFloat;

	SW_EXPECT_EQUAL( 42, readInt );
	SW_EXPECT_NEAR_EQUAL( 3.14159f, readFloat, 1e-4f );
}

SW_TEST_CASE( Utility_File, ArchiveObjectTLVSerialization )
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

	std::vector<uint8> data;
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

SW_TEST_CASE( Utility_File, ArchiveChecksumVerification )
{
	sw::Archive writeArc;
	writeArc << std::string( "AntigravityData" );
	writeArc << 123456789LL;

	uint32 initialChecksum = writeArc.calculateChecksum();
	SW_EXPECT_TRUE( initialChecksum != 0 );

	writeArc.writeChecksum();
	SW_EXPECT_TRUE( writeArc.validateChecksum() );
}
