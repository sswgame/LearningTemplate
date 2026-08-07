/**
 * @file main.cpp
 * @brief Toy Engine 런타임 클라이언트 진입점
 */
#include "App.h"

int main( int argc, char* argv[] )
{
	sw::App app;
	if ( app.initialize( argc, argv ) == false )
		return -1;

	app.run();
	app.shutdown();
	return 0;
}
