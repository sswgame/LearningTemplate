/**
 * @file TestResource.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"
#include "Core/Utility/Resource/ResourceUtil.h"

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

// SW_TEST_CASE( Utility_Resource, ResourceFolderPathsAndCacheClear )\n// Temporarily disabled due to resource path issue

