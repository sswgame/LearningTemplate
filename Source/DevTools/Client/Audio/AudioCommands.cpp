#include "pch.h"

#include "Core/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioSubsystem.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineSubsystems.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 개발 명령 `audio.post <event>` — 읽은 이벤트 라이브러리의 소리 이벤트를 화면 소리(2D)로 냅니다.
         * @details 대사 음성의 자막(이벤트의 자막 키 → 자막 서비스)과 믹스를 시나리오 · 콘솔에서 게임 코드 없이 확인한다(언리얼 콘솔 `PlaySoundCue` 자리). 배포본에는 등록되지 않는다.
         */
        struct AudioCommandsInternal
        {
            static bool runPost( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                AudioSubsystem* const pAudio = engine::findSubsystem<AudioSubsystem>();
                if ( pAudio == nullptr || pAudio->isInitialized() == false )
                {
                    outReply = "audio.post: no audio service";
                    return true;
                }
                const AudioPlayingID                     playingID = pAudio->getEngine().postEvent( hashed_string( listArgument[0] ) ); // 모르는 이벤트는 엔진이 알린다
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "audio.post '%#': playing id %#", listArgument[0].c_str(), playingID );
                outReply = string( replyBuilder.view() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( AudioPost, "audio.post", "audio.post <event>", "Post a sound event of the loaded event library as a 2D sound (dialogue lines with subtitles, mix checks)",
                    &AudioCommandsInternal::runPost );
} // namespace sw
