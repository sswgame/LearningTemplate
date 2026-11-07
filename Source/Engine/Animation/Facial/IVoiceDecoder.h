/**
 * @file Engine/Animation/Facial/IVoiceDecoder.h
 * @brief 립싱크가 음성 파일을 모노 PCM 으로 푸는 창구입니다 — 얼굴 애니메이션 · 립싱크 임포트는 오디오 헤더를 읽지 않습니다.
 * @details 구현은 오디오 폴더(`Engine/Audio/AudioVoiceDecoder.cpp`)의 정적 등록 하나다. 전용 서버 구성은 오디오를 짓지 않아 창구가 없다 —
 *          `--import-lipsync` 는 서버에서 클라이언트 전용 작업으로 거절되고, 런타임 립싱크는 임포트가 만든 비즘 트랙만 쓴다(`IShaderCookService` 와 같은 갈림).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Content/AssetID.h"

namespace sw
{
    /** @class IVoiceDecoder @brief 음성 리소스를 모노 샘플로 풉니다. 어느 스레드에서 불러도 됩니다(상태가 없다). */
    class SW_API IVoiceDecoder
    {
    public:
        virtual ~IVoiceDecoder() = default;

        /**
         * @brief @p assetID 를 읽어 모노 샘플(-1..1)과 표본율로 풉니다.
         * @return 읽지 못했거나 풀 수 없는 형식이면 false 입니다(오류는 디코더가 남긴다).
         */
        [[nodiscard]] virtual bool decodeMono( const AssetID& assetID, vector<float32>& outListSample, uint32& outSampleRate ) = 0;

        /** @brief 등록된 창구입니다. 오디오를 짓지 않는 구성(전용 서버)은 nullptr 입니다. */
        [[nodiscard]] static IVoiceDecoder* find();

        /** @struct Registration @brief 오디오 폴더가 정적 객체로 창구를 겁니다(이미지에 하나). */
        struct SW_API Registration
        {
            explicit Registration( IVoiceDecoder& decoder );
        };
    };
} // namespace sw
