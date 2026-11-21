// AudioGraphViewModelTest — 문서 편집 알림은 모델이 이름과 함께 낸다(감사 E-16): 연산 한 번 → 그리기 끝(publishEdit)에 알림 한 번, 이름은 모델이 정한다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"
#include "Editor/Panels/AudioGraph/Model/AudioGraphViewModel.h"

using namespace sw;
using namespace sw::editor;

/** @brief [AudioGraphViewModelTest] 노드 넣기 한 번은 알림 한 번이고 이름은 "Add Audio Graph Node" 다. 고친 것이 없으면 알리지 않는다 */
SW_TEST_CASE( AudioGraphViewModelTest, AddNodeNotifiesOnceWithItsName )
{
    uint32                     count{ 0 };
    string                     lastLabel;
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, [&count, &lastLabel]( string_view label )
    {
        ++count;
        lastLabel = string{ label };
    } ) };
    AudioGraphViewModel        model;
    model.bindDocumentEditRecorder( recorder );
    model.publishEdit();
    SW_EXPECT_EQUAL( count, 0u );

    SW_REQUIRE_TRUE( model.addNode( AudioGraphNodeKind::Sine, float2{ 0.0f, 0.0f } ) );
    model.publishEdit();
    SW_EXPECT_EQUAL( count, 1u );
    SW_EXPECT_TRUE( lastLabel == "Add Audio Graph Node" );
    model.publishEdit(); // 이미 넘겼다
    SW_EXPECT_EQUAL( count, 1u );
}
