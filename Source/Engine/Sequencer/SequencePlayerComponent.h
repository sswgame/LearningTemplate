/**
 * @file SequencePlayerComponent.h
 * @brief SequenceAsset 을 재생하고, 씬의 바인딩 표로 묶은 대상 오브젝트에 클립 · 키 · 구간 동작 · 이벤트를 적용합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Sequencer/SequenceDiagnostics.h"
#include "Engine/Sequencer/SequencePlayer.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

namespace sw
{
    class InputMap;
    class SequenceAsset;

    /**
     * @brief 컷씬 동안 플레이어 움직임 정책입니다(언리얼 `FMovieSceneSequencePlaybackSettings` 의 `bDisableMovementInput` · `bDisableLookAtInput` 와 "플레이 가능한 컷씬").
     * @details 막기는 입력 맵 문맥으로 가른다 — 재생 동안 엔진 컷씬 레이어(`Cutscene`)를 플레이어 입력 맵 위에 올리고, `Blocked` 면 아래 레이어(이동 · 시선 · 공격)를 막는다.
     *          컷씬 레이어의 동작(건너뛰기 · 빨리감기)과 UI 맵(일시정지 · 메뉴 조작)은 막히지 않는다.
     */
    ENUM()
    enum class SequencePlayerMovement : int32
    {
        Free     = 0, /**< 막지 않는다. */
        Blocked  = 1, /**< 이동 · 시선 · 게임 동작 입력을 막는다(컷씬 레이어가 아래 레이어를 막는다). */
        Confined = 2, /**< 움직일 수 있되 `_confineTarget` 을 `_confineCenter` 둘레 `_confineRadius` 안에 둔다(넘으면 경계로 되돌린다). */
    };
    /** @brief 이벤트 항목 하나를 지났을 때 부르는 델리게이트입니다. 항목은 알림 동안만 유효한 사본이고 대상은 그 바인딩의 오브젝트입니다. */
    SW_DECLARE_DELEGATE( void, OnSequenceEventDelegate, const SequenceFiredEvent& event );
    /** @brief 카메라 신호(컷 바뀜, 시네마틱 시작 · 끝, 대기 뷰, 흔들림) 하나를 냈을 때 부르는 델리게이트입니다. */
    SW_DECLARE_DELEGATE( void, OnSequenceCameraSignalDelegate, const SequenceCameraSignal& signal );
    /** @brief 재생이 정상으로 끝났을 때(끝 프레임 · `skipToEnd`) 부르는 델리게이트입니다(언리얼 `OnFinished`). `stop` 은 부르지 않는다. */
    using OnSequenceFinishedDelegate = Delegate<void()>;

    /**
     * @class SequencePlayerComponent
     * @brief 시퀀스 하나를 재생합니다. 시퀀스의 바인딩 id 마다 이 씬의 오브젝트를 `_listBinding` 으로 묶는다(유니티 PlayableDirector 의 바인딩, 언리얼 Level Sequence Actor 의 Binding Overrides).
     * @details 재생 계약: 시작할 때 재생에 필요한 바인딩이 모두 묶였는지 보고, 하나라도 비면 오류를 남기고 틀지 않는다(이름 찾기 폴백 없음).
     *          끝(반복이 아닐 때 진행 방향의 끝 프레임) · 끝으로 건너뛰기(`skipToEnd`) · `stop` · 씬 나감(`onEndPlay`) · 재생 중 다시 틀기(먼저 멈춘다) · 시퀀스 바꾸기는 모두 같은 끝내기(`endPlayback`)를 지난다 —
     *          덮던 컷이 있으면 시네마틱 끝을 알리고, 구간 동작(소리 · 애니메이션 덮어쓰기)을 나가고, 스폰을 지우고, 컷을 풀고, 시퀀스의 `_whenFinished` 가
     *          `RestoreState` 면 시작 전 상태로 되돌린다. 일시정지는 끝내기가 아니다.
     */
    REFLECT( Category = "Cinematics", DisplayName = "Sequence Player Component", Tooltip = "Plays a SequenceAsset timeline against the scene objects bound to its binding ids", Meta = "Icon=Sequence" )
    class SW_API SequencePlayerComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;
        SequencePlayerComponent();
        virtual ~SequencePlayerComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;
        /** @brief 읽어 들인 `_sequencePath` 로 시퀀스를 다시 엽니다. 플레이 전(에디터)에도 Play 가 그 시퀀스를 돌립니다. */
        void onPostLoad() override;
        /** @brief `_sequencePath` 가 바뀌면 그 시퀀스를 다시 엽니다. */
        void onPropertyChanged( hashed_string propertyName ) override;

        FUNCTION( Category = "Playback", DisplayName = "Play", CallInEditor )
        void play();
        /**
         * @brief @p frame(재생 범위 안으로 자른다)으로 점프해 틉니다 — 컷씬 옵션 · 되돌릴 상태는 `play` 와 같다. 그 앞 이벤트는 쏘지 않는다(점프).
         * @details 늦게 들어온 클라이언트가 서버의 지금 프레임부터 잇는다(언리얼 `ULevelSequencePlayer` 복제의 `PlayFrom`/`SetPlaybackPosition` 자리).
         */
        void playFromFrame( int32 frame );
        /**
         * @brief 재생을 @p seconds 만큼 바로 진행합니다 — 지나는 이벤트를 쏘고 묶인 오브젝트에 적용한다(게임 스레드, 틱 밖). 재생 중이 아니거나 0 이하면 할 일이 없다.
         * @details 늦게 닿은 시작(망 지연 · 재전송)이 흐른 시간을 따라잡는 자리다 — 점프(`playFromFrame`)와 달리 그 사이의 이벤트를 잃지 않는다.
         */
        void advance( float32 seconds );
        FUNCTION( Category = "Playback", DisplayName = "Stop", CallInEditor )
        void stop();
        FUNCTION( Category = "Playback", DisplayName = "Pause", CallInEditor )
        void pause();
        FUNCTION( Category = "Playback", DisplayName = "Resume", CallInEditor )
        void resume();
        /** @brief 속도를 음수로 두고(크기는 그대로) 끝에서 거꾸로 틉니다(언리얼 `PlayReverse`). */
        FUNCTION( Category = "Playback", DisplayName = "Play Reverse", CallInEditor )
        void playReverse();
        /**
         * @brief 진행 방향의 끝 프레임으로 점프해 그 상태를 적용하고 정상 끝으로 끝냅니다(언리얼 `GoToEndAndStop`). 사이의 이벤트는 쏘지 않는다(점프).
         * @details 덮던 컷이 있으면 시네마틱 끝(Blend Out)을 알려 게임 카메라로 돌아온다. 재생 중이 아니면 아무것도 하지 않고 false 입니다.
         */
        FUNCTION( Category = "Playback", DisplayName = "Skip To End", CallInEditor )
        bool skipToEnd();
        /** @brief 재생 속도를 바꿉니다(음수 = 역방향, 재생 중에도). 0 · 유한하지 않은 값은 오류를 남기고 거절한다(false). */
        [[nodiscard]] bool setPlayRate( float32 playRate );
        float32            getPlayRate() const { return _playRate; }
        /** @brief 지금 거꾸로 진행하는지입니다(속도 부호와 왕복이 뒤집은 방향). */
        bool isReversed() const { return _player.isReversed(); }
        /** @brief 반복 방식을 바꿉니다(재생 중에도). */
        void             setLoopMode( SequenceLoopMode loopMode );
        SequenceLoopMode getLoopMode() const { return _loopMode; }
        /** @brief 컷씬 움직임 정책을 바꿉니다. 다음 재생부터 따른다(재생 중의 막기 요청은 끝내기가 같은 값으로 푼다). */
        void                   setPlayerMovement( SequencePlayerMovement movement ) { _playerMovement = movement; }
        SequencePlayerMovement getPlayerMovement() const { return _playerMovement; }
        /** @brief 시작할 때 스스로 틀지 정합니다(기본 참, 트리거가 트는 디렉터는 끈다). 코드 · 테스트가 쓰고 씬 파일은 PROPERTY 로 든다. */
        void setAutoPlay( bool bAutoPlay ) { _bAutoPlay = bAutoPlay ? SW_TRUE : SW_FALSE; }
        bool isAutoPlay() const { return _bAutoPlay == SW_TRUE; }
        /** @brief 범위 제한(`Confined`)의 대상 · 중심 · 반지름을 정합니다. 코드 · 테스트가 쓴다 — 씬 파일은 PROPERTY 로 든다. */
        void setConfine( GameObjectHandle target, GameObjectHandle center, float32 radius )
        {
            _confineTarget = target;
            _confineCenter = center;
            _confineRadius = radius;
        }
        /** @brief 이 플레이어가 지금 플레이어 입력(아래 레이어)을 막고 있으면 true 입니다(감시 값 `Sequence.InputBlocked`). */
        bool isBlockingPlayerInput() const { return _cutsceneBlock.isActive(); }
        /** @brief 빨리감기 입력이 눌려 배율이 걸려 있으면 true 입니다. */
        bool isFastForwardHeld() const { return _bFastForwardHeld == SW_TRUE; }
        /** @brief 지금 플레이어에 걸린 재생 속도입니다(빨리감기 배율 포함, 감시 값 `Sequence.PlayRate`). */
        float32 getEffectivePlayRate() const { return _player.getPlayRate(); }
        /** @brief 재생 범위를 정합니다(`SequencePlayer::setPlaybackRange` — 시퀀스 안 · 시작 < 끝이 아니면 오류 · false). 시퀀스를 다시 열면 전체로 돌아간다. */
        [[nodiscard]] bool setPlaybackRange( int32 startFrame, int32 endFrame ) { return _player.setPlaybackRange( startFrame, endFrame ); }
        /** @brief 시퀀스를 재생 중이면 true 입니다(시나리오의 `WaitSequence` 가 끝을 기다린다). */
        bool isPlaying() const { return _player.isPlaying(); }
        /** @brief 지금 프레임입니다(디버거 · 감시 값). */
        int32 getCurrentFrame() const { return _player.getCurrentFrame(); }
        /** @brief 재생하는 시퀀스입니다. */
        const SequenceAsset& getAsset() const { return _player.getAsset(); }

        /** @brief 재생할 시퀀스를 바꿉니다(코드로 만든 시퀀스). 재생 중이었으면 먼저 끝낸다. 재생 위치는 그 시퀀스의 처음입니다. */
        void setSequence( const SequenceAsset& asset );
        /**
         * @brief @p source 의 시퀀스 · 속도 · 반복을 옮겨 시간만 재는 플레이어로 둡니다(스스로 틀기는 끈다). 재생 중이었으면 먼저 끝냅니다.
         * @details 시간만 재는 재생은 바인딩을 갖지 않고 오브젝트 · 화면 · 입력 · 이벤트에 아무것도 적용하지 않는다 — 끝과 건너뛰기만 판정한다. 같은 시퀀스의 재생 둘이 같은 오브젝트를
         *          겹쳐 움직일 수 없다(적용하는 재생은 씬의 디렉터 하나 — 언리얼 `Client` RPC 컷씬은 서버가 틀지 않고, 유니티 Timeline 은 바인딩 하나를 디렉터 하나가 쥔다).
         *          서버가 다른 플레이어만 보는 재생을 따로 재는 자리(`SequenceTriggerComponent`)가 쓴다.
         */
        void copyTimelineFrom( const SequencePlayerComponent& source );
        /** @brief 재생할 시퀀스 파일을 바꾸고 바로 엽니다. 못 열면 경고하고 빈 시퀀스입니다. */
        void setSequencePath( string_view path );
        /** @brief 재생할 시퀀스 파일 경로입니다. */
        const string& getSequencePath() const { return _sequencePath.path(); }

        /** @brief 바인딩 @p bindingID 를 @p object 에 묶습니다(이미 칸이 있으면 바꾼다, 무효 핸들이면 칸을 지운다). 코드 · 테스트 · 에디터가 쓴다 — 씬 파일은 PROPERTY 로 든다. */
        void setBinding( uint32 bindingID, GameObjectHandle object );
        /** @brief 씬의 바인딩 표입니다. */
        const vector<SequenceBindingSlot>& getBindings() const { return _listBinding; }
        /** @brief 바인딩을 오브젝트로 푸는 창입니다(스폰한 오브젝트 포함). 에디터 미리보기도 이 표로 푼다. */
        SequenceBindingView makeBindingView() const;
        /** @brief 바인딩 @p bindingID 가 묶인 살아 있는 오브젝트입니다. 없으면 nullptr 입니다. */
        GameObject* findBoundObject( uint32 bindingID ) const;
        /** @brief 최근 재생 기록입니다(컷씬 디버거 · 감시 값 `Sequence.History`). */
        const SequencePlaybackHistory& getHistory() const { return _history; }

        /**
         * @brief 이벤트 항목을 지날 때 호출할 델리게이트를 등록합니다(언리얼 시퀀서 이벤트 트랙, 유니티 Timeline Signal 에 해당합니다).
         * @details 한 갱신에 지난 이벤트를 시간 순서로(루프를 되감았으면 되감기 전 끝 구간부터) 모두 알린다. 로그와 달리 Shipping 에서도 호출된다.
         *          시나리오 항목(`SequenceItemKind::Scenario`)은 알리기 전에 이 컴포넌트가 `ScenarioPlayback::play` 로 재생한다.
         *          돌려준 구독이 사라지면 뗍니다. 이 컴포넌트가 먼저 사라지면 구독은 무효가 됩니다.
         */
        [[nodiscard]] Subscription registerSequenceEvent( const OnSequenceEventDelegate& delegate );
        /**
         * @brief 카메라 신호를 받을 델리게이트를 등록합니다. 엔진은 카메라 디렉터를 모르므로 블렌드 · 프리셋 · 흔들림은 듣는 쪽(게임 프레임워크)이 합니다.
         * @details 한 갱신의 신호는 `SequenceTimelineUtil::collectPlaybackCameraSignals` 순서로 낸다. 재생을 끝낼 때 컷이 덮고 있었으면 시네마틱 끝을 낸다.
         *          대기 뷰 신호는 카메라 매니저를 거쳐 LOD 까지 간다(`CameraManagerComponent` 의 대기 뷰 설명). 돌려준 구독이 사라지면 뗍니다.
         */
        [[nodiscard]] Subscription registerCameraSignal( const OnSequenceCameraSignalDelegate& delegate );
        /** @brief 재생이 정상으로 끝날 때(끝 프레임 · `skipToEnd`) 부를 델리게이트를 등록합니다. 끝내기(되돌림 · 컷 풀기)를 마친 뒤 부른다. 돌려준 구독이 사라지면 뗍니다. */
        [[nodiscard]] Subscription registerFinished( const OnSequenceFinishedDelegate& delegate );
        /** @brief 재생 빠르기(초당 프레임)입니다. */
        float32 getFramesPerSecond() const { return _framesPerSecond; }

    private:
        /** @brief `play` · `playFromFrame` 의 공통 시작입니다. @p startFrame 이 없으면 진행 방향의 처음에서 튼다. */
        void startPlayback( optional<int32> startFrame );
        void applyTimeline( const SequencePlaybackStep& step );
        /** @brief 민 구간(`_pendingStep`)을 적용하고, 그 갱신에서 멈췄으면 끝냅니다(게임 스레드, 틱 밖). */
        void applyPendingStep();
        /** @brief 재생을 끝냅니다(클래스 머리말의 재생 계약). @p kind 는 기록에 남길 끝의 종류입니다. 쥔 것이 없으면 아무것도 하지 않는다. */
        void endPlayback( SequenceHistoryKind kind );
        /** @brief 카메라 신호의 초 단위 값과 대상을 채워 알리고 기록합니다. */
        void broadcastCameraSignals( vector<SequenceCameraSignal>& inoutListSignal );
        /** @brief `_sequencePath` 의 시퀀스를 플레이어에 엽니다. 경로가 비었으면 아무것도 하지 않고, 열지 못하면 경고합니다. */
        void loadSequenceFromPath();
        /** @brief 재생에 필요한 바인딩이 모두 묶였으면 true 입니다. 아니면 비어 있는 바인딩마다 오류와 기록을 남긴다. */
        [[nodiscard]] bool validateBindings();
        /** @brief 범위 제한(`Confined`)에 필요한 칸(대상 · 중심 · 반지름)이 찼으면 true 입니다. 아니면 오류를 남긴다(제한 없이 트는 길은 없다). */
        [[nodiscard]] bool validateConfine() const;
        /** @brief 컷씬 옵션을 겁니다(입력 레이어 · HUD 숨김). 재생 시작이 부른다. */
        void acquireCutsceneOptions();
        /** @brief `acquireCutsceneOptions` 가 건 것을 같은 수로 풉니다(빨리감기 배율 포함). 끝내기가 부른다. */
        void releaseCutsceneOptions();
        /** @brief 건너뛰기 · 빨리감기 입력을 읽습니다(틱 — 게임이 멈춰 있으면 읽지 않는다). 건너뛰기를 눌렀으면 true 입니다. */
        [[nodiscard]] bool readCutsceneInput();
        /** @brief 범위 제한 대상을 중심 둘레 반지름 안으로 되돌립니다(틱 뒤). */
        void applyConfine();

        PROPERTY( Category = "Sequence", DisplayName = "Sequence", Tooltip = "Sequence asset (.seq.xml)" )
        AssetRef<SequenceAsset> _sequencePath;
        /** @brief 시퀀스의 바인딩 id 마다 이 씬의 오브젝트입니다. 같은 시퀀스를 다른 씬에서 다른 오브젝트에 묶어 다시 쓴다. */
        PROPERTY( Category = "Sequence", DisplayName = "Bindings", Tooltip = "Scene object for each binding id of the sequence" )
        vector<SequenceBindingSlot> _listBinding;
        PROPERTY( Category = "Playback", DisplayName = "Frames Per Second", Min = 1.0, Max = 120.0 )
        float32 _framesPerSecond;
        /** @brief 재생 속도입니다. 부호가 방향이다(음수 = 역방향). 0 이면 시작에서 오류로 틀지 않는다. */
        PROPERTY( Category = "Playback", DisplayName = "Play Rate", Tooltip = "Playback speed; negative plays backwards from the end (0 is rejected)" )
        float32 _playRate;
        PROPERTY( Category = "Playback", DisplayName = "Loop Mode", Tooltip = "At the end of the range: stop, loop, or ping-pong" )
        SequenceLoopMode _loopMode;
        PROPERTY( Category = "Cutscene", DisplayName = "Player Movement", Tooltip = "Player input during playback: free, blocked (movement, look and game actions), or confined to a radius" )
        SequencePlayerMovement _playerMovement;
        /** @brief 범위 제한을 받는 오브젝트(보통 플레이어 폰)입니다. `Confined` 일 때 필수입니다. */
        PROPERTY( Category = "Cutscene", DisplayName = "Confine Target", Tooltip = "Confined: object kept inside the radius (usually the player pawn)" )
        GameObjectHandle _confineTarget;
        /** @brief 범위 제한의 중심 오브젝트입니다. `Confined` 일 때 필수입니다. */
        PROPERTY( Category = "Cutscene", DisplayName = "Confine Center", Tooltip = "Confined: object at the center of the allowed circle" )
        GameObjectHandle _confineCenter;
        PROPERTY( Category = "Cutscene", DisplayName = "Confine Radius", Min = 0.0, Units = m, Tooltip = "Confined: radius of the allowed circle on the ground plane" )
        float32 _confineRadius;
        PROPERTY( Category = "Playback", DisplayName = "Auto Play" )
        uint8 _bAutoPlay : 1;
        PROPERTY( Category = "Cutscene", DisplayName = "Hide HUD", Tooltip = "Hide the game HUD layer during playback" )
        uint8 _bHideHUD : 1;
        PROPERTY( Category = "Cutscene", DisplayName = "Skippable", Tooltip = "The cutscene input map's Cutscene.Skip jumps to the end" )
        uint8 _bSkippable : 1;
        PROPERTY( Category = "Cutscene", DisplayName = "Fast Forward", Tooltip = "Holding Cutscene.FastForward multiplies the play rate (EngineConfig _sequencer._fastForwardRate)" )
        uint8                       _bAllowFastForward : 1;
        SW_TRANSIENT uint8          _bFastForwardHeld  : 1; ///< 빨리감기 배율이 걸렸다. 저장하지 않는다
        SW_TRANSIENT uint8          _bTimelineOnly     : 1; ///< 시간만 잰다(`copyTimelineFrom`). 저장하지 않는다
        [[maybe_unused]] uint8      _reserved          : 2;
        Subscription                _hudHide;       ///< 재생 동안 건 HUD 숨김 요청(`IUILayerVisibility`). 저장하지 않는다
        Subscription                _cutsceneLayer; ///< 재생 동안 켠 컷씬 레이어(`InputManager::enableSharedLayer`). 저장하지 않는다
        Subscription                _cutsceneBlock; ///< 막기(Blocked)라 스택에 올린 컷씬 레이어. 저장하지 않는다
        SW_TRANSIENT SequencePlayer _player;
        /** @brief 이번 틱이 민 재생 구간입니다 — 틱 뒤 작업이 묶인 오브젝트에 적용한다(틱 뒤 작업은 값을 캡처하지 않는다). 저장하지 않는다. */
        SW_TRANSIENT SequencePlaybackStep _pendingStep;
        /** @brief 재생이 쥔 것(스폰 · 구간 · 되돌릴 값)입니다. 저장하지 않는다 — 재생이 만들고 끝내기가 거둔다. */
        SW_TRANSIENT SequencePlaybackState _state;
        /** @brief 최근 재생 기록입니다. 저장하지 않는다. */
        SW_TRANSIENT SequencePlaybackHistory _history;
        /** @brief 이벤트 항목 구독자들입니다. 저장하지 않습니다. 코드가 등록하는 것이기 때문입니다. */
        MulticastDelegate<void( const SequenceFiredEvent& )> _sequenceEventMulticast;
        /** @brief 카메라 신호 구독자들입니다. 저장하지 않습니다. */
        MulticastDelegate<void( const SequenceCameraSignal& )> _cameraSignalMulticast;
        /** @brief 정상 끝 구독자들입니다. 저장하지 않습니다. */
        MulticastDelegate<void()> _finishedMulticast;
    };
} // namespace sw
