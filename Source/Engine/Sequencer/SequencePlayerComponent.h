/**
 * @file SequencePlayerComponent.h
 * @brief SequenceAsset 을 재생하고 대상 오브젝트에 클립과 이벤트를 적용합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Sequencer/SequencePlayer.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

namespace sw
{
    /** @brief 이벤트 항목 하나를 지났을 때 부르는 델리게이트입니다. 항목은 알림 동안만 유효한 사본입니다. */
    SW_DECLARE_DELEGATE( void, OnSequenceEventDelegate, const SequenceTrackItem& item );
    /** @brief 카메라 신호(컷 바뀜, 시네마틱 시작 · 끝, 대기 뷰, 흔들림) 하나를 냈을 때 부르는 델리게이트입니다. */
    SW_DECLARE_DELEGATE( void, OnSequenceCameraSignalDelegate, const SequenceCameraSignal& signal );

    REFLECT( Category = "Cinematics", DisplayName = "Sequence Player Component", Tooltip = "Plays a SequenceAsset timeline against named scene objects", Meta = "Icon=Sequence" )
    class SW_API SequencePlayerComponent : public Component
    {
    public:
        REFLECT_BODY();
        SequencePlayerComponent();
        virtual ~SequencePlayerComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;
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

        /** @brief 재생할 시퀀스를 바꿉니다(코드로 만든 시퀀스). 재생 위치는 그 시퀀스의 처음입니다. */
        void setSequence( const SequenceAsset& asset );
        /** @brief 재생할 시퀀스 파일을 바꾸고 바로 엽니다. 못 열면 경고하고 빈 시퀀스입니다. */
        void setSequencePath( string_view path );
        /** @brief 재생할 시퀀스 파일 경로입니다. */
        const string& getSequencePath() const { return _sequencePath; }
        /**
         * @brief 이벤트 항목을 지날 때 호출할 델리게이트를 등록합니다(언리얼 시퀀서 이벤트 트랙, 유니티 Timeline Signal 에 해당합니다).
         * @details 한 갱신에 지난 이벤트를 시간 순서로(루프를 되감았으면 되감기 전 끝 구간부터) 모두 알린다. 로그와 달리 Shipping 에서도 호출된다.
         */
        DelegateHandle registerSequenceEvent( const OnSequenceEventDelegate& delegate );
        /** @brief `registerSequenceEvent` 로 건 델리게이트를 뗍니다. */
        void unregisterSequenceEvent( DelegateHandle handle );
        /**
         * @brief 카메라 신호를 받을 델리게이트를 등록합니다. 엔진은 카메라 디렉터를 모르므로 블렌드 · 프리셋 · 흔들림은 듣는 쪽(게임 프레임워크)이 합니다.
         * @details 한 갱신의 신호는 `SequenceTimelineUtil::collectPlaybackCameraSignals` 순서로 낸다. 재생을 멈출 때 컷이 덮고 있었으면 시네마틱 끝을 낸다.
         *          대기 뷰 신호를 듣는 렌더 쪽(LOD, 스트리밍, PSO 미리 만들기)은 아직 없다.
         */
        DelegateHandle registerCameraSignal( const OnSequenceCameraSignalDelegate& delegate );
        /** @brief `registerCameraSignal` 로 건 델리게이트를 뗍니다. */
        void unregisterCameraSignal( DelegateHandle handle );
        /** @brief 재생 빠르기(초당 프레임)입니다. */
        float32 getFramesPerSecond() const { return _framesPerSecond; }

    private:
        void applyTimeline();
        /** @brief 카메라 신호의 초 단위 값을 채워 알립니다. */
        void broadcastCameraSignals( vector<SequenceCameraSignal>& inoutListSignal );
        /** @brief `_sequencePath` 의 시퀀스를 플레이어에 엽니다. 경로가 비었으면 아무것도 하지 않고, 열지 못하면 경고합니다. */
        void loadSequenceFromPath();

        PROPERTY( Category = "Sequence", DisplayName = "Sequence", AssetPath, AssetType = "Sequence", Tooltip = "Sequence asset (.seq.xml)" )
        string _sequencePath;
        PROPERTY( Category = "Playback", DisplayName = "Frames Per Second", Min = 1.0, Max = 120.0 )
        float32 _framesPerSecond;
        PROPERTY( Category = "Playback", DisplayName = "Loop" )
        uint8 _bLoop : 1;
        PROPERTY( Category = "Playback", DisplayName = "Auto Play" )
        uint8                  _bAutoPlay : 1;
        [[maybe_unused]] uint8 _reserved  : 6;
        SequencePlayer         _player;
        /** @brief 이벤트 항목 구독자들입니다. 저장하지 않습니다. 코드가 등록하는 것이기 때문입니다. */
        MulticastDelegate<void( const SequenceTrackItem& )> _sequenceEventMulticast;
        /** @brief 카메라 신호 구독자들입니다. 저장하지 않습니다. */
        MulticastDelegate<void( const SequenceCameraSignal& )> _cameraSignalMulticast;
    };
} // namespace sw
