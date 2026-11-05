#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/InputMap/Model/InputMapDocument.h"

using namespace sw;
using namespace sw::editor;

// 바뀐 편집만 저장하지 않은 표시를 켠다 — 패널이 후속 처리를 쓰지 않아도 표시가 맞다.
SW_TEST_CASE( InputMapDocumentTest, EditsThatChangeNothingStayClean )
{
    InputMapDocument document;
    SW_EXPECT_FALSE( document.isDirty() );
    SW_EXPECT_FALSE( document.createAction( "", 0 ) );
    SW_EXPECT_FALSE( document.setBindingNegate( hashed_string( "Missing" ), 0, true ) );
    SW_EXPECT_FALSE( document.unbind( hashed_string( "Missing" ), 0 ) );
    SW_EXPECT_FALSE( document.isDirty() );
}

SW_TEST_CASE( InputMapDocumentTest, EditsThatChangeTheMapMarkItDirty )
{
    InputMapDocument created;
    SW_EXPECT_TRUE( created.createAction( "Jump", 0 ) );
    SW_EXPECT_TRUE( created.isDirty() );

    InputMapDocument layer;
    layer.setLayerEnabled( hashed_string( "Gameplay" ), false );
    SW_EXPECT_TRUE( layer.isDirty() );

    InputMapDocument reset;
    reset.resetAllBindingsToDefault();
    SW_EXPECT_TRUE( reset.isDirty() );
}

// 키 잡기: 바인딩이 없는 액션이면 바뀌지 않고, 잡기는 끝난다.
SW_TEST_CASE( InputMapDocumentTest, KeyCaptureWithoutABindingLeavesTheMapClean )
{
    InputMapDocument document;
    document.selectAction( "Missing" );
    document.beginKeyCapture( 0 );
    SW_EXPECT_TRUE( document.isCapturingKey() );
    SW_EXPECT_FALSE( document.finishKeyCapture( Key::A ) );
    SW_EXPECT_FALSE( document.isCapturingKey() );
    SW_EXPECT_FALSE( document.isDirty() );
}
