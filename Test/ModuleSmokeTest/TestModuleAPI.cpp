/**
 * @file TestModuleAPI.cpp
 * @brief Dev MODULE DLL / Shipping STATIC fillGameAPI (및 Dev Editor) 스모크
 */
#include "TestFramework.h"

#include "Runtime/EditorAPI.h"
#include "Runtime/GameAPI.h"

#if defined( SW_SHIPPING )

SW_TEST_CASE( ModuleAPI, FillGameAPI_ShippingStatic )
{
	sw::GameAPI api{};
	SW_EXPECT_TRUE( fillGameAPI( &api ) );
	SW_EXPECT_TRUE( api.create != nullptr );
	SW_EXPECT_TRUE( api.destroy != nullptr );
	SW_EXPECT_TRUE( api.initialize != nullptr );
	SW_EXPECT_TRUE( api.shutdown != nullptr );
	SW_EXPECT_TRUE( api.update != nullptr );

	sw::GameHandle game = api.create();
	SW_EXPECT_TRUE( game != nullptr );
	if ( game != nullptr )
		api.destroy( game );
}

#else

#include "Core/Utility/File/FileUtil.h"

namespace
{
	std::string modulePath( const char* baseName )
	{
	#if defined( SW_TEST_MODULE_DIR )
		const std::string dir = SW_TEST_MODULE_DIR;
	#else
		const std::string dir = sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() );
	#endif
		return dir + "/" + sw::FileUtil::formatSharedLibraryName( baseName );
	}

	void* loadModule( const char* baseName )
	{
		const std::string path = modulePath( baseName );
		SW_EXPECT_TRUE( sw::FileUtil::isFileExist( path ) );
		if ( sw::FileUtil::isFileExist( path ) == false )
			return nullptr;
		return sw::FileUtil::loadDynamicLibrary( path );
	}
} // namespace

SW_TEST_CASE( ModuleAPI, FillGameAPI )
{
	void* handle = loadModule( "SWGame" );
	SW_EXPECT_TRUE( handle != nullptr );
	if ( handle == nullptr )
		return;

	auto* fill = reinterpret_cast<sw::PFN_FillGameAPI>(
		sw::FileUtil::getDynamicSymbol( handle, "fillGameAPI" ) );
	SW_EXPECT_TRUE( fill != nullptr );
	if ( fill == nullptr )
	{
		sw::FileUtil::freeDynamicLibrary( handle );
		return;
	}

	sw::GameAPI api{};
	SW_EXPECT_TRUE( fill( &api ) );
	SW_EXPECT_TRUE( api.create != nullptr );
	SW_EXPECT_TRUE( api.destroy != nullptr );
	SW_EXPECT_TRUE( api.initialize != nullptr );
	SW_EXPECT_TRUE( api.shutdown != nullptr );
	SW_EXPECT_TRUE( api.update != nullptr );

	sw::GameHandle game = api.create();
	SW_EXPECT_TRUE( game != nullptr );
	if ( game != nullptr )
		api.destroy( game );

	sw::FileUtil::freeDynamicLibrary( handle );
}

SW_TEST_CASE( ModuleAPI, FillEditorAPI )
{
	void* handle = loadModule( "EditorModule" );
	SW_EXPECT_TRUE( handle != nullptr );
	if ( handle == nullptr )
		return;

	auto* fill = reinterpret_cast<sw::PFN_FillEditorAPI>(
		sw::FileUtil::getDynamicSymbol( handle, "fillEditorAPI" ) );
	SW_EXPECT_TRUE( fill != nullptr );
	if ( fill == nullptr )
	{
		sw::FileUtil::freeDynamicLibrary( handle );
		return;
	}

	sw::EditorAPI api{};
	SW_EXPECT_TRUE( fill( &api ) );
	SW_EXPECT_TRUE( api.create != nullptr );
	SW_EXPECT_TRUE( api.destroy != nullptr );
	SW_EXPECT_TRUE( api.initialize != nullptr );
	SW_EXPECT_TRUE( api.shutdown != nullptr );
	SW_EXPECT_TRUE( api.render != nullptr );
	// create()/destroy() 는 ImGui·RHI 컨텍스트가 필요하므로 스모크에서는 테이블만 검증

	sw::FileUtil::freeDynamicLibrary( handle );
}

#endif // SW_SHIPPING
