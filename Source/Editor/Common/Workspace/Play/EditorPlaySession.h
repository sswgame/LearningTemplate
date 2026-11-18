/**
 * @file EditorPlaySession.h
 * @brief 에디터 플레이(PIE) 세션과 씬 스냅샷 및 롤백을 관리합니다.
 */
#pragma once
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/ThreadSafety.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"

#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    class CameraComponent;
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    class ImGuiEditor;
    struct EditorUICommand;
    struct EditorPlayConsoleCommands;
    struct EditorAutomationPlayReset;

    /**
     * @class EditorPlaySessionWriteKey
     * @brief 플레이 세션을 바꾸는 함수(`EditorPlaySession::play` · `setState` · `findData` …)의 게임 스레드 전용 열쇠입니다(passkey).
     * @details 만들 수 있는 쪽은 게임 스레드의 입구뿐이다 — UI 명령 적용(`EditorUICommand::apply`), 호스트 훅(`ImGuiEditor` 의 `stopSimulation` · `onHostFrameEnd`),
     *          콘솔 명령(`EditorPlayConsoleCommands`), 자동화 단계 초기화(`EditorAutomationPlayReset`). ViewModel · 패널은 열쇠를 만들 수 없어 세션을 바로 바꾸지 못하고
     *          명령(`EditorUICommandQueue`)을 보낸다 — 에디터 UI 스레드 분리의 출력 계약을 형식이 막는다(언리얼 Slate 가 게임 상태를 명령으로만 바꾸는 자리).
     *          열쇠를 받은 함수는 그것을 넘겨 다른 쓰기 함수를 부를 수 있다(값 복사).
     */
    class EditorPlaySessionWriteKey final
    {
    private:
        // explicit 이 집합체 초기화(`EditorPlaySessionWriteKey{}` 가 비공개 생성자를 건너뛰는 C++17 구멍)를 막는다.
        explicit EditorPlaySessionWriteKey() = default;

        friend class ImGuiEditor;
        friend struct EditorUICommand;
        friend struct EditorPlayConsoleCommands;
        friend struct EditorAutomationPlayReset;
    };

    enum class PlaySessionState : uint8
    {
        Stopped = 0,
        Playing,
        Paused
    };

    /**
     * @struct PlaySessionData
     * @brief 플레이 세션의 모든 상태를 담습니다. 소유자는 에디터 서브시스템 `EditorPlaySession` 입니다.
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
             * @details 레지스트리는 놓인 오브젝트의 표시를 제거하므로, 플레이 중에 지워진 부품을 원래 id 로 복원할 때 이 값으로 다시 기록한다.
             */
            PrefabNestedPart _nestedPart;
            bool             _bNestedPart{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            /**
             * @brief 프로퍼티 밖에 있는 저장하지 않은 지형 편집(`TerrainComponent::writeState`)입니다. 없으면 비어 있습니다.
             * @details 되돌리기는 컴포넌트를 다시 만들고 지형이 디스크 에셋을 다시 읽으므로, 플레이 전에 칠한 것은 이것으로만 돌아온다.
             */
            vector<uint8> _listTerrainEdit;
        };

        vector<ObjectSnapshot> _listSnapshot;
        /** @brief 오브젝트 상태에 없는 씬 조립 기록(인스턴스 · 데이터 레이어 · 상속)입니다. 씬을 다시 만들어도 저장에서 풀리지 않게 함께 되돌립니다. */
        Scene::AssemblySnapshot _sceneAssembly;
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
        /** @brief 씬 편집을 막고 있는 작업 수입니다(`EditorPlaySession::holdSceneEdits` — 베이크하는 동안 물리 셰이프가 바뀌지 않게). */
        uint32 _sceneEditHoldCount;
        /** @brief 막는 동안 씬이 바뀐 횟수입니다(`EditorPlaySession::recordSceneEditWhileHeld`). */
        uint32 _heldSceneEditCount;
        /**
         * @brief 마지막으로 거절 로그를 남긴 편집 이름입니다(`EditorPlaySession::refuseSceneEdit`). 편집이 다시 허용되면 비웁니다.
         * @details 기즈모 드래그처럼 매 프레임 같은 편집을 부르는 경로가 플레이 중에 줄마다 경고를 쏟지 않게, 같은 편집과 같은 이유의 연속 거절은 한 번만 남긴다.
         */
        string _lastRefusedAction;
        uint8  _bLastRefusedByHold : 1; ///< `_lastRefusedAction` 을 거절한 이유가 작업의 편집 막기(`holdSceneEdits`)였다
        uint8  _bHasSnapshot       : 1;
        uint8  _bStartQueued       : 1; ///< 씬을 여는 중이라 시작을 미뤘다(언리얼의 RequestPlaySession 대기 요청에 해당)
        uint8  _bSimulate          : 1; ///< 월드만 실행한다. 게임 모듈 업데이트와 게임 입력이 없고 에디터 카메라를 유지한다(언리얼의 Simulate)
        uint8  _bStartAtPosition   : 1; ///< 다음 Play 를 `_startPosition` 에서 시작한다
        uint8  _bStartMovePending  : 1; ///< 시작 위치로 한 번 더 옮긴다. 첫 프레임이 스폰 위치로 되돌린 것을 덮어쓴다
        uint8  _bEjected           : 1; ///< F8 로 조종을 놓았다. 다시 누르면 조종으로 돌아간다

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
            , _lastRefusedAction{}
            , _bLastRefusedByHold{ SW_FALSE }
            , _bHasSnapshot{ SW_FALSE }
            , _bStartQueued{ SW_FALSE }
            , _bSimulate{ SW_FALSE }
            , _bStartAtPosition{ SW_FALSE }
            , _bStartMovePending{ SW_FALSE }
            , _bEjected{ SW_FALSE }
        {
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct EditorPlayStateSnapshot
     * @brief UI 가 읽는 플레이 세션 상태 값입니다 — 게임 스레드가 세션을 바꾼 뒤 넘기고(`EditorPlaySession::publishState`), UI 프레임 시작이 받는다(`beginUIFrame`).
     * @details 게임 → UI 방향의 스냅샷이다. ViewModel · 툴바 · 창 제목 · 키보드 주인은 살아 있는 세션(`PlaySessionData`)이 아니라 이 값을 읽는다(`EditorPlaySession::getUIState`).
     *          판정 규칙은 세션의 정적 함수와 같다(같은 이름의 함수).
     */
    struct EditorPlayStateSnapshot
    {
        PlaySessionState _state{ PlaySessionState::Stopped };
        uint8            _bSimulate{ SW_FALSE };
        uint8            _bPendingStep{ SW_FALSE };
        uint8            _bEjected{ SW_FALSE };
        uint8            _bPlayQueued{ SW_FALSE };
        uint8            _bSceneEditsHeld{ SW_FALSE };

        bool isStopped() const { return _state == PlaySessionState::Stopped; }
        bool isPaused() const { return _state == PlaySessionState::Paused; }
        bool isPlaying() const { return _bPendingStep == SW_TRUE || _state == PlaySessionState::Playing; }
        bool isSimulating() const { return isStopped() == false && _bSimulate == SW_TRUE; }
        bool isPlayerActive() const { return _bSimulate == SW_FALSE && isPlaying(); }
        bool isEjected() const { return isStopped() == false && _bEjected == SW_TRUE; }
        bool isPlayQueued() const { return _bPlayQueued == SW_TRUE; }
        /** @brief 정지이고 편집을 막는 작업이 없으면 true 입니다(`EditorSessionPolicy::areSceneEditsAllowed` 와 같은 규칙). */
        SW_EDITOR_API bool areSceneEditsAllowed() const;
    };

    /**
     * @class EditorSceneEditHold
     * @brief 씬 편집 막기 하나를 쥐는 객체입니다(`EditorPlaySession::holdSceneEdits` 가 준다). 놓거나(`release`) 사라지면 막기를 풉니다(RAII).
     * @details 예전에는 막기와 풀기가 짝 맞춘 두 함수라 아무 데서나(UI 프레임에서도) 부를 수 있었고 풀기를 빠뜨릴 수 있었다. 이제 막기는 게임 스레드 열쇠를 받아서만 생기고,
     *          풀기는 이 객체만 한다 — 짝이 형식으로 맞는다(언리얼 `FScopedTransaction` 처럼 쥔 동안만 효력).
     */
    class EditorSceneEditHold final
    {
    public:
        EditorSceneEditHold() = default;
        ~EditorSceneEditHold() { release(); }
        EditorSceneEditHold( const EditorSceneEditHold& )            = delete;
        EditorSceneEditHold& operator=( const EditorSceneEditHold& ) = delete;
        EditorSceneEditHold( EditorSceneEditHold&& other ) noexcept
            : _bHeld{ other._bHeld }
        {
            other._bHeld = SW_FALSE;
        }
        EditorSceneEditHold& operator=( EditorSceneEditHold&& other ) noexcept
        {
            if ( this != &other )
            {
                release();
                _bHeld       = other._bHeld;
                other._bHeld = SW_FALSE;
            }
            return *this;
        }

        /** @brief 쥐고 있으면 막기를 풉니다. 이미 놓았으면 아무것도 하지 않습니다. */
        void release();
        bool isHeld() const { return _bHeld == SW_TRUE; }

    private:
        friend class EditorPlaySession;
        explicit EditorSceneEditHold( bool bHeld )
            : _bHeld{ bHeld ? SW_TRUE : SW_FALSE }
        {
        }

    private:
        uint8 _bHeld{ SW_FALSE };
    };

    /**
     * @class EditorPlaySession
     * @brief 에디터 Play-In-Editor(PIE) 시뮬레이션의 수명 주기와 씬 롤백을 관리합니다.
     * @details 에디터 서브시스템으로 상태(`PlaySessionData`)를 들고, 정적 함수가 에디터 범위에서 이 서브시스템을 찾아 조작한다(언리얼 `FSlateApplication::Get` 처럼
     *          하나뿐인 인스턴스에 정적으로 닿는다). 에디터 범위가 없으면 "정지" 로 응답한다. `PlaySessionData&` 를 받는 본체는 컨텍스트 없이 테스트한다.
     *          세션을 바꾸는 정적 함수와 상태를 고칠 수 있게 꺼내는 `findData` 는 게임 스레드 열쇠(`EditorPlaySessionWriteKey`)를 받는다 — 읽기는 누구나 한다.
     */
    class EditorPlaySession final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorPlaySession";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /** @brief "카메라 위치에서 시작" 이 먼저 찾는 태그입니다. 이 태그를 가진 오브젝트가 없으면 게임 카메라를 가진 오브젝트를 옮깁니다. */
        static constexpr const utf8* kPlayerStartTag = "Player";
        /** @brief 한 번에 진행할 수 있는 Step 프레임 수의 상한입니다. */
        static constexpr uint32 kMaxStepFrameCount = 10000;

        /**
         * @brief UI 쪽 — 이번 UI 프레임의 플레이 상태 값입니다(프레임 시작에 받은 사본). 에디터 범위가 없으면 "정지" 입니다. 확장 모듈의 ViewModel 이 읽으므로 내보낸다.
         * @details UI(ViewModel · 툴바 · 창 제목)는 이것을 읽는다 — 아래 정적 질의는 게임 스레드(명령 적용 · 게임 단계 · 감시 값)와 명령의 켜짐 판정이 쓴다.
         */
        SW_EDITOR_API static EditorPlayStateSnapshot getUIState();
        /** @brief 게임 스레드 쪽 — 지금 세션 상태를 UI 쪽으로 넘깁니다(셸이 UI 명령 적용 뒤와 호스트 프레임 끝에 부른다). */
        static void publishState( EditorPlaySessionWriteKey key );
        /** @brief UI 쪽 — UI 프레임 시작에 게임 스레드가 마지막으로 넘긴 상태를 이번 UI 프레임 값으로 받습니다. */
        static void beginUIFrame();

        /** @brief 현재 플레이 세션 상태를 반환합니다. */
        static PlaySessionState getState();
        /** @brief 플레이 중이면 true 입니다. Step 대기 중일 때도 true 입니다. */
        static bool isPlaying();
        /** @brief 일시정지 상태이면 true 입니다. */
        static bool isPaused();
        /** @brief 정지(편집 모드) 상태이면 true 입니다. 확장 모듈(`GF_EditorPlugin_*`)의 패널이 런타임 상태 유무를 가르는 데 쓰므로 내보낸다. */
        SW_EDITOR_API static bool isStopped();
        /** @brief 플레이 세션이 정지(Stop) 상태일 때만 씬 오브젝트를 편집할 수 있습니다(`EditorSessionPolicy`). 확장 모듈의 인스펙터(`InspectorObjectEdit` 인라인)가 부르므로 내보낸다. */
        SW_EDITOR_API static bool areSceneEditsAllowed();
        /**
         * @brief 씬 편집이 막혀 있으면 거절 이유를 경고 로그(`Editor.SceneEditRefused`)로 남기고 true 를 반환합니다. 편집 명령이 맨 앞에서 묻습니다.
         * @details 명령이 조용히 반환하면 사용자도 호출자도 왜 아무 일도 없었는지 알 수 없다 — 거절 이유를 한 자리에서 남긴다.
         *          이벤트마다 한 번 불리는 명령(버튼 · 드롭)용이다. 프레임마다 묻는 UI 잠금은 `areSceneEditsAllowed` 를 쓴다.
         * @param pAction 거절된 동작의 이름(로그에 들어간다)
         */
        [[nodiscard]] static bool refuseSceneEdit( const utf8* pAction );
        /**
         * @brief 씬 편집을 막고 그 막기를 쥔 객체를 돌려줍니다(그 객체가 놓이거나 사라질 때까지). 씬을 읽으며 여러 프레임에 걸쳐 도는 게임 스레드 작업(베이크)이 부른다.
         * @details 편집 경로는 모두 `areSceneEditsAllowed` 를 묻는다 — 막는 동안 그 답이 false 라 인스펙터 · 기즈모 · 붓이 잠긴다. 플레이 시작은 막지 않는다
         *          (작업이 정지 상태가 아니면 스스로 멈춘다). 에디터 범위가 없으면 아무것도 쥐지 않은 객체다.
         */
        [[nodiscard]] static EditorSceneEditHold holdSceneEdits( EditorPlaySessionWriteKey key );
        /** @brief 씬 편집을 막고 있는 작업이 있으면 true 입니다. */
        static bool areSceneEditsHeld();
        /** @brief 막는 동안 씬이 바뀌었음을 셉니다(`EditorWorkspace::markSceneDirty` 가 부른다 — 편집 허용 답을 묻지 않은 경로다). */
        static void recordSceneEditWhileHeld();
        /** @brief 막는 동안 바뀐 횟수입니다(세션 동안 늘기만 한다). 막는 작업은 시작할 때 값과 비교해 바뀌면 멈춘다. */
        static uint32 getHeldSceneEditCount();
        /** @brief 이번 프레임에 Step 이 예약되어 있으면 true 입니다. */
        static bool hasPendingStep();
        /** @brief F8 로 조종을 놓은(Eject) 세션이면 true 입니다. 정지하면 false 로 돌아간다. UI 의 플레이 배치(`EditorPlayCommands::update`)가 읽는다. */
        static bool isEjected();
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
        static void setState( EditorPlaySessionWriteKey key, PlaySessionState state );
        /**
         * @brief `setState` 의 본체입니다. 상태를 인자로 받아 에디터 컨텍스트 없이도 테스트할 수 있습니다.
         * @details 정지에서 시작할 때 씬을 여는 중이면(`SceneManager::isTransitioning`) 시작을 미룬다. 언리얼의 `RequestPlaySession` 이 요청을
         *          등록해 두고 다음 틱에 시작하는 것과 같이, 로드가 끝난 프레임에 `update` 가 시작한다. 지금 시작하면 스냅샷이 곧 언로드될 씬을 찍고,
         *          플레이 중에 로드가 끝나 씬이 바뀐다. Stop 은 미룬 시작도 취소한다.
         */
        static void setState( PlaySessionData& data, PlaySessionState state );
        /** @brief 매 에디터 프레임에 부릅니다. 미룬 시작이 있고 씬 로드가 끝났으면 시작합니다. */
        static void update( EditorPlaySessionWriteKey key );
        /** @brief `update` 의 본체입니다. 상태를 인자로 받습니다. */
        static void update( PlaySessionData& data );
        /** @brief 플레이어가 조종하는 플레이를 시작합니다. Simulate 중이면 플레이어 모드로 바꿉니다. */
        static void play( EditorPlaySessionWriteKey key );
        /** @brief 월드만 실행하는 Simulate 를 시작합니다. 플레이 중이면 Simulate 로 바꿉니다. */
        static void simulate( EditorPlaySessionWriteKey key );
        /** @brief `play` 와 `simulate` 의 본체입니다. @p bSimulate 가 세션 종류를 정하고 상태를 플레이로 바꿉니다. */
        static void startSession( PlaySessionData& data, bool bSimulate );
        /** @brief 시뮬레이션을 일시정지합니다. */
        static void pause( EditorPlaySessionWriteKey key ) { setState( key, PlaySessionState::Paused ); }
        /** @brief 시뮬레이션을 정지하고 씬을 롤백합니다. */
        static void stop( EditorPlaySessionWriteKey key ) { setState( key, PlaySessionState::Stopped ); }
        /** @brief @p frameCount 프레임을 진행한 뒤 일시정지합니다. 정지 상태이면 플레이를 시작합니다. */
        static void stepFrames( EditorPlaySessionWriteKey key, uint32 frameCount );
        /** @brief `stepFrames` 의 본체입니다. 0 은 무시하고 `kMaxStepFrameCount` 로 자릅니다. */
        static void stepFrames( PlaySessionData& data, uint32 frameCount );
        /** @brief 예약된 Step 한 프레임을 소비하고, 다 소비하면 일시정지로 되돌립니다. */
        static void consumePendingStep( EditorPlaySessionWriteKey key );
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
        /** @brief 현재 세션의 상태를 고칠 수 있게 반환합니다(게임 스레드 — 시작 위치 · Eject · 남길 변경 · 플레이 옵션). 컨텍스트가 없으면 nullptr 입니다. */
        static PlaySessionData* findData( EditorPlaySessionWriteKey key );
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

    private:
        /** @brief 읽기 · 세션 안의 기록(거절 로그 · 편집 막기)용 조회입니다. 밖으로는 열쇠를 받는 `findData` 만 상태를 고칠 수 있게 내준다. */
        static PlaySessionData* findDataInternal();
        /** @brief 막기 하나를 풉니다 — `EditorSceneEditHold` 만 부른다. */
        static void releaseSceneEditHold();
        friend class EditorSceneEditHold;

        PlaySessionData                         _data{};
        mutable mutex                           _stateMutex{}; ///< 넘긴 상태를 지킨다 — 게임 스레드가 넘기고 UI 스레드가 받는다
        EditorPlayStateSnapshot _publishedState SW_GUARDED_BY( _stateMutex ){};
        EditorPlayStateSnapshot                 _uiState{}; ///< 이번 UI 프레임 값(UI 스레드만)
    };
} // namespace sw::editor
