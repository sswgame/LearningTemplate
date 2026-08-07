/**
 * @file main.cpp
 * @brief Toy Engine 런타임 클라이언트 진입점
 */
#include "App.h"

int main( int argc, char* argv[] )
{
	// g_RHIBackend 변경 시 App을 새로 만들어 파이프라인을 다시 올린다.
	for ( ;; )
	{
		sw::App app;
		if ( app.initialize( argc, argv ) == false )
			return -1;

		app.run();
		const bool bRestart = app.shouldRestartForBackendChange();
		app.shutdown();

		if ( bRestart == false )
			break;
	}

	return 0;
}
