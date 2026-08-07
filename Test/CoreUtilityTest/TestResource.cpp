/**
 * @file TestResource.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Utility/String/StringUtil.h"

SW_TEST_CASE( Utility_Resource, GetResourcePathEmptyForNonexistent )
{
	sw::ResourceUtil::initialize();
	std::string nonExistent = sw::ResourceUtil::getResourcePath( "non_existent_file_xyz_12345.dat" );
	SW_EXPECT_TRUE( nonExistent.empty() );
}

SW_TEST_CASE( Utility_Resource, GetResourcePathWithFolderNameEmptyForNonexistent )
{
	sw::ResourceUtil::initialize();
	std::string nonExistent = sw::ResourceUtil::getResourcePath( "non_existent_file_xyz_12345.dat", "Textures" );
	SW_EXPECT_TRUE( nonExistent.empty() );
}

SW_TEST_CASE( Utility_Resource, FolderRootsAndKnownShaderPath )
{
	SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

	const std::string& root = sw::ResourceUtil::getRootFolderPath();
	SW_EXPECT_TRUE_MSG( root.empty() == false, "Resource root should be resolved" );
	SW_EXPECT_TRUE_MSG( sw::ResourceUtil::getGameFolderPath().empty() == false, "Game resource folder should exist" );

	const std::vector<std::string> shaderFolders = sw::ResourceUtil::getResourceFolders( "Shaders" );
	SW_EXPECT_TRUE_MSG( shaderFolders.empty() == false, "Expected at least one Shaders folder under resource roots" );

	// folderName filters within each resource root; Game assets live under Resource/Game already.
	const std::string shaderPath = sw::ResourceUtil::getResourcePath( "Shaders/SampleCompute.hlsl" );
	if ( shaderPath.empty() )
	{
		SW_TEST_SKIP( "SampleCompute.hlsl not found under Resource roots; skip path resolution check" );
	}
	// FileUtil::normalizePath lowercases on Windows.
	const std::string lowerPath = sw::StringUtil::toLower( shaderPath );
	SW_EXPECT_TRUE_MSG( lowerPath.find( "samplecompute.hlsl" ) != std::string::npos, shaderPath.c_str() );

	const std::string shaderPathAgain = sw::ResourceUtil::getResourcePath( "Shaders/SampleCompute.hlsl" );
	SW_EXPECT_STREQ( shaderPath.c_str(), shaderPathAgain.c_str() );
}
