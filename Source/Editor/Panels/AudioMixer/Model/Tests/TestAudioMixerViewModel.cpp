// AudioMixerViewModelTest — 미터 채움 비율과, 버스 · 이벤트가 없을 때의 명령(오디오 서비스를 찾지 않는다)(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Panels/AudioMixer/Model/AudioMixerViewModel.h"

using namespace sw;
using namespace sw::editor;

/** @brief [AudioMixerViewModelTest] 채움은 바닥(dB)에서 0, 0 dB 에서 1 이고 그 사이는 선형이며 밖은 자른다 */
SW_TEST_CASE( AudioMixerViewModelTest, MeterFillIsLinearInDecibel )
{
    constexpr float32 kFloor = AudioMixerViewModel::kMeterFloorDb;
    SW_EXPECT_NEAR( 0.0f, AudioMixerViewModel::computeMeterFill( kFloor ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.5f, AudioMixerViewModel::computeMeterFill( kFloor * 0.5f ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, AudioMixerViewModel::computeMeterFill( 0.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, AudioMixerViewModel::computeMeterFill( kFloor - 10.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, AudioMixerViewModel::computeMeterFill( 6.0f ), 1.0e-6f );
}

/** @brief [AudioMixerViewModelTest] 버스 · 이벤트를 아직 읽지 않은 모델의 명령은 아무것도 하지 않는다(서비스 없이도 안전하다) */
SW_TEST_CASE( AudioMixerViewModelTest, CommandsWithoutBusesDoNothing )
{
    AudioMixerViewModel model;
    SW_EXPECT_TRUE( model.getBusStatuses().empty() );
    SW_EXPECT_TRUE( model.getEventNames().empty() );
    model.setBusVolume( 0, 0.5f );
    model.setBusMuted( 3, true );
    model.setBusSolo( 3, true );
    model.selectEvent( 5 );
    model.postSelectedEvent();
    SW_EXPECT_EQUAL( 5u, model.getSelectedEvent() );
}
