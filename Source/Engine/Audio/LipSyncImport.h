/**
 * @file LipSyncImport.h
 * @brief 립싱크 일괄 임포트입니다. `voice/` 폴더의 음성을 디코드해 분석(`LipSyncAnalyzer`)하고, 곁에 비즘 트랙(`.visemes.xml`)을 씁니다(`App --import-lipsync`).
 * @details 소리를 디코드하는 일(디코더)이 Audio 에 있으므로 이 기능도 여기에 있다. 분석기와 트랙 형식은 Animation(하위 티어)의 `Animation/Facial/LipSync` 에 있다.
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
        /**
         * @brief `Content/` 아래 음성마다 트랙을 씁니다.
         * @return 쓴 트랙 수입니다. 읽거나 쓰지 못한 음성은 @p outFailedCount 에 셉니다.
         */
        static uint32 importAll( const string& resourceRoot, const LipSyncSettings& settings, uint32& outFailedCount );
    };
} // namespace sw
