/**
 * @file EditorPlaySession.h
 * @brief 에디터 플레이(PIE) 세션과 씬 스냅샷 및 롤백을 관리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"

#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"

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
     * @brief 플레이 세션의 모든 상태를 담습니다. 소유자는 에디터 서브시스템 `EditorPlaySessionState` 입니다.
     * @details 에디터 범위가 보관하므로 컨텍스트와 함께 생기고 사라진다. 파일 정적 변수로 두면 수명을 소유한 객체가 없어,
     *          컨텍스트를 다시 만들어도 이전 세션의 스냅샷과 재생 상태가 그대로 남는다.
     */
    struct PlaySessionData
    {
        /**
         * @brief 롤백용 오브젝트 스냅샷 하나입니다.
         * @details 런타임 id(`_identity`)도 함께 저장한다. 정지할 때 상태를 되돌리면서 컴포넌트를 다시 만드는데, 원래 id 를
         *          복원해야 플레이 전에 보관하던 핸들(선택, 씬의 활성 카메라)이 그대로 이어진다.
         */
        struct ObjectSnapshot
        {
            ObjectIdentity _identity;
            string         _name;
            vector<uint8>  _bytes;
            string         _prefabPath; ///< 씬이 이 오브젝트에 연결한 프리팹 경로(`Scene::getEntityPrefabPath`). 씬을 다시 만들 때 복원한다
            /**
             * @brief 중첩 프리팹 부품 표시(`PrefabInstanceRegistry`)입니다. `_bNestedPart` 가 false 면 부품이 아닙니다.
             * @details 레지스트리는 놓인 오브젝트의 표시를 거두므로, 플레이 중에 지워진 부품을 원래 id 로 되살릴 때 이 값으로 다시 기록한다.
             */
            PrefabNestedPart _nestedPart;
            bool             _bNestedPart{ false };
            /**
             * @brief 프로퍼티 밖에 있는 저장하지 않은 지형 편집(`TerrainComponent::writeState`)입니다. 없으면 비어 있습니다.
             * @details 되돌리기는 컴포넌트를 다시 만들고 지형이 디스크 에셋을 다시 읽으므로, 플레이 전에 칠한 것은 이것으로만 돌아온다.
             */
            vector<uint8> _listTerrainEdit;
        };

        vector<ObjectSnapshot> _listSnapshot;
        /** @brief Keep Simulation Changes 로 저장한 오브젝트의 플레이 중 상태입니다. Stop 이 씬을 되돌린 뒤 편집 씬에 적용합니다(`keepObjectState`). */
        vector<ObjectSnapshot> _listKept;
        /**
         * @brief 스냅샷을 찍을 때의 활성 씬입니다. 씬 세대(`SceneManager::getSceneGeneration`), 이름, 소스 경로를 저장합니다.
         * @details Stop 할 때 활성 씬의 세대가 다르면 플레이 중에 씬이 바뀐 것이다. 게임 코드가 다음 레벨을 열었을 때가 그렇다.
         *          이때는 편집하던 씬을 저장해 둔 이름과 소스 경로로 다시 만든 뒤 되돌린다(`EditorPlaySession::restoreSnapshot`).
         */
        uint64 _sceneGeneration;
        string _sceneName;
        string _sceneSourcePath;
        /** @brief "카메라 위치에서 시작" 의 위치입니다. `_bStartAtPosition` 일 때만 의미가 있습니다. */
        float3 _startPosition;
        /** @brief 아직 진행할 Step 프레임 수입니다. 0 이 되면 일시정지로 돌아갑니다(`consumePendingStep`). */
        uint32           _pendingStepCount;
        PlaySessionState _state{ PlaySessionState::Stopped };
        /** @brief 씬을 여는 중에 요청한 시작 상태입니다. `_bStartQueued` 일 때만 의미가 있으며, 로드가 끝난 프레임에 `update` 가 이 상태로 시작합니다. */
        PlaySessionState _queuedState{ PlaySessionState::Stopped };
        /** @brief 씬 편집을 막고 있는 작업 수입니다(`EditorPlaySession::holdSceneEdits` — 굽는 동안 물리 셰이프가 바뀌지 않게). */
        uint32 _sceneEditHoldCount;
        /** @brief 막는 동안 씬이 바뀐 횟수입니다(`EditorPlaySession::noteSceneEditWhileHeld`). */
        uint32 _heldSceneEditCount;
        uint8  _bHasSnapshot      : 1;
        uint8  _bStartQueued      : 1; ///< 씬을 여는 중이라 시작을 미뤘다(언리얼의 RequestPlaySession 대기 요청에 해당)
        uint8  _bSimulate         : 1; ///< 월드만 실행한다. 게임 모듈 업데이트와 게임 입력이 없고 에디터 카메라를 유지한다(언리얼의 Simulate)
        uint8  _bStartAtPosition  : 1; ///< 다음 Play 를 `_startPosition` 에서 시작한다
        uint8  _bStartMovePending : 1; ///< 시작 위치로 한 번 더 옮긴다. 첫 프레임이 스폰 위치로 되돌린 것을 덮어쓴다
        uint8  _bOptionsApplied   : 1; ///< 이 세션의 플레이 옵션(최대화)을 적용했다(`EditorPlayCommands::update`)
        uint8  _bMaximizedForPlay : 1; ///< Maximize On Play 가 게임 뷰를 최대화했다. 멈추면 배치를 되돌린다
        uint8  _bEjected          : 1; ///< F8 로 조종을 놓았다. 다시 누르면 조종으로 돌아간다

        PlaySessionData()
            : _listSnapshot{}
            , _listKept{}
            , _sceneGeneration{ 0 }
            , _sceneName{}
            , _sceneSourcePath{}
            , _startPosition{}
            , _pendingStepCount{ 0 }
            , _state{ PlaySessionState::Stopped }
            , _queuedState{ PlaySessionState::Stopped }
            , _sceneEditHoldCount{ 0 }
            , _heldSceneEditCount{ 0 }
            , _bHasSnapshot{ SW_FALSE }
            , _bStartQueued{ SW_FALSE }
            , _bSimulate{ SW_FALSE }
            , _bStartAtPosition{ SW_FALSE }
            , _bStartMovePending{ SW_FALSE }
            , _bOptionsApplied{ SW_FALSE }
            , _bMaximizedForPlay{ SW_FALSE }
            , _bEjected{ SW_FALSE }
        {
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorPlaySessionState
     * @brief 플레이 세션 상태(`PlaySessionData`)를 드는 에디터 서브시스템입니다. 조작은 정적 파사드 `EditorPlaySession` 이 합니다.
     */
    class EditorPlaySessionState final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorPlaySessionState";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        PlaySessionData& getData() { return _data; }

    private:
        PlaySessionData _data{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorPlaySession
     * @brief 에디터 Play-In-Editor(PIE) 시뮬레이션의 수명 주기와 씬 롤백을 관리합니다.
     * @details 상태는 에디터 서브시스템 `EditorPlaySessionState` 가 보관하고(`PlaySessionData`), 이 클래스는 그 상태를 조작하는 정적 파사드이다.
     *          컨텍스트가 없으면 "정지" 로 응답한다.
     */
    class EditorPlaySession
    {
    public:
        /** @brief "카메라 위치에서 시작" 이 먼저 찾는 태그입니다. 이 태그를 가진 오브젝트가 없으면 게임 카메라를 가진 오브젝트를 옮깁니다. */
        static constexpr const utf8* kPlayerStartTag = "Player";
        /** @brief 한 번에 진행할 수 있는 Step 프레임 수의 상한입니다. */
        static constexpr uint32 kMaxStepFrameCount = 10000;

        /** @brief 현재 플레이 세션 상태를 반환합니다. */
        static PlaySessionState getState();
        /** @brief 플레이 중이면 true 입니다. Step 대기 중일 때도 true 입니다. */
        static bool isPlaying();
        /** @brief 일시정지 상태이면 true 입니다. */
        static bool isPaused();
        /** @brief 정지(편집 모드) 상태이면 true 입니다. 확장 모듈(`GF_EditorPlugIn_*`)의 패널이 런타임 상태 유무를 가르는 데 쓰므로 내보낸다. */
        SW_EDITOR_API static bool isStopped();
        /** @brief 플레이 세션이 정지(Stop) 상태일 때만 씬 오브젝트를 편집할 수 있습니다(`EditorSessionPolicy`). */
        static bool areSceneEditsAllowed();
        /**
         * @brief 씬 편집을 막습니다(짝이 되는 `releaseSceneEdits` 까지). 씬을 읽으며 여러 프레임에 걸쳐 도는 작업(라이트 프로브 굽기)이 부른다.
         * @details 편집 경로는 모두 `areSceneEditsAllowed` 를 묻는다 — 막는 동안 그 답이 false 라 인스펙터 · 기즈모 · 붓이 잠긴다. 플레이 시작은 막지 않는다
         *          (작업이 정지 상태가 아니면 스스로 멈춘다).
         */
        static void holdSceneEdits();
        /** @brief `holdSceneEdits` 하나를 풉니다. */
        static void releaseSceneEdits();
        /** @brief 씬 편집을 막고 있는 작업이 있으면 true 입니다. */
        static bool areSceneEditsHeld();
        /** @brief 막는 동안 씬이 바뀌었음을 셉니다(`EditorWorkspace::markSceneDirty` 가 부른다 — 편집 허용 답을 묻지 않은 경로다). */
        static void noteSceneEditWhileHeld();
        /** @brief 막는 동안 바뀐 횟수입니다(세션 동안 늘기만 한다). 막는 작업은 시작할 때 값과 견줘 바뀌면 멈춘다. */
        static uint32 getHeldSceneEditCount();
        /** @brief 이번 프레임에 Step 이 예약되어 있으면 true 입니다. */
        static bool hasPendingStep();
        /** @brief Simulate(월드만 실행하는 세션)이면 true 입니다. 정지 상태이면 false 입니다. */
        static bool isSimulating();
        /**
         * @brief 플레이어가 조종하는 세션이 실행 중이면 true 입니다. 이때 게임 모듈 업데이트, 게임 입력, 게임 카메라가 켜집니다.
         * @details 플레이 중(또는 Step 대기)이고 Simulate 가 아닐 때 true 이다. Simulate 는 씬만 틱하므로 false 이다.
         */
        static bool isPlayerActive();
        /** @brief `isPlayerActive` 의 본체입니다. 상태를 인자로 받습니다. */
        static bool isPlayerActive( const PlaySessionData& data );
        /** @brief 씬을 여는 중이라 시작이 미뤄져 있으면 true 입니다. 로드가 끝나면 `update` 가 시작합니다. */
        static bool isPlayQueued();

        /** @brief 플레이 세션 상태를 바꿉니다. 이때 스냅샷 캡처와 롤백 복구도 처리합니다. */
        static void setState( PlaySessionState state );
        /**
         * @brief `setState` 의 본체입니다. 상태를 인자로 받아 에디터 컨텍스트 없이도 테스트할 수 있습니다.
         * @details 정지에서 시작할 때 씬을 여는 중이면(`SceneManager::isTransitioning`) 시작을 미룬다. 언리얼의 `RequestPlaySession` 이 요청을
         *          등록해 두고 다음 틱에 시작하는 것과 같이, 로드가 끝난 프레임에 `update` 가 시작한다. 지금 시작하면 스냅샷이 곧 언로드될 씬을 찍고,
         *          플레이 중에 로드가 끝나 씬이 바뀐다. Stop 은 미룬 시작도 취소한다.
         */
        static void setState( PlaySessionData& data, PlaySessionState state );
        /** @brief 매 에디터 프레임에 부릅니다. 미룬 시작이 있고 씬 로드가 끝났으면 시작합니다. */
        static void update();
        /** @brief `update` 의 본체입니다. 상태를 인자로 받습니다. */
        static void update( PlaySessionData& data );
        /** @brief 플레이어가 조종하는 플레이를 시작합니다. Simulate 중이면 플레이어 모드로 바꿉니다. */
        static void play();
        /** @brief 월드만 실행하는 Simulate 를 시작합니다. 플레이 중이면 Simulate 로 바꿉니다. */
        static void simulate();
        /** @brief `play` 와 `simulate` 의 본체입니다. @p bSimulate 가 세션 종류를 정하고 상태를 플레이로 바꿉니다. */
        static void startSession( PlaySessionData& data, bool bSimulate );
        /** @brief 시뮬레이션을 일시정지합니다. */
        static void pause() { setState( PlaySessionState::Paused ); }
        /** @brief 시뮬레이션을 정지하고 씬을 롤백합니다. */
        static void stop() { setState( PlaySessionState::Stopped ); }
        /** @brief 한 프레임만 시뮬레이션한 뒤 일시정지합니다. */
        static void stepOnce() { stepFrames( 1 ); }
        /** @brief @p frameCount 프레임을 진행한 뒤 일시정지합니다. 정지 상태이면 플레이를 시작합니다. */
        static void stepFrames( uint32 frameCount );
        /** @brief `stepFrames` 의 본체입니다. 0 은 무시하고 `kMaxStepFrameCount` 로 자릅니다. */
        static void stepFrames( PlaySessionData& data, uint32 frameCount );
        /** @brief 예약된 Step 한 프레임을 소비하고, 다 소비하면 일시정지로 되돌립니다. */
        static void consumePendingStep();
        /** @brief `consumePendingStep` 의 본체입니다. */
        static void consumePendingStep( PlaySessionData& data );

        /**
         * @brief 다음 Play 를 @p position 에서 시작하게 합니다("카메라 위치에서 시작"). Simulate 에는 쓰지 않습니다.
         * @details 시작할 때(월드 시작 직후)와 첫 프레임 뒤에 한 번 더 옮긴다. 첫 틱에 스폰 위치로 되돌리는 게임이 있기 때문이다.
         *          옮길 오브젝트는 `findStartObject` 가 고른다.
         */
        static void setStartPosition( PlaySessionData& data, const float3& position );
        /** @brief "카메라 위치에서 시작" 을 끕니다. */
        static void clearStartPosition( PlaySessionData& data );
        /** @brief 현재 세션의 상태를 반환합니다. 컨텍스트가 없으면 nullptr 입니다. 패널이 시작 위치를 정할 때 씁니다. */
        static PlaySessionData* findData();
        /**
         * @brief 시작 위치로 옮길 오브젝트를 고릅니다. `kPlayerStartTag` 태그를 가진 오브젝트를 먼저 찾고, 없으면 게임 카메라를 가진 오브젝트의 최상위 조상을 고릅니다.
         * @return 찾은 오브젝트입니다. 없으면 nullptr 입니다.
         */
        static GameObject* findStartObject( GameObjectManager& manager, CameraComponent* pGameCamera );

        /**
         * @brief Play 를 시작할 때 활성 씬을 스냅샷으로 찍습니다. 활성 씬의 세대, 이름, 소스 경로와 오브젝트의 프리팹 연결도 함께 저장합니다.
         * @details `setState` 가 정지 상태를 벗어날 때 부른다. 상태(`PlaySessionData`)를 인자로 받으므로 에디터 컨텍스트 없이도 테스트할 수 있다.
         */
        static void captureSnapshot( PlaySessionData& data );
        /**
         * @brief Stop 할 때 스냅샷을 활성 씬에 되돌립니다.
         * @details 플레이 중에 활성 씬이 바뀌었으면 그 씬에 되돌리지 않는다. 편집하던 씬을 빈 씬으로 다시 만들고(이름과 소스 경로 복원) 그 위에 되돌린다.
         *          현재 씬에 그대로 되돌리면 두 씬이 섞이고, 저장할 때 플레이 중에 연 씬 파일을 덮어쓴다.
         */
        static void restoreSnapshot( PlaySessionData& data );
        /**
         * @brief @p pObject 의 현재(플레이 중) 상태를 Stop 이후에도 남도록 저장합니다. 같은 오브젝트를 다시 저장하면 새 상태로 바꿉니다. 정지 상태이면 false 입니다.
         * @details Stop 은 스냅샷으로 씬을 되돌린 뒤 저장한 상태를 그 오브젝트(같은 id)에 적용하고, 되돌리기 한 번("Keep Simulation Changes")으로 기록한다.
         *          플레이 중에 생긴 오브젝트는 되돌리기가 지우므로 남지 않는다.
         */
        static bool keepObjectState( PlaySessionData& data, const GameObject* pObject );
    };
} // namespace sw::editor
