#include "pch.h"

#include "Engine/Sequencer/SequenceSpanTracks.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimationAssetCache.h"
#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/Component/AudioEmitterComponent.h"
#include "Engine/Audio/Component/SceneAudio.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/ISubtitleSink.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    namespace
    {
        struct SequenceSpanTracksInternal
        {
            /** @brief 항목 구간의 길이(초)입니다. 끝 프레임도 구간이라 하나를 더한다. */
            static float32 computeSpanSeconds( const SequenceSpanContext& context )
            {
                const float32 fps = context._framesPerSecond > 0.0f ? context._framesPerSecond : 30.0f;
                return static_cast<float32>( context._pItem->_end - context._pItem->_start + 1 ) / fps;
            }

            static SkeletalAnimatorComponent* findAnimator( const SequenceSpanContext& context )
            {
                return context._pTarget != nullptr ? context._pTarget->getComponent<SkeletalAnimatorComponent>() : nullptr;
            }

            static void enterAnimation( const SequenceSpanContext& context )
            {
                if ( findAnimator( context ) == nullptr )
                {
                    SW_LOG_ERROR_ID( "SequenceSpan.AnimationNoAnimator", "Sequence animation '%#' - the bound object '%#' has no SkeletalAnimatorComponent", context._pItem->_name,
                                     context._pTarget != nullptr ? context._pTarget->getName().c_str() : "?" );
                    return;
                }
                context._pState->_clip = SharedAssetCache<AnimationClip>::acquire( context._pItem->_clip );
                if ( context._pState->_clip == nullptr )
                    SW_LOG_ERROR_ID( "SequenceSpan.AnimationClipMissing", "Sequence animation '%#' - clip '%#' could not be read", context._pItem->_name, context._pItem->_clip );
            }

            static void updateAnimation( const SequenceSpanContext& context )
            {
                SkeletalAnimatorComponent* pAnimator = findAnimator( context );
                if ( pAnimator == nullptr || context._pState->_clip == nullptr )
                    return;
                const float32 fps  = context._framesPerSecond > 0.0f ? context._framesPerSecond : 30.0f;
                const float32 time = static_cast<float32>( context._frame - context._pItem->_start ) / fps;
                pAnimator->setSequencerOverride( context._pState->_clip, time, MathUtil::saturate( context._pItem->_amplitude ) );
            }

            static void leaveAnimation( const SequenceSpanContext& context )
            {
                SkeletalAnimatorComponent* pAnimator = findAnimator( context );
                if ( pAnimator != nullptr )
                    pAnimator->setSequencerOverride( nullptr, 0.0f, 0.0f );
                context._pState->_clip.reset();
            }

            static AudioEngine* findAudioEngine( const SequenceSpanContext& context )
            {
                return context._pManager != nullptr ? context._pManager->getSubsystem<SceneAudio>().findAudioEngine() : nullptr;
            }

            static void enterAudio( const SequenceSpanContext& context )
            {
                const hashed_string eventName( context._pItem->_sound.c_str() );
                // 바인딩이 있으면 그 자리에서 낸다(에미터가 없으면 데이터 결함이다). 없으면 화면 소리(에미터 0)다.
                if ( context._pItem->_binding != kSequenceNoBinding )
                {
                    AudioEmitterComponent* pEmitter = context._pTarget != nullptr ? context._pTarget->getComponent<AudioEmitterComponent>() : nullptr;
                    if ( pEmitter == nullptr )
                    {
                        SW_LOG_ERROR_ID( "SequenceSpan.AudioNoEmitter", "Sequence audio '%#' - the bound object has no AudioEmitterComponent", context._pItem->_name );
                        return;
                    }
                    context._pState->_playingID = pEmitter->post( eventName );
                    return;
                }
                AudioEngine* pEngine        = findAudioEngine( context );
                context._pState->_playingID = pEngine != nullptr ? pEngine->postEvent( eventName ) : 0;
            }

            static void leaveAudio( const SequenceSpanContext& context )
            {
                AudioEngine* pEngine = findAudioEngine( context );
                if ( pEngine != nullptr && context._pState->_playingID != 0 )
                    pEngine->stop( context._pState->_playingID, -1.0f ); // 음수는 이벤트의 페이드다
                context._pState->_playingID = 0;
            }

            static void enterSubtitle( const SequenceSpanContext& context )
            {
                ISubtitleSink* pSubtitles = engine::findSubsystem<ISubtitleSink>();
                if ( pSubtitles == nullptr )
                    return; // 보일 곳이 없는 구성(전용 서버 · 하네스)이다
                const string& key      = context._pItem->_text;
                const utf8*   pText    = LocalizedText::find( key, key.c_str() );
                const utf8*   pSpeaker = context._pTarget != nullptr ? context._pTarget->getName().c_str() : "";
                pSubtitles->postSubtitle( pSpeaker, pText, computeSpanSeconds( context ) );
            }
        };
    } // namespace

    const SequenceSpanHandler kSequenceAnimationSpanHandler{ &SequenceSpanTracksInternal::enterAnimation, &SequenceSpanTracksInternal::updateAnimation, &SequenceSpanTracksInternal::leaveAnimation };
    const SequenceSpanHandler kSequenceAudioSpanHandler{ &SequenceSpanTracksInternal::enterAudio, nullptr, &SequenceSpanTracksInternal::leaveAudio };
    const SequenceSpanHandler kSequenceSubtitleSpanHandler{ &SequenceSpanTracksInternal::enterSubtitle, nullptr, nullptr };
} // namespace sw
