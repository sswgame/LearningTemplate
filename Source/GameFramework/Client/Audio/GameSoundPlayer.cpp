#include "pch.h"

#include "Core/Concurrency/atomic.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/Component/AudioEmitterComponent.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IGameSoundPlayer.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 게임 코드의 소리 창구(`IGameSoundPlayer`)를 엔진의 오디오 서비스로 잇는 클라이언트 쪽 구현입니다. 상태는 한 번 쓰는 에미터 번호뿐이다.
         * @details 전용 서버는 이 소스를 짓지 않아 `GameSound` 가 아무것도 내지 않는다.
         */
        class GameSoundPlayer final : public IGameSoundPlayer
        {
        public:
            bool play( const AssetID& assetID, const hashed_string& bus ) override
            {
                IAudioService* pAudio = game::findSubsystem<IAudioService>();
                return pAudio != nullptr && pAudio->play( assetID, bus );
            }

            bool playMusic( const AssetID& assetID ) override
            {
                IAudioService* pAudio = game::findSubsystem<IAudioService>();
                return pAudio != nullptr && pAudio->playMusic( assetID );
            }

            bool loadEvents( const AssetID& assetID ) override
            {
                const string_view path    = assetID.view();
                AudioEngine*      pEngine = findEngine();
                if ( pEngine == nullptr )
                    return true; // 오디오 서비스가 없으면(헤드리스) 할 일이 없다
                AudioEventLibrary library;
                if ( library.loadFromAsset( assetID ) == false )
                    return false;
                return pEngine->loadEventLibrary( hashed_string( path ), library );
            }

            void unloadEvents( string_view path ) override
            {
                AudioEngine* pEngine = findEngine();
                if ( pEngine != nullptr )
                    pEngine->unloadEventLibrary( hashed_string( path ) );
            }

            GameSoundPlayingID postEvent( const hashed_string& eventName ) override
            {
                AudioEngine* pEngine = findEngine();
                return pEngine != nullptr ? pEngine->postEvent( eventName, 0 ) : 0;
            }

            GameSoundPlayingID postEventAt( const hashed_string& eventName, const float3& position ) override
            {
                AudioEngine* pEngine = findEngine();
                if ( pEngine == nullptr )
                    return 0;
                // 한 번 쓰는 에미터 — 자리를 두고 내고 바로 지운다. 엔진은 그 자리의 소리가 끝날 때까지 자리를 남긴다.
                const AudioEmitterID emitterID = kOneShotEmitterBase + _nextOneShotEmitter.fetch_add( 1 );
                pEngine->setEmitter( emitterID, position, float3{} );
                const AudioPlayingID playingID = pEngine->postEvent( eventName, emitterID );
                pEngine->removeEmitter( emitterID );
                return playingID;
            }

            void setObjectParameter( const GameObject& object, const hashed_string& name, float32 value ) override
            {
                object.forEachComponentOfType<AudioEmitterComponent>( [&name, value]( AudioEmitterComponent* pEmitter )
                { pEmitter->setParameter( name, value ); } );
            }

        private:
            /** @brief 한 번 쓰는 에미터 id 의 시작입니다 — 컴포넌트 id(작은 수부터 센다)와 겹치지 않는 높은 자리. */
            static constexpr AudioEmitterID kOneShotEmitterBase = AudioEmitterID{ 1 } << 62;

            /** @brief 오디오 엔진입니다. 서비스가 없거나 내려가 있으면 nullptr 입니다. */
            static AudioEngine* findEngine()
            {
                IAudioService* pAudio = game::findSubsystem<IAudioService>();
                return pAudio != nullptr && pAudio->isInitialized() ? &pAudio->getEngine() : nullptr;
            }

            atomic<uint64> _nextOneShotEmitter{ 0 };
        };

        GameSoundPlayer                      s_gameSoundPlayer;
        const IGameSoundPlayer::Registration s_gameSoundPlayerRegistration{ s_gameSoundPlayer };
    } // namespace
} // namespace sw
