#include "pch.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/EditorVisualizerToggles.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorVisualizerTogglesTest] 바꾼 기록이 없으면 등록 줄의 기본값이 켬/끔이다
 */
SW_TEST_CASE( EditorVisualizerTogglesTest, DefaultsComeFromTheRegistration )
{
    const EditorVisualizerRegistration kOnByDefault{
        { "on", 100 },
        nullptr,
        "On",
        "",
        true,
        nullptr
    };
    const EditorVisualizerRegistration kOffByDefault{
        { "off", 200 },
        nullptr,
        "Off",
        "",
        false,
        nullptr
    };
    const EditorVisualizerToggles toggles;
    SW_EXPECT_TRUE( toggles.isOn( kOnByDefault ) );
    SW_EXPECT_FALSE( toggles.isOn( kOffByDefault ) );
}

/**
 * @brief [EditorVisualizerTogglesTest] 켬/끔은 등록 순서가 아니라 id 를 따른다 — 확장 모듈이 사이에 줄을 끼워도 다른 시각화가 켜지지 않는다
 */
SW_TEST_CASE( EditorVisualizerTogglesTest, StateFollowsTheIDNotTheOrder )
{
    const EditorVisualizerRegistration kInserted{
        { "inserted", 150 },
        nullptr,
        "B",
        "",
        false,
        nullptr
    }; // 확장 모듈이 사이에 끼운 줄
    const EditorVisualizerRegistration kSecond{
        { "second", 200 },
        nullptr,
        "C",
        "",
        false,
        nullptr
    };
    EditorVisualizerToggles toggles;
    toggles.setOn( kSecond, true );
    SW_EXPECT_FALSE( toggles.isOn( kInserted ) ); // 비트 마스크였으면 둘째 자리가 끼운 줄로 옮겨 갔다
    SW_EXPECT_TRUE( toggles.isOn( kSecond ) );
    SW_EXPECT_EQUAL( 1u, toggles.getOverrideCount() );
    toggles.setOn( kSecond, false ); // 기본값과 같아지면 기록이 지워진다
    SW_EXPECT_FALSE( toggles.isOn( kSecond ) );
    SW_EXPECT_EQUAL( 0u, toggles.getOverrideCount() );
}
