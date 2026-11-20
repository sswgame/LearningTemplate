/**
 * @file TestEditorPlaySessionCarry.cpp
 * @brief 플레이 세션을 에디터 모듈 리로드 너머로 맡겼다 되찾는 왕복(`EditorPlaySession::carryOut` · `carryIn`)을 봅니다(nogpu).
 * @details 상태는 맡김 칸(`IModuleStateStore`)에 바이트로, 롤백 스냅샷은 엔진 형 그대로 넘어간다 — 둘 다 에디터 컨텍스트 없이 본다.
 */
#include "pch.h"

#include "Core/Container/map.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "ModuleAPI/Service/IModuleStateStore.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorPlaySessionCarryTestInternal
    {
        /** @brief 호스트 맡김 칸의 테스트 대역입니다(`ModuleStateStore` 와 같은 동작). */
        class FakeStateStore final : public IModuleStateStore
        {
        public:
            void               store( string_view key, vector<uint8>&& bytes ) override { _mapStored[string( key )] = std::move( bytes ); }
            [[nodiscard]] bool take( string_view key, vector<uint8>& outBytes ) override
            {
                const auto it = _mapStored.find( string( key ) );
                if ( it == _mapStored.end() )
                    return false;
                outBytes = std::move( it->second );
                _mapStored.erase( it );
                return true;
            }
            size_t getStoredCount() const { return _mapStored.size(); }

        private:
            map<string, vector<uint8>> _mapStored;
        };

        /** @brief 플레이 중인 세션 하나를 꾸밉니다 — 롤백 스냅샷 · 남길 변경 · 맡기지 않을 편집 막기와 거절 기록까지. */
        static PlaySessionData makePlayingSession()
        {
            PlaySessionData data;
            data._state              = PlaySessionState::Paused;
            data._queuedState        = PlaySessionState::Playing;
            data._pendingStepCount   = 3;
            data._heldSceneEditCount = 7;
            data._startPosition      = float3{ 1.0f, 2.0f, 3.0f };
            data._bSimulate          = SW_TRUE;
            data._bStartAtPosition   = SW_TRUE;
            data._bStartMovePending  = SW_TRUE;
            data._bEjected           = SW_TRUE;
            data._sceneEditHoldCount = 2;
            data._lastRefusedAction  = "Move";

            ScenePlaySnapshot::ObjectSnapshot entry;
            entry._identity._objectID = 42;
            entry._name               = "Crate";
            entry._bytes              = vector<uint8>{ 1, 2, 3 };
            data._snapshot._listSnapshot.push_back( entry );
            entry._name = "Kept";
            data._snapshot._listKept.push_back( entry );
            data._snapshot._sceneGeneration = 5;
            data._snapshot._sceneName       = "edit";
            data._snapshot._sceneSourcePath = "maps/edit.scene.xml";
            data._snapshot._bHasSnapshot    = SW_TRUE;
            return data;
        }
    };
} // namespace

/**
 * @brief [EditorPlaySessionCarryTest] 맡긴 세션을 새 인스턴스가 그대로 되찾는다 — 상태 · Step · 시작 위치 · 플래그와 롤백 스냅샷(편집 씬 백업)이 같고, 편집 막기 · 거절 기록은 넘어가지 않으며, 칸은 한 번만 꺼낸다
 */
