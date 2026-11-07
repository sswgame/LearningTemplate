#include "pch.h"

#include "Engine/Sequencer/SequenceSpanTracks.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Animation/AnimationAssetCache.h"
#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/ISubtitleSink.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Presentation/ISoundPlayer.h"

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

            /** @brief 시퀀스가 도는 씬이 받은 소리 창구입니다. 없으면(전용 서버 — 오디오를 짓지 않는다 · 하네스) nullptr 입니다. */
            static ISoundPlayer* findSoundPlayer( const SequenceSpanContext& context )
            {
                return context._pManager != nullptr ? locateSubsystem<ISoundPlayer>( context._pManager->getSceneEngineServices()._pEngineSubsystems ) : nullptr;
            }

            static void enterAudio( const SequenceSpanContext& context )
            {
                ISoundPlayer* pSound = findSoundPlayer( context );
                if ( pSound == nullptr )
                    return; // 들을 곳이 없는 구성이다
                const hashed_string eventName( context._pItem->_sound.c_str() );
                // 바인딩이 있으면 그 오브젝트의 에미터에서 낸다(에미터가 없으면 창구가 데이터 결함으로 남긴다). 없으면 화면 소리다.
                if ( context._pItem->_binding != kSequenceNoBinding )
                {
                    if ( context._pTarget == nullptr )
                    {
                        SW_LOG_ERROR_ID( "SequenceSpan.AudioNoTarget", "Sequence audio '%#' - the binding resolved to no object", context._pItem->_name );
                        return;
                    }
                    context._pState->_playingID = pSound->postEventOn( *context._pTarget, eventName );
                    return;
                }
                context._pState->_playingID = pSound->postEvent( eventName );
            }

            static void leaveAudio( const SequenceSpanContext& context )
            {
                ISoundPlayer* pSound = findSoundPlayer( context );
                if ( pSound != nullptr )
                    pSound->stop( context._pState->_playingID, -1.0f ); // 음수는 이벤트의 페이드다
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
