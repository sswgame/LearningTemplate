// RetargetProfileEditModelTest — 리타깃 프로파일 창의 편집 연산은 모델이 이름과 함께 알린다(감사 E-16): 연산 한 번 → 알림 한 번, 문서 바꾸기(replace)는 알리지 않는다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"
#include "Editor/Common/Workspace/Undo/DocumentEditSpan.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/RetargetProfileEditModel.h"

using namespace sw;
using namespace sw::editor;

/** @brief [RetargetProfileEditModelTest] 같은 이름 짝짓기 켜기 한 번은 알림 한 번("Retarget same names")이고, 문서를 통째로 바꾸는 것은 편집이 아니다 */
SW_TEST_CASE( RetargetProfileEditModelTest, SetterNotifiesOnceAndReplaceDoesNot )
{
    uint32                     count{ 0 };
    string                     lastLabel;
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, [&count, &lastLabel]( string_view label )
    {
        ++count;
        lastLabel = string{ label };
    } ) };
    RetargetProfileEditModel   model;
    model.bindDocumentEditRecorder( recorder );
    model.replace( RetargetProfile{} );
    SW_EXPECT_EQUAL( count, 0u );
    model.setMapSameNames( true );
    SW_EXPECT_EQUAL( count, 1u );
    SW_EXPECT_TRUE( lastLabel == "Retarget same names" );
    SW_EXPECT_TRUE( model.getProfile().isMappingSameNames() );
}

/** @brief [RetargetProfileEditModelTest] 목표 자세 끌기는 끄는 동안 알림이 여러 번 와도 놓을 때 되돌리기 한 단계다(상호작용 구간 — `DocumentEditSpan`) */
SW_TEST_CASE( RetargetProfileEditModelTest, PoseDragIsOneUndoStep )
{
    RetargetProfileEditModel model;
    DocumentEditSpan         span;
    span.resetBaseline( model.getProfile().toXML() );
    bool       bInteracting{ true };
    uint32     stepCount{ 0 };
    uint32     notifyCount{ 0 };
    const auto capture = [&model]()
    { return model.getProfile().toXML(); };
    const DocumentEditRecorder recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, [&]( string_view label )
    {
        ++notifyCount;
        if ( span.notifyEdited( label, bInteracting, capture ).has_value() )
            ++stepCount;
    } ) };
    model.bindDocumentEditRecorder( recorder );

    for ( uint32 frameIndex = 1; frameIndex <= 5; ++frameIndex ) // 끄는 프레임마다 자세 사본을 넣는다(창의 `drawTargetPoseRows` 와 같다)
    {
        vector<RetargetPoseBone> listPose;
        listPose.push_back( RetargetPoseBone{
            hashed_string( "spine" ), EulerAngles::fromDegrees( float3{ static_cast<float32>( frameIndex ), 0.0f, 0.0f } ) } );
        model.setTargetPose( std::move( listPose ) );
    }
    SW_EXPECT_EQUAL( notifyCount, 5u );
    SW_EXPECT_EQUAL( stepCount, 0u );
    bInteracting = false;
    if ( span.isHolding() && span.finish( capture() ).has_value() )
        ++stepCount;
    SW_EXPECT_EQUAL( stepCount, 1u );
}
