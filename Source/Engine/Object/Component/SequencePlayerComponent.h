/**
 * @file SequencePlayerComponent.h
 * @brief SequenceAsset을 재생하고 대상 오브젝트에 클립/이벤트를 적용합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Sequencer/SequencePlayer.h"

namespace sw
{
    namespace generated
    {
        struct sw_SequencePlayerComponent_Registrar;
    } // namespace generated

    REFLECT( Category = "Cinematics", DisplayName = "Sequence Player Component", Tooltip = "Plays a SequenceAsset timeline against named scene objects" )
    class SW_API SequencePlayerComponent : public Component
    {
        friend struct ::sw::generated::sw_SequencePlayerComponent_Registrar;

    public:
        REFLECT_BODY();
        SequencePlayerComponent();
        virtual ~SequencePlayerComponent() override                              = default;
        SequencePlayerComponent( SequencePlayerComponent&& ) noexcept            = default;
        SequencePlayerComponent& operator=( SequencePlayerComponent&& ) noexcept = default;

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
    };
} // namespace sw
