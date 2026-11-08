#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Engine/Audio/IAudioService.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/UserSettings/UserSettingsHandlerRegistry.h"

namespace sw
{
    namespace
    {
        /**
         * @struct AudioUserSettingsHandlersInternal
         * @brief 사용자 설정의 오디오 핸들러(`audio.busVolume` — param 은 버스, `audio.mute`, `audio.speakerMode`)입니다. 엔진 서비스의 오디오에 값을 넣습니다.
         * @details 사용자 설정 폴더는 오디오를 모른다 — 이 묶음이 정적 등록(`SW_USER_SETTINGS_HANDLER_SET`)으로 스스로 오른다. 오디오 서비스가 없으면(하네스) 값을 읽기만 하고 넣지 않는다.
         */
        struct AudioUserSettingsHandlersInternal
        {
            static bool applyBusVolume( const UserSettingApplyParams& context )
            {
                float32 volume{ 0.0f };
                if ( StringUtil::parseFloat( context._value, volume ) == false )
                    return false;
                if ( IAudioService* pAudio = engine::findSubsystem<IAudioService>() )
                    pAudio->setBusVolume( hashed_string( context._param ), volume );
                return true;
            }

            static bool applyMute( const UserSettingApplyParams& context )
            {
                bool bMute{ false };
                if ( StringUtil::tryParseBool( context._value, bMute ) == false )
                    return false;
                if ( IAudioService* pAudio = engine::findSubsystem<IAudioService>() )
                    pAudio->setMute( bMute );
                return true;
            }

            static bool applySpeakerMode( const UserSettingApplyParams& context )
            {
                AudioSpeakerMode mode{ AudioSpeakerMode::Auto };
                if ( IAudioService::tryParseSpeakerMode( context._value, mode ) == false )
                    return false;
                IAudioService* pAudio = engine::findSubsystem<IAudioService>();
                // 장치가 그만큼의 채널을 갖지 않으면 서비스가 오류를 남기고 거절한다(지금 구성을 그대로 쓴다).
                return pAudio == nullptr || pAudio->setSpeakerMode( mode );
            }

            static bool isSpeakerModeName( string_view value )
            {
                AudioSpeakerMode mode{ AudioSpeakerMode::Auto };
                return IAudioService::tryParseSpeakerMode( value, mode );
            }

            static void registerHandlers( UserSettingsHandlerRegistry& registry, vector<Subscription>& outListRegistration )
            {
                outListRegistration.push_back( registry.registerHandler( "audio.busVolume", SW_DELEGATE_FUNCTION( UserSettingHandlerDelegate, &AudioUserSettingsHandlersInternal::applyBusVolume ) ) );
                outListRegistration.push_back( registry.registerHandler( "audio.mute", SW_DELEGATE_FUNCTION( UserSettingHandlerDelegate, &AudioUserSettingsHandlersInternal::applyMute ) ) );
                outListRegistration.push_back( registry.registerHandler( "audio.speakerMode", SW_DELEGATE_FUNCTION( UserSettingHandlerDelegate, &AudioUserSettingsHandlersInternal::applySpeakerMode ),
                                                                         SW_DELEGATE_FUNCTION( UserSettingValueFilterDelegate, &AudioUserSettingsHandlersInternal::isSpeakerModeName ) ) );
            }
        };
    } // namespace

    SW_USER_SETTINGS_HANDLER_SET( Audio, &AudioUserSettingsHandlersInternal::registerHandlers );
} // namespace sw
