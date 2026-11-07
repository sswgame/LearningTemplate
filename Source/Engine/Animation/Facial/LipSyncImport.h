/**
 * @file LipSyncImport.h
 * @brief 립싱크 일괄 임포트입니다. `voice/` 폴더의 음성을 디코드해 분석(`LipSyncAnalyzer`)하고, 곁에 비즘 트랙(`.visemes.xml`)을 씁니다(`App --import-lipsync`).
 * @details 분석기와 트랙 형식(`LipSync`) 곁에 둔다. 소리를 디코드하는 일은 오디오의 창구(`IVoiceDecoder`)로 부른다 — 오디오를 짓지 않는 전용 서버에는 디코더가 없어 오류로 멈춘다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct LipSyncSettings;

    /**
     * @struct LipSyncImport
     * @brief 음성 경로 규칙과 일괄 임포트입니다.
     */
    struct SW_API LipSyncImport
    {
        /** @brief 리소스 상대 경로가 음성 폴더 안의 소리 파일인지 반환합니다(`.../voice/...` 의 .wav, .ogg). */
        static bool isVoiceAudio( string_view relativePath );
        /** @brief 곁 파일 규칙(`SW_ASSET_COMPANION`)입니다. 음성(`isVoiceAudio`)이면 그 비즘 트랙(`VisemeTrack::makePathForAudio`), 아니면 빈 글 — 런타임은 트랙이 있으면 쓴다(`FacialAnimationComponent::speak`). */
        static string makeCompanionTrackPath( string_view assetID );
        /**
         * @brief `Content/` 아래 음성마다 트랙을 씁니다.
         * @return 쓴 트랙 수입니다. 읽거나 쓰지 못한 음성은 @p outFailedCount 에 셉니다.
         */
        static uint32 importAll( const string& resourceRoot, const LipSyncSettings& settings, uint32& outFailedCount );
    };
} // namespace sw
