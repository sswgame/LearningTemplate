/**
 * @file TestEditorDragPayload.cpp
 * @brief 끌어 놓기 받는 쪽의 종류 거르기(`EditorPayloadKindSet`)를 봅니다 — 받는 쪽은 이름 문자열이 아니라 종류 집합으로 거르고, 집합 밖 종류는 놓을 자리로 보이지 않는다(R11).
 * @details ImGui 없이 값 형식만 본다. 놓을 자리를 열기 전의 내용 거르기(확장자 · 에셋 종류)는 `EditorAssetFilter` · `PropertyGridViewModel::canAcceptAssetPath` 가 맡는다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/EditorDragPayload.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorDragPayloadTest] 받는 종류 집합은 적은 종류만 받는다 — 에셋 칸은 오브젝트를, 계층 행은 위젯 타입을 받지 않는다
 */
SW_TEST_CASE( EditorDragPayloadTest, KindSetAcceptsOnlyListedKinds )
{
    constexpr EditorPayloadKindSet assetField{ EditorPayloadKind::Asset };
    constexpr EditorPayloadKindSet hierarchyRow{ EditorPayloadKind::GameObject, EditorPayloadKind::Asset };
    constexpr EditorPayloadKindSet none{};

    SW_EXPECT_TRUE( assetField.contains( EditorPayloadKind::Asset ) );
    SW_EXPECT_FALSE( assetField.contains( EditorPayloadKind::GameObject ) );
    SW_EXPECT_FALSE( assetField.contains( EditorPayloadKind::WidgetType ) );
    SW_EXPECT_TRUE( hierarchyRow.contains( EditorPayloadKind::GameObject ) );
    SW_EXPECT_TRUE( hierarchyRow.contains( EditorPayloadKind::Asset ) );
    SW_EXPECT_FALSE( hierarchyRow.contains( EditorPayloadKind::WidgetType ) );
    SW_EXPECT_TRUE( none.isEmpty() );
    SW_EXPECT_FALSE( none.contains( EditorPayloadKind::Asset ) );
    SW_EXPECT_FALSE( assetField.isEmpty() );
}

/**
 * @brief [EditorDragPayloadTest] 페이로드는 문자열 · 포인터 없는 값이라 ImGui 버퍼에 바이트째 복사된다 — 복사한 값이 종류 · id 를 그대로 든다
 */
SW_TEST_CASE( EditorDragPayloadTest, PayloadCopiesAsBytes )
{
    static_assert( std::is_trivially_copyable_v<EditorDragPayload>, "페이로드는 ImGui 버퍼에 memcpy 된다" );
    const EditorDragPayload source{ EditorPayloadKind::GameObject, 0x1234'5678'9ABCull };
    EditorDragPayload       copied{};
    std::memcpy( &copied, &source, sizeof( copied ) );
    SW_EXPECT_TRUE( copied._kind == EditorPayloadKind::GameObject );
    SW_EXPECT_EQUAL( source._id, copied._id );
}
