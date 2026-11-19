// 뷰포트 시각화 켬/끔 — 등록 줄의 기본값 위에 사용자가 바꾼 것만 id 로 남고, 기본값으로 돌아오면 기록을 지운다(nogpu, ImGui 없음).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorVisualizerToggles.h"

using sw::editor::EditorVisualizerRegistration;
using sw::editor::EditorVisualizerToggles;

namespace
{
    struct EditorVisualizerTogglesTestInternal
    {
        static EditorVisualizerRegistration makeRegistration( const utf8* pID, bool bDefaultOn )
        {
            EditorVisualizerRegistration registration{};
            registration._pID        = pID;
            registration._bDefaultOn = bDefaultOn ? SW_TRUE : SW_FALSE;
            return registration;
        }
    };
} // namespace

/**
 * @brief [EditorVisualizerTogglesTest] 바꾼 기록이 없으면 기본값이고, 기본값과 다르게 바꾸면 그 id 만 기록하며, 기본값으로 되돌리면 기록이 사라진다
 */
SW_TEST_CASE( EditorVisualizerTogglesTest, OnlyOverridesOfTheDefaultAreKept )
{
    using Internal                                  = EditorVisualizerTogglesTestInternal;
    const EditorVisualizerRegistration offByDefault = Internal::makeRegistration( "Test.Grid", false );
    const EditorVisualizerRegistration onByDefault  = Internal::makeRegistration( "Test.Bounds", true );

    EditorVisualizerToggles toggles;
    SW_EXPECT_FALSE( toggles.isOn( offByDefault ) );
    SW_EXPECT_TRUE( toggles.isOn( onByDefault ) );

    toggles.setOn( onByDefault, true ); // 기본값과 같다 — 남길 것이 없다
    SW_EXPECT_EQUAL( toggles.getOverrideCount(), 0u );

    toggles.setOn( offByDefault, true );
    toggles.setOn( onByDefault, false );
    SW_EXPECT_EQUAL( toggles.getOverrideCount(), 2u );
    SW_EXPECT_TRUE( toggles.isOn( offByDefault ) );
    SW_EXPECT_FALSE( toggles.isOn( onByDefault ) );

    toggles.setOn( offByDefault, false );
    SW_EXPECT_EQUAL( toggles.getOverrideCount(), 1u );
    SW_EXPECT_FALSE( toggles.isOn( offByDefault ) );
    SW_EXPECT_FALSE( toggles.isOn( onByDefault ) ); // 다른 id 의 기록은 그대로다
}
