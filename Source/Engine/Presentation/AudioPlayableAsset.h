/**
 * @file Engine/Presentation/AudioPlayableAsset.h
 * @brief 재생 입구(`GameSound::play`)가 받는 소리 참조의 대상 종류입니다 — 게임 데이터가 오디오 헤더 없이 소리 칸(`AssetRef<AudioPlayableAsset>`)을 듭니다.
 * @details 오디오 폴더가 아니라 표현 폴더에 둔다 — 그 칸을 든 게임 코드는 소리를 내지 않는 전용 서버도 짓는다(소리는 클라이언트의 `IGameSoundPlayer` 가 낸다).
 */
#pragma once
#include "Engine/Content/AssetTypeRegistry.h"

namespace sw
{
    /**
     * @struct AudioPlayableAsset
     * @brief 재생 입구(`GameSound::play`)가 받는 것을 가리키는 참조의 대상입니다 — 소리 파일이나 소리 그래프.
     * @details 표에 올리지 않고(접미사의 주인은 `AudioClipData` · `AudioGraphDef` 다) 타입 있는 참조의 검사에만 쓴다. 언리얼 `USoundBase` 칸이 파동과 큐를 함께 받는 자리다(기억에 의존).
     */
    struct AudioPlayableAsset
    {
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            {           ".ogg", AssetSuffixRole::Plain},
            {           ".wav", AssetSuffixRole::Plain},
            {           ".mp3", AssetSuffixRole::Plain},
            {          ".flac", AssetSuffixRole::Plain},
            {".audiograph.xml", AssetSuffixRole::Plain},
        };
        static constexpr AssetTypeDef kAssetType{ "AudioPlayable", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, {} };
    };
} // namespace sw
