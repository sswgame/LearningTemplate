/**
 * @file TestCommandLine.cpp
 * @brief Auto-generated documentation header
 */
#include "TestFramework.h"
#include "Core/Utility/CommandLine/CommandLineManager.h"

SW_TEST_CASE( Utility_CommandLine, DefaultValueForWidthExists )
{
	sw::CommandLineManager cmdManager;
	cmdManager.initialize();

	int32 width	   = 0;
	bool  hasWidth = cmdManager.getArgument( sw::CommandLineArgument::WIDTH, width );
	SW_EXPECT_TRUE( hasWidth );
	SW_EXPECT_EQUAL( 1280, width );
}

SW_TEST_CASE( Utility_CommandLine, ParseWidthArgument )
{
	sw::CommandLineManager cmdManager;
	cmdManager.initialize();

	char* argv[] = {
		const_cast<char*>( "TestApp.exe" ),
		const_cast<char*>( "WIDTH=1920" ),
	};
	cmdManager.parse( 2, argv );

	int32 width	   = 0;
	bool  hasWidth = cmdManager.getArgument( sw::CommandLineArgument::WIDTH, width );
	SW_EXPECT_TRUE( hasWidth );
	SW_EXPECT_EQUAL( 1920, width );
}

SW_TEST_CASE( Utility_CommandLine, ParseSynonymArgument )
{
	sw::CommandLineManager cmdManager;
	cmdManager.initialize();

	char* argv[] = {
		const_cast<char*>( "TestApp.exe" ),
		const_cast<char*>( "W=800" ),
	};
	cmdManager.parse( 2, argv );

	int32 width	   = 0;
	bool  hasWidth = cmdManager.getArgument( sw::CommandLineArgument::WIDTH, width );
	SW_EXPECT_TRUE( hasWidth );
	SW_EXPECT_EQUAL( 800, width );
}

SW_TEST_CASE( Utility_CommandLine, GetArgumentNotFoundReturnsFalse )
{
	sw::CommandLineManager cmdManager;
	cmdManager.initialize();

	std::string ip;
	bool		hasIP = cmdManager.getArgument( sw::CommandLineArgument::IP, ip );
	SW_EXPECT_FALSE( hasIP );
}

SW_TEST_CASE( Utility_CommandLine, StringKeyAndUtf16Parse )
{
	sw::CommandLineManager cmdManager;
	cmdManager.initialize();

	utf16* wargv[] = {
		const_cast<utf16*>( L"TestApp.exe" ),
		const_cast<utf16*>( L"WIDTH=2560" ),
	};
	cmdManager.parse( 2, wargv );

	int32 width	   = 0;
	bool  hasWidth = cmdManager.getArgument( std::string_view( "WIDTH" ), width );
	SW_EXPECT_TRUE( hasWidth );
	SW_EXPECT_EQUAL( 2560, width );
}

SW_TEST_CASE( Utility_CommandLine, RHIBackendCommandLineFlagsAndSynonyms )
{
	{
		sw::CommandLineManager cmdManager;
		cmdManager.initialize();
		char* argv[] = {
			const_cast<char*>( "TestApp.exe" ),
			const_cast<char*>( "VULKAN" ),
		};
		cmdManager.parse( 2, argv );

		bool bVk = false;
		SW_EXPECT_TRUE( cmdManager.getArgument( sw::CommandLineArgument::VULKAN, bVk ) );
		SW_EXPECT_TRUE( bVk );
	}

	{
		sw::CommandLineManager cmdManager;
		cmdManager.initialize();
		char* argv[] = {
			const_cast<char*>( "TestApp.exe" ),
			const_cast<char*>( "dx11" ),
		};
		cmdManager.parse( 2, argv );

		bool bDx11 = false;
		SW_EXPECT_TRUE( cmdManager.getArgument( sw::CommandLineArgument::DIRECTX_11, bDx11 ) );
		SW_EXPECT_TRUE( bDx11 );
	}

	{
		sw::CommandLineManager cmdManager;
		cmdManager.initialize();
		char* argv[] = {
			const_cast<char*>( "TestApp.exe" ),
			const_cast<char*>( "gl" ),
		};
		cmdManager.parse( 2, argv );

		bool bGl = false;
		SW_EXPECT_TRUE( cmdManager.getArgument( sw::CommandLineArgument::OPENGL, bGl ) );
		SW_EXPECT_TRUE( bGl );
	}
}
