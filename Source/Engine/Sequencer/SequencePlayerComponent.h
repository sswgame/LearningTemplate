/**
 * @file SequencePlayerComponent.h
 * @brief SequenceAsset 을 재생하고, 씬의 바인딩 표로 묶은 대상 오브젝트에 클립 · 키 · 구간 동작 · 이벤트를 적용합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
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
    class SequenceAsset;
    /** @brief 이벤트 항목 하나를 지났을 때 부르는 델리게이트입니다. 항목은 알림 동안만 유효한 사본이고 대상은 그 바인딩의 오브젝트입니다. */
    SW_DECLARE_DELEGATE( void, OnSequenceEventDelegate, const SequenceFiredEvent& event );
    /** @brief 카메라 신호(컷 바뀜, 시네마틱 시작 · 끝, 대기 뷰, 흔들림) 하나를 냈을 때 부르는 델리게이트입니다. */
    SW_DECLARE_DELEGATE( void, OnSequenceCameraSignalDelegate, const SequenceCameraSignal& signal );

    /**
     * @class SequencePlayerComponent
     * @brief 시퀀스 하나를 재생합니다. 시퀀스의 바인딩 id 마다 이 씬의 오브젝트를 `_listBinding` 으로 묶는다(유니티 PlayableDirector 의 바인딩, 언리얼 Level Sequence Actor 의 Binding Overrides).
     * @details **재생 계약**: 시작할 때 재생에 필요한 바인딩이 모두 묶였는지 보고, 하나라도 비면 오류를 남기고 틀지 않는다(이름 찾기 폴백 없음).
     *          끝(루프가 아닐 때 끝 프레임) · `stop` · 씬 나감(`onEndPlay`) · 재생 중 다시 틀기(먼저 멈춘다) · 시퀀스 바꾸기는 모두 같은 끝내기(`endPlayback`)를 지난다 —
     *          덮던 컷이 있으면 시네마틱 끝을 알리고, 구간 동작(소리 · 애니메이션 덮어쓰기)을 나가고, 스폰을 지우고, 컷을 풀고, 시퀀스의 `_whenFinished` 가
     *          `RestoreState` 면 시작 전 상태로 되돌린다. 일시정지는 끝내기가 아니다.
     */
    REFLECT( Category = "Cinematics", DisplayName = "Sequence Player Component", Tooltip = "Plays a SequenceAsset timeline against the scene objects bound to its binding ids", Meta = "Icon=Sequence" )
    class SW_API SequencePlayerComponent : public Component
    {
    public:
        REFLECT_BODY();
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
        FUNCTION( Category = "Playback", DisplayName = "Stop", CallInEditor )
        void stop();
        FUNCTION( Category = "Playback", DisplayName = "Pause", CallInEditor )
        void pause();
        FUNCTION( Category = "Playback", DisplayName = "Resume", CallInEditor )
        void resume();
        /** @brief 시퀀스를 재생 중이면 true 입니다(시나리오의 `WaitSequence` 가 끝을 기다린다). */
        bool isPlaying() const { return _player.isPlaying(); }
        /** @brief 지금 프레임입니다(디버거 · 감시 값). */
        int32 getCurrentFrame() const { return _player.getCurrentFrame(); }
        /** @brief 재생하는 시퀀스입니다. */
        const SequenceAsset& getAsset() const { return _player.getAsset(); }

        /** @brief 재생할 시퀀스를 바꿉니다(코드로 만든 시퀀스). 재생 중이었으면 먼저 끝낸다. 재생 위치는 그 시퀀스의 처음입니다. */
        void setSequence( const SequenceAsset& asset );
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
        /** @brief 재생 빠르기(초당 프레임)입니다. */
        float32 getFramesPerSecond() const { return _framesPerSecond; }

    private:
        void applyTimeline( const SequencePlaybackStep& step );
        /** @brief 재생을 끝냅니다(클래스 머리말의 재생 계약). @p kind 는 기록에 남길 끝의 종류입니다. 쥔 것이 없으면 아무것도 하지 않는다. */
        void endPlayback( SequenceHistoryKind kind );
        /** @brief 카메라 신호의 초 단위 값과 대상을 채워 알리고 기록합니다. */
        void broadcastCameraSignals( vector<SequenceCameraSignal>& inoutListSignal );
        /** @brief `_sequencePath` 의 시퀀스를 플레이어에 엽니다. 경로가 비었으면 아무것도 하지 않고, 열지 못하면 경고합니다. */
        void loadSequenceFromPath();
        /** @brief 재생에 필요한 바인딩이 모두 묶였으면 true 입니다. 아니면 비어 있는 바인딩마다 오류와 기록을 남긴다. */
        [[nodiscard]] bool checkBindings();

        PROPERTY( Category = "Sequence", DisplayName = "Sequence", Tooltip = "Sequence asset (.seq.xml)" )
        AssetRef<SequenceAsset> _sequencePath;
        /** @brief 시퀀스의 바인딩 id 마다 이 씬의 오브젝트입니다. 같은 시퀀스를 다른 씬에서 다른 오브젝트에 묶어 다시 쓴다. */
        PROPERTY( Category = "Sequence", DisplayName = "Bindings", Tooltip = "Scene object for each binding id of the sequence" )
        vector<SequenceBindingSlot> _listBinding;
        PROPERTY( Category = "Playback", DisplayName = "Frames Per Second", Min = 1.0, Max = 120.0 )
        float32 _framesPerSecond;
        PROPERTY( Category = "Playback", DisplayName = "Loop" )
        uint8 _bLoop : 1;
        PROPERTY( Category = "Playback", DisplayName = "Auto Play" )
        uint8                  _bAutoPlay : 1;
        [[maybe_unused]] uint8 _reserved  : 6;
        SequencePlayer         _player;
        /** @brief 재생이 쥔 것(스폰 · 구간 · 되돌릴 값)입니다. 저장하지 않는다 — 재생이 만들고 끝내기가 거둔다. */
        SequencePlaybackState _state;
        /** @brief 최근 재생 기록입니다. 저장하지 않는다. */
        SequencePlaybackHistory _history;
        /** @brief 이벤트 항목 구독자들입니다. 저장하지 않습니다. 코드가 등록하는 것이기 때문입니다. */
        MulticastDelegate<void( const SequenceFiredEvent& )> _sequenceEventMulticast;
        /** @brief 카메라 신호 구독자들입니다. 저장하지 않습니다. */
        MulticastDelegate<void( const SequenceCameraSignal& )> _cameraSignalMulticast;
    };
} // namespace sw
