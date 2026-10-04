/**
 * @file LipSyncImport.h
 * @brief 립싱크 일괄 임포트 — `voice/` 폴더의 음성을 풀어 분석(`LipSyncAnalyzer`)하고 곁에 비즘 트랙(`.visemes.json`)을 씁니다(`App --import-lipsync`).
 * @details 소리를 푸는 일(디코더)이 Audio 라 여기 있습니다 — 분석기 · 트랙 형식은 Animation(아래 티어)의 `Animation/Facial/LipSync` 입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    struct LipSyncSettings;

    /**
     * @struct LipSyncImport
     * @brief 음성 경로 규칙과 일괄 임포트입니다.
     */
    struct SW_API LipSyncImport
    {
        /** @brief 리소스 상대 경로가 음성 폴더 안의 소리 파일인지입니다(`.../voice/...` 의 .wav · .ogg). */
        static bool isVoiceAudio( string_view relativePath );
        /**
         * @brief `Resource/` 아래 음성마다 트랙을 씁니다.
         * @return 쓴 트랙 수입니다. 읽거나 쓰지 못한 음성은 @p outFailedCount 에 셉니다.
         */
        static uint32 importAll( const string& resourceRoot, const LipSyncSettings& settings, uint32& outFailedCount );
    };
} // namespace sw
