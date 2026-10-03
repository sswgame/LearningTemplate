/**
 * @file SequencePlayerComponent.h
 * @brief SequenceAsset 을 재생하고 대상 오브젝트에 클립 · 이벤트를 적용합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Sequencer/SequencePlayer.h"

namespace sw
{
    /** @brief 이벤트 항목 하나를 지났을 때 부르는 델리게이트입니다. 항목은 알림 동안만 유효한 사본입니다. */
    SW_DECLARE_DELEGATE( void, OnSequenceEventDelegate, const SequenceTrackItem& item );

    REFLECT( Category = "Cinematics", DisplayName = "Sequence Player Component", Tooltip = "Plays a SequenceAsset timeline against named scene objects" )
    class SW_API SequencePlayerComponent : public Component
    {
    public:
        REFLECT_BODY();
        SequencePlayerComponent();
        virtual ~SequencePlayerComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        FUNCTION( Category = "Playback", DisplayName = "Play", CallInEditor )
        void play();
        FUNCTION( Category = "Playback", DisplayName = "Stop", CallInEditor )
        void stop();
        FUNCTION( Category = "Playback", DisplayName = "Pause", CallInEditor )
        void pause();
        FUNCTION( Category = "Playback", DisplayName = "Resume", CallInEditor )
        void resume();

        /** @brief 재생할 시퀀스를 바꿉니다(코드로 지은 시퀀스). 재생 위치는 그 시퀀스의 처음입니다. */
        void setSequence( const SequenceAsset& asset );
        /**
         * @brief 이벤트 항목을 지날 때 부를 델리게이트를 겁니다(언리얼 시퀀서 이벤트 트랙 · 유니티 Timeline Signal 에 해당).
         * @details 한 갱신에 지난 이벤트를 시간 순서로(루프를 되감았으면 되감기 전 끝 구간부터) 모두 알립니다. 로그와 달리 Shipping 에서도 불립니다.
         */
        DelegateHandle registerSequenceEvent( const OnSequenceEventDelegate& delegate );
        /** @brief `registerSequenceEvent` 로 건 델리게이트를 뗍니다. */
        void unregisterSequenceEvent( DelegateHandle handle );

    private:
        void applyTimeline();

        PROPERTY( Category = "Sequence", DisplayName = "Sequence", AssetPath, AssetType = "Sequence", Tooltip = "Sequence asset (.seq / .seq.json)" )
        string _sequencePath;
        PROPERTY( Category = "Playback", DisplayName = "Frames Per Second", Min = 1.0, Max = 120.0 )
        float32 _framesPerSecond;
        PROPERTY( Category = "Playback", DisplayName = "Loop" )
        uint8 _bLoop : 1;
        PROPERTY( Category = "Playback", DisplayName = "Auto Play" )
        uint8                  _bAutoPlay : 1;
        [[maybe_unused]] uint8 _reserved  : 6;
        SequencePlayer         _player;
        /** @brief 이벤트 항목 구독자들입니다. 저장하지 않는다 — 코드가 거는 것이다. */
        MulticastDelegate<void( const SequenceTrackItem& )> _sequenceEventMulticast;
    };
} // namespace sw