SW_TEST_CASE( EditorPlaySessionCarryTest, CarryOutThenInRoundTrips )
{
    using Internal = EditorPlaySessionCarryTestInternal;
    Internal::FakeStateStore store;
    PlaySessionData          oldInstance = Internal::makePlayingSession();

    unique_ptr<ScenePlaySnapshot> held = EditorPlaySession::carryOut( oldInstance, store );
    SW_EXPECT_EQUAL( store.getStoredCount(), 1u );
    SW_EXPECT_TRUE( held != nullptr );
    SW_EXPECT_TRUE( oldInstance._state == PlaySessionState::Stopped ); // 옛 인스턴스는 빈 세션으로 남는다
    SW_EXPECT_TRUE( oldInstance._snapshot._listSnapshot.empty() );

    PlaySessionData newInstance;
    SW_EXPECT_TRUE( EditorPlaySession::carryIn( newInstance, store, std::move( held ) ) );
    SW_EXPECT_TRUE( newInstance._state == PlaySessionState::Paused );
    SW_EXPECT_TRUE( newInstance._queuedState == PlaySessionState::Playing );
    SW_EXPECT_EQUAL( newInstance._pendingStepCount, 3u );
    SW_EXPECT_EQUAL( newInstance._heldSceneEditCount, 7u );
    SW_EXPECT_EQUAL( newInstance._startPosition.y, 2.0f );
    SW_EXPECT_TRUE( newInstance._bSimulate == SW_TRUE );
    SW_EXPECT_TRUE( newInstance._bStartAtPosition == SW_TRUE );
    SW_EXPECT_TRUE( newInstance._bStartMovePending == SW_TRUE );
    SW_EXPECT_TRUE( newInstance._bEjected == SW_TRUE );
    SW_EXPECT_TRUE( newInstance._bStartQueued == SW_FALSE );
    SW_EXPECT_EQUAL( newInstance._sceneEditHoldCount, 0u ); // 막기를 쥔 작업은 옛 인스턴스와 함께 사라졌다
    SW_EXPECT_TRUE( newInstance._lastRefusedAction.empty() );

    const ScenePlaySnapshot& snapshot = newInstance._snapshot;
    SW_EXPECT_TRUE( snapshot._bHasSnapshot == SW_TRUE );
    SW_EXPECT_EQUAL( snapshot._sceneGeneration, 5u );
    SW_EXPECT_TRUE( snapshot._sceneName == "edit" );
    SW_EXPECT_TRUE( snapshot._sceneSourcePath == "maps/edit.scene.xml" );
    SW_EXPECT_EQUAL( snapshot._listSnapshot.size(), 1u );
    SW_EXPECT_EQUAL( snapshot._listSnapshot[0]._identity._objectID, 42u );
    SW_EXPECT_EQUAL( snapshot._listSnapshot[0]._bytes.size(), 3u );
    SW_EXPECT_EQUAL( snapshot._listKept.size(), 1u );

    PlaySessionData third;
    SW_EXPECT_FALSE( EditorPlaySession::carryIn( third, store, nullptr ) ); // 칸은 비었다 — 다음 인스턴스는 정지로 시작한다
    SW_EXPECT_TRUE( third._state == PlaySessionState::Stopped );
}

/**
 * @brief [EditorPlaySessionCarryTest] 읽을 수 없는 칸(형식 판이 다름)은 세션을 건드리지 않고 false 다. 스냅샷 칸을 잃었으면 플레이는 잇되 되돌릴 스냅샷이 없다
 */
SW_TEST_CASE( EditorPlaySessionCarryTest, UnreadableOrMissingSnapshot )
{
    using Internal = EditorPlaySessionCarryTestInternal;
    SW_EXPECT_LOG( "EditorPlaySession.CarriedPlaySessionUnreadable" );
    SW_EXPECT_LOG( "EditorPlaySession.CarriedPlaySnapshotMissing" );
    Internal::FakeStateStore store;
    store.store( "editor.playSession", vector<uint8>{ 0xFF, 0xFF, 0xFF, 0xFF } );
    PlaySessionData data;
    SW_EXPECT_FALSE( EditorPlaySession::carryIn( data, store, make_unique<ScenePlaySnapshot>() ) );
    SW_EXPECT_TRUE( data._state == PlaySessionState::Stopped );

    PlaySessionData playing = Internal::makePlayingSession();
    (void)EditorPlaySession::carryOut( playing, store ); // 스냅샷을 맡긴 칸을 잃은 경우
    PlaySessionData resumed;
    SW_EXPECT_TRUE( EditorPlaySession::carryIn( resumed, store, nullptr ) );
    SW_EXPECT_TRUE( resumed._state == PlaySessionState::Paused );
    SW_EXPECT_TRUE( resumed._snapshot._bHasSnapshot == SW_FALSE );
}
