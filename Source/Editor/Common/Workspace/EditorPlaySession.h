/**
 * @file EditorPlaySession.h
 * @brief 에디터 플레이(PIE) 세션 및 씬 스냅샷/복구 관리자
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Object/GameObject/ObjectStateSerializer.h"

namespace sw
{
    class CameraComponent;
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    enum class PlaySessionState : uint8
    {
        Stopped = 0,
        Playing,
        Paused
    };

    /**
     * @struct PlaySessionData
     * @brief 플레이 세션이 들고 있는 상태 전부입니다. **소유는 `EditorContext`** 입니다.
     * @details 컨텍스트가 들므로 컨텍스트와 함께 생기고 사라집니다. 파일 정적 변수로 두면 수명이 아무에게도 속하지 않아,
     *          컨텍스트가 다시 만들어져도 앞 세션의 스냅샷과 재생 상태가 그대로 남습니다.
     */
    struct PlaySessionData
    {
        /**
         * @brief 롤백용 오브젝트 스냅샷 하나입니다.
         * @details 런타임 id(`_identity`)를 같이 적어 둡니다. 정지할 때 상태를 되돌리며 컴포넌트를 다시 만드는데, 원래 id 를
         *          되살려야 플레이 전에 들고 있던 핸들(선택 · 씬의 활성 카메라)이 그대로 이어집니다.
         */
        struct ObjectSnapshot
        {
            ObjectIdentity _identity;
            string         _name;
            vector<uint8>  _bytes;
            string         _prefabPath; ///< 씬이 이 오브젝트에 매어 둔 프리팹 경로(`Scene::getEntityPrefabPath`). 씬을 다시 세울 때 되살린다
        };

        vector<ObjectSnapshot> _listSnapshot;
        /**
         * @brief 스냅샷을 찍을 때의 활성 씬입니다(`SceneManager::getSceneGeneration` · 이름 · 소스 경로).
         * @details Stop 때 활성 씬의 세대가 다르면 플레이 중에 씬이 바뀐 것이다(게임 코드가 다음 레벨을 열었다). 그때는 편집하던 씬을 이 이름 ·
         *          소스 경로로 다시 세운 뒤 되돌린다(`EditorPlaySession::restoreSnapshot`).
         */
        uint64 _sceneGeneration;
        string _sceneName;
        string _sceneSourcePath;
        /** @brief "카메라 위치에서 시작" 의 위치입니다(`_bStartAtPosition` 일 때만 뜻이 있다). */
        float3 _startPosition;
        /** @brief 아직 진행할 Step 프레임 수입니다. 0 이 되면 일시정지로 돌아간다(`consumePendingStep`). */
        uint32           _pendingStepCount;
        PlaySessionState _state{ PlaySessionState::Stopped };
        /** @brief 씬을 여는 중에 요청한 시작 상태입니다(`_bStartQueued` 일 때만 뜻이 있다). 로드가 끝난 프레임에 `update` 가 이 상태로 시작한다. */
        PlaySessionState       _queuedState{ PlaySessionState::Stopped };
        uint8                  _bHasSnapshot      : 1;
        uint8                  _bStartQueued      : 1; ///< 씬을 여는 중이라 시작을 미뤘다(언리얼 RequestPlaySession 의 대기 요청)
        uint8                  _bSimulate         : 1; ///< 월드만 돈다 — 게임 모듈 업데이트 · 게임 입력 없음, 에디터 카메라 유지(언리얼 Simulate)
        uint8                  _bStartAtPosition  : 1; ///< 다음 Play 를 `_startPosition` 에서 시작한다
        uint8                  _bStartMovePending : 1; ///< 시작 위치로 한 번 더 옮길 차례다(첫 프레임이 스폰 자리로 되돌린 것을 덮는다)
        [[maybe_unused]] uint8 _reserved          : 3;

        PlaySessionData()
            : _listSnapshot{}
            , _sceneGeneration{ 0 }
            , _sceneName{}
            , _sceneSourcePath{}
            , _startPosition{}
            , _pendingStepCount{ 0 }
            , _state{ PlaySessionState::Stopped }
            , _queuedState{ PlaySessionState::Stopped }
            , _bHasSnapshot{ SW_FALSE }
            , _bStartQueued{ SW_FALSE }
            , _bSimulate{ SW_FALSE }
            , _bStartAtPosition{ SW_FALSE }
            , _bStartMovePending{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorPlaySession
     * @brief 에디터 Play-In-Editor(PIE) 시뮬레이션의 수명 주기와 씬 롤백을 관리합니다.
     * @details 상태는 `EditorContext` 가 들고 있고(`PlaySessionData`), 이 클래스는 그것을 조작하는 정적 파사드입니다.
     *          컨텍스트가 없으면 "정지" 로 답합니다.
     */
    class EditorPlaySession
    {
    public:
        /** @brief "카메라 위치에서 시작" 이 먼저 찾는 태그입니다. 이 태그를 단 오브젝트가 없으면 게임 카메라를 든 오브젝트를 옮깁니다. */
        static constexpr const utf8* kPlayerStartTag = "Player";
        /** @brief 한 번에 진행할 수 있는 Step 프레임 수의 상한입니다. */
        static constexpr uint32 kMaxStepFrameCount = 10000;

        /** @brief 현재 플레이 세션 상태를 반환합니다. */
        static PlaySessionState getState();
        /** @brief 플레이 중인지 여부를 반환합니다. Step 대기 중이면 true입니다. */
        static bool isPlaying();
        /** @brief 일시정지 상태인지 여부를 반환합니다. */
        static bool isPaused();
        /** @brief 정지(편집 모드) 상태인지 여부를 반환합니다. */
        static bool isStopped();
        /** @brief 이번 프레임에 Step이 예약되어 있는지 반환합니다. */
        static bool hasPendingStep();
        /** @brief Simulate(월드만 도는 세션)이면 true 입니다. 멈춤이면 false 입니다. */
        static bool isSimulating();
        /**
         * @brief 플레이어가 조종하는 세션이 도는 중이면 true 입니다 — 게임 모듈 업데이트 · 게임 입력 · 게임 카메라가 켜진다.
         * @details 플레이 중(또는 Step 대기)이고 Simulate 가 아닐 때입니다. Simulate 는 씬만 틱하고 이것은 false 입니다.
         */
        static bool isPlayerActive();
        /** @brief `isPlayerActive` 의 본체입니다(상태를 인자로 받는다). */
        static bool isPlayerActive( const PlaySessionData& data );
        /** @brief 씬을 여는 중이라 시작이 미뤄져 있으면 true 입니다(로드가 끝나면 `update` 가 시작한다). */
        static bool isPlayQueued();

        /** @brief 플레이 세션 상태를 변경합니다 (스냅샷 캡처 및 롤백 복구 처리). */
        static void setState( PlaySessionState state );
        /**
         * @brief `setState` 의 본체입니다. 상태를 인자로 받아 에디터 컨텍스트 없이도 시험할 수 있습니다.
         * @details 정지에서 시작할 때 씬을 여는 중이면(`SceneManager::isTransitioning`) **시작을 미룹니다** — 언리얼 `RequestPlaySession` 이 요청을
         *          걸어 두고 다음 틱에 시작하듯, 로드가 끝난 프레임에 `update` 가 시작합니다. 지금 시작하면 스냅샷이 곧 내려갈 씬을 찍고, 플레이 중에
         *          로드가 끝나 씬이 바뀝니다. Stop 은 미룬 시작도 거둡니다.
         */
        static void setState( PlaySessionData& data, PlaySessionState state );
        /** @brief 매 에디터 프레임에 부릅니다. 미룬 시작이 있고 씬 로드가 끝났으면 시작합니다. */
        static void update();
        /** @brief `update` 의 본체입니다(상태를 인자로 받는다). */
        static void update( PlaySessionData& data );
        /** @brief 플레이어가 조종하는 플레이를 시작합니다(Simulate 중이면 플레이어 모드로 바꿉니다). */
        static void play();
        /** @brief 월드만 도는 Simulate 를 시작합니다(플레이 중이면 Simulate 로 바꿉니다). */
        static void simulate();
        /** @brief `play` · `simulate` 의 본체입니다. @p bSimulate 가 세션 종류를 정하고 상태를 플레이로 둡니다. */
        static void startSession( PlaySessionData& data, bool bSimulate );
        /** @brief 시뮬레이션을 일시정지합니다. */
        static void pause() { setState( PlaySessionState::Paused ); }
        /** @brief 시뮬레이션을 정지하고 씬을 롤백 복구합니다. */
        static void stop() { setState( PlaySessionState::Stopped ); }
        /** @brief 한 프레임만 시뮬레이션한 뒤 일시정지합니다. */
        static void stepOnce() { stepFrames( 1 ); }
        /** @brief @p frameCount 프레임을 진행한 뒤 일시정지합니다(멈춤이면 플레이를 시작한다). */
        static void stepFrames( uint32 frameCount );
        /** @brief `stepFrames` 의 본체입니다. 0 은 무시하고 `kMaxStepFrameCount` 로 자릅니다. */
        static void stepFrames( PlaySessionData& data, uint32 frameCount );
        /** @brief 예약된 Step 한 프레임을 소비하고, 다 쓰면 일시정지로 되돌립니다. */
        static void consumePendingStep();
        /** @brief `consumePendingStep` 의 본체입니다. */
        static void consumePendingStep( PlaySessionData& data );

        /**
         * @brief 다음 Play 를 @p position 에서 시작하게 합니다("카메라 위치에서 시작"). Simulate 에는 쓰지 않습니다.
         * @details 시작할 때(월드 시작 직후)와 첫 프레임 뒤에 한 번 더 옮깁니다 — 첫 틱에 스폰 자리로 되돌리는 게임이 있다. 옮길 오브젝트는
         *          `findStartObject` 가 고릅니다.
         */
        static void setStartPosition( PlaySessionData& data, const float3& position );
        /** @brief "카메라 위치에서 시작" 을 끕니다. */
        static void clearStartPosition( PlaySessionData& data );
        /** @brief 지금 세션의 상태입니다. 컨텍스트가 없으면 nullptr 입니다(패널이 시작 위치를 정할 때 씁니다). */
        static PlaySessionData* findData();
        /**
         * @brief 시작 위치로 옮길 오브젝트를 고릅니다. `kPlayerStartTag` 태그를 단 오브젝트, 없으면 게임 카메라를 든 오브젝트의 맨 위 조상입니다.
         * @return 없으면 nullptr
         */
        static GameObject* findStartObject( GameObjectManager& manager, CameraComponent* pGameCamera );

        /**
         * @brief 활성 씬을 스냅샷으로 찍습니다(Play 시작). 활성 씬의 세대 · 이름 · 소스 경로와 오브젝트의 프리팹 연결도 함께 적습니다.
         * @details `setState` 가 정지를 떠날 때 부릅니다. 상태(`PlaySessionData`)를 인자로 받아 에디터 컨텍스트 없이도 시험할 수 있습니다.
         */
        static void captureSnapshot( PlaySessionData& data );
        /**
         * @brief 스냅샷을 활성 씬에 되돌립니다(Stop).
         * @details 플레이 중에 활성 씬이 바뀌었으면 그 씬에 되돌리지 않고, 편집하던 씬을 빈 씬으로 다시 세운 뒤(이름 · 소스 경로) 되돌립니다.
         *          지금 씬에 그대로 되돌리면 두 씬이 섞이고, 저장하면 플레이 중에 연 씬 파일을 덮어씁니다.
         */
        static void restoreSnapshot( PlaySessionData& data );
    };
} // namespace sw::editor
