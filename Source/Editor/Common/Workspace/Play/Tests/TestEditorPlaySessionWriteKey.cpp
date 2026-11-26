/**
 * @file TestEditorPlaySessionWriteKey.cpp
 * @brief 플레이 세션 쓰기 열쇠 계약(`EditorPlaySessionWriteKey`)을 봅니다 — 게임 스레드 입구(친구) 밖에서는 만들 수 없어 ViewModel · 패널이 세션을 바로 바꾸지 못한다.
 * @details 계약은 형식이 지킨다 — 아래 정적 단언이 빌드에서 멈춘다. 쓰기 함수가 열쇠를 받는지는 함수 형식으로 본다.
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include <type_traits>

using namespace sw;
using namespace sw::editor;

// 친구가 아닌 곳에서는 열쇠를 만들 수 없다 — 기본 생성자가 비공개이고, 집합체가 아니라 `{}` 로도 건너뛰지 못한다(C++17 집합체 규칙).
static_assert( std::is_default_constructible_v<EditorPlaySessionWriteKey> == false, "only game-thread entry points may create the play session write key" );
static_assert( std::is_aggregate_v<EditorPlaySessionWriteKey> == false, "an aggregate key would let `{}` bypass the private constructor" );
// 열쇠를 받은 쪽은 넘겨 쓸 수 있다(값 복사).
static_assert( std::is_trivially_copyable_v<EditorPlaySessionWriteKey>, "the key is passed by value" );
// 세션을 바꾸는 정적 함수는 열쇠를 받는다 — 열쇠 없는 모양이 다시 생기면 이 단언이 멈춘다.
static_assert( std::is_same_v<decltype( &EditorPlaySession::play ), void ( * )( EditorPlaySessionWriteKey )>, "play needs the write key" );
static_assert( std::is_same_v<decltype( &EditorPlaySession::simulate ), void ( * )( EditorPlaySessionWriteKey )>, "simulate needs the write key" );
static_assert( std::is_same_v<decltype( &EditorPlaySession::pause ), void ( * )( EditorPlaySessionWriteKey )>, "pause needs the write key" );
static_assert( std::is_same_v<decltype( &EditorPlaySession::stop ), void ( * )( EditorPlaySessionWriteKey )>, "stop needs the write key" );
static_assert( std::is_same_v<decltype( &EditorPlaySession::findData ), PlaySessionData* (*)( EditorPlaySessionWriteKey )>, "mutable session data needs the write key" );
// 씬 편집 막기는 열쇠를 받아서만 생기고, 쥔 객체(RAII)만 푼다 — 복사하면 두 번 풀리므로 옮기기만 된다.
static_assert( std::is_same_v<decltype( &EditorPlaySession::holdSceneEdits ), EditorSceneEditHold ( * )( EditorPlaySessionWriteKey )>, "holding scene edits needs the write key" );
static_assert( std::is_copy_constructible_v<EditorSceneEditHold> == false, "a copied hold would release twice" );
static_assert( std::is_nothrow_move_constructible_v<EditorSceneEditHold>, "a hold moves into its owner" );

/**
 * @brief [EditorPlaySessionWriteKeyTest] 열쇠 없이 쓰는 본체(`PlaySessionData&`)는 테스트가 가진 지역 상태만 바꾼다 — 에디터 범위의 세션은 열쇠를 받는 `findData` 로만 고칠 수 있다
 */
SW_TEST_CASE( EditorPlaySessionWriteKeyTest, BodiesTouchOnlyGivenData )
{
    PlaySessionData data;
    EditorPlaySession::setStartPosition( data, float3{ 1.0f, 2.0f, 3.0f } );
    SW_EXPECT_TRUE( data._bStartAtPosition == SW_TRUE );
    EditorPlaySession::clearStartPosition( data );
    SW_EXPECT_TRUE( data._bStartAtPosition == SW_FALSE );
    EditorSceneEditHold empty; // 쥐지 않은 막기는 놓아도 아무 일이 없다
    empty.release();
    SW_EXPECT_FALSE( empty.isHeld() );
}

/**
 * @brief [EditorPlaySessionWriteKeyTest] UI 가 읽는 플레이 상태 값(`EditorPlayStateSnapshot`)의 판정은 세션 질의와 같은 규칙이다 — Step 대기는 재생 중, Simulate 는 플레이어 조종이 아님, 막기는 편집 거절
 */
SW_TEST_CASE( EditorPlaySessionWriteKeyTest, UIStateRules )
{
    EditorPlayStateSnapshot state;
    SW_EXPECT_TRUE( state.isStopped() );
    SW_EXPECT_TRUE( state.areSceneEditsAllowed() );
    SW_EXPECT_FALSE( EditorPlaySession::getUIState().isPlaying() ); // 에디터 범위가 없으면 "정지"

    state._state        = PlaySessionState::Paused;
    state._bPendingStep = SW_TRUE;
    SW_EXPECT_TRUE( state.isPlaying() );
    SW_EXPECT_TRUE( state.isPlayerActive() );
    SW_EXPECT_FALSE( state.areSceneEditsAllowed() );

    state._bSimulate = SW_TRUE;
    SW_EXPECT_TRUE( state.isSimulating() );
    SW_EXPECT_FALSE( state.isPlayerActive() );

    EditorPlayStateSnapshot held;
    held._bSceneEditsHeld = SW_TRUE;
    SW_EXPECT_FALSE( held.areSceneEditsAllowed() );
}
