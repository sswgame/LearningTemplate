// 씬 뷰 마우스 소유 — 소유자는 하나이고 새로 가져가면 앞 소유자는 잃으며, 소유자만 풀 수 있고, 한 프레임 동안 다시 잡지 않으면 다음 프레임이 거둔다(nogpu).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Viewport/Model/EditorViewportMouseCapture.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorViewportMouseCaptureTest] isCapturedBy 는 지금 소유자에게만 참이다 — 가져간 쪽이 바뀌거나 프레임 회수로 잃으면 거짓이 되고, nullptr 는 소유자가 아니다
 */
SW_TEST_CASE( EditorViewportMouseCaptureTest, IsCapturedByFollowsTheCurrentOwner )
{
    EditorStateStore store;
    SubsystemScope   scope;
    SW_REQUIRE_TRUE( scope.bind( &store ) );
    const ScopedEditorScope scopedEditor( scope );

    int32 ownerA = 0;
    int32 ownerB = 0;
    EditorViewportMouseCapture::beginFrame( 1 );
    SW_EXPECT_FALSE( EditorViewportMouseCapture::isCapturedBy( &ownerA ) );
    SW_EXPECT_FALSE( EditorViewportMouseCapture::isCapturedBy( nullptr ) );

    EditorViewportMouseCapture::acquire( &ownerA );
    SW_EXPECT_TRUE( EditorViewportMouseCapture::isCapturedBy( &ownerA ) );
    SW_EXPECT_FALSE( EditorViewportMouseCapture::isCapturedBy( &ownerB ) );

    EditorViewportMouseCapture::acquire( &ownerB ); // 새 소유자가 가져가면 앞 소유자는 잃는다
    SW_EXPECT_FALSE( EditorViewportMouseCapture::isCapturedBy( &ownerA ) );
    EditorViewportMouseCapture::release( &ownerA ); // 소유자가 아닌 쪽의 풀기는 무시한다
    SW_EXPECT_TRUE( EditorViewportMouseCapture::isCapturedBy( &ownerB ) );

    EditorViewportMouseCapture::beginFrame( 2 ); // 프레임 1 에 잡았다 — 남는다
    SW_EXPECT_TRUE( EditorViewportMouseCapture::isCapturedBy( &ownerB ) );
    EditorViewportMouseCapture::beginFrame( 3 ); // 프레임 2 에 다시 잡지 않았다 — 거둔다
    SW_EXPECT_FALSE( EditorViewportMouseCapture::isCapturedBy( &ownerB ) );
    SW_EXPECT_FALSE( EditorViewportMouseCapture::isCaptured() );
}
