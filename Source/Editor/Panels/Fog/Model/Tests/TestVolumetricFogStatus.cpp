#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Fog/Model/VolumetricFogStatus.h"

using namespace sw;
using namespace sw::editor;

// 모든 상태가 문장을 갖고(빈 줄로 알리지 않는다), 켜려다 못 켠 상태만 문제로 보이며, 진단 보기는 볼륨이 돌 때만 쓴다.
SW_TEST_CASE( VolumetricFogStatusTest, EveryStateExplainsItself )
{
    const FogVolumeState arrState[] = { FogVolumeState::NoFog, FogVolumeState::Disabled, FogVolumeState::Underwater, FogVolumeState::NoDevice,
                                        FogVolumeState::MissingPipeline, FogVolumeState::BufferFailed, FogVolumeState::Active };
    for ( const FogVolumeState state : arrState )
    {
        const utf8* pText = VolumetricFogStatus::describe( state );
        SW_EXPECT_TRUE( pText != nullptr && pText[0] != '\0' );
        SW_EXPECT_EQUAL( state == FogVolumeState::Active, VolumetricFogStatus::canShowVolumeViews( state ) );
    }
    SW_EXPECT_TRUE( VolumetricFogStatus::getSeverity( FogVolumeState::MissingPipeline ) == VolumetricFogSeverity::Problem );
    SW_EXPECT_TRUE( VolumetricFogStatus::getSeverity( FogVolumeState::BufferFailed ) == VolumetricFogSeverity::Problem );
    SW_EXPECT_TRUE( VolumetricFogStatus::getSeverity( FogVolumeState::Disabled ) == VolumetricFogSeverity::Neutral );
    SW_EXPECT_TRUE( VolumetricFogStatus::getSeverity( FogVolumeState::Active ) == VolumetricFogSeverity::Active );
}
