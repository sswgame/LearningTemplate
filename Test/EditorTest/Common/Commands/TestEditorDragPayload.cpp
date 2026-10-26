#include "pch.h"

#include "Editor/Common/Commands/EditorDragPayload.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorDragPayloadTest] 받을 종류 집합은 넣은 종류만 담고, 빈 집합은 아무것도 받지 않는다
 * @details 받는 쪽이 페이로드 이름 문자열을 하나씩 비교하던 것을 종류 집합 하나로 바꾼다(EditorUX 10-4). 집합 밖 종류는 어댑터가 거절한다.
 */
SW_TEST_CASE( EditorDragPayloadTest, KindSetContainsOnlyListedKinds )
{
    constexpr EditorPayloadKindSet kSceneDrop{ EditorPayloadKind::Asset, EditorPayloadKind::GameObject };
    static_assert( kSceneDrop.contains( EditorPayloadKind::Asset ), "constexpr 로 만들 수 있어야 한다" );
    SW_EXPECT_TRUE( kSceneDrop.contains( EditorPayloadKind::GameObject ) );
    SW_EXPECT_FALSE( kSceneDrop.contains( EditorPayloadKind::WidgetType ) );
    SW_EXPECT_FALSE( kSceneDrop.isEmpty() );

    const EditorPayloadKindSet none{};
    SW_EXPECT_TRUE( none.isEmpty() );
    SW_EXPECT_FALSE( none.contains( EditorPayloadKind::Asset ) );

    // 페이로드는 POD 라 ImGui 페이로드 버퍼에 그대로 복사된다.
    static_assert( std::is_trivially_copyable_v<EditorDragPayload>, "페이로드는 복사만으로 옮겨져야 한다" );
}
