#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/RenderTarget/Model/RenderTargetViewSettings.h"

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( RenderTargetViewSettingsTest, DepthPlanesStayPositiveAndOrdered )
{
    RenderTargetViewSettings settings;
    settings.setDepthPlanes( 0.0f, -5.0f );
    SW_EXPECT_TRUE( settings._nearPlane > 0.0f );
    SW_EXPECT_TRUE( settings._farPlane > settings._nearPlane );
    settings.setDepthPlanes( 1.0f, 100.0f );
    SW_EXPECT_EQUAL( 1.0f, settings._nearPlane );
    SW_EXPECT_EQUAL( 100.0f, settings._farPlane );
}
