/**
 * @file Engine/Presentation/AudioPlayableAsset.h
 * @brief 재생 입구(`GameSound::play`)가 받는 소리 참조의 대상 종류입니다 — 게임 데이터가 오디오 헤더 없이 소리 칸(`AssetRef<AudioPlayableAsset>`)을 듭니다.
 * @details 오디오 폴더가 아니라 표현 폴더에 둔다. 그 칸을 든 게임 코드는 소리를 내지 않는 전용 서버도 짓는다(소리는 엔진 서비스 `ISoundPlayer` 가 내고, 서버에는 그 칸이 없다).
 */
#pragma once
#include "Engine/Content/AssetTypeRegistry.h"

namespace sw
{
    /** @brief 소리 파일의 코덱입니다. 접미사 칸(`AssetSuffix::_format`)이 정하고, 디코더 고르기가 이것을 본다(확장자 글을 다시 비교하지 않는다). */
    enum class AudioFileFormat : uint8
    {
        Vorbis = 0, ///< `AudioVorbisDecoder`(스트리밍 가능)
        WAV,        ///< PCM16 · float32 는 공통 디코더, 그 밖(ADPCM)은 백엔드의 대체 디코더
        Other,      ///< 공통 디코더가 없는 형식(MP3 · FLAC) — 백엔드의 대체 디코더만 읽는다
    };

    /**
     * @struct AudioClipAssetKind
     * @brief 소리 파일 에셋 종류의 정의(접미사 · 코덱)입니다. `AudioClipData::kAssetType` 이 이 값을 가리킨다.
     * @details 오디오 폴더 밖에 둔다 — 소리를 내지 않는 전용 서버의 TU(립싱크 가져오기의 음성 판정)도 오디오 헤더 없이 이 표를 읽는다(`PrefabAssetKind.h` 와 같은 꼴).
     */
    struct AudioClipAssetKind
    {
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".ogg", AssetSuffixRole::Plain, AssetSuffix::makeFormat( AudioFileFormat::Vorbis )},
            { ".wav", AssetSuffixRole::Plain,    AssetSuffix::makeFormat( AudioFileFormat::WAV )},
            { ".mp3", AssetSuffixRole::Plain,  AssetSuffix::makeFormat( AudioFileFormat::Other )},
            {".flac", AssetSuffixRole::Plain,  AssetSuffix::makeFormat( AudioFileFormat::Other )},
        };
        /** @brief 소리 에셋 종류입니다. 전용 서버 패키지가 빼는 묶음 `Audio` 다. */
        static constexpr AssetTypeDef kAssetType{ "Audio", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, "Audio" };

        /** @brief @p path 의 코덱입니다(접미사 칸). 소리 파일이 아니면 `Other` 입니다. */
        [[nodiscard]] static AudioFileFormat findFileFormat( string_view path )
        {
            const AssetSuffix* pSuffix = kAssetType.findSuffix( path );
            return pSuffix != nullptr ? pSuffix->getFormat<AudioFileFormat>() : AudioFileFormat::Other;
        }
        /** @brief 공통 디코더(Vorbis · WAV)가 읽는 소리면 true 입니다. */
        [[nodiscard]] static bool isDecodable( string_view path ) { return findFileFormat( path ) != AudioFileFormat::Other; }
    };

    /** @brief 소리 그래프 에셋 종류의 정의입니다. `AudioGraphDef::kAssetType` 이 이 값을 가리킨다(오디오 헤더 없이 소리 참조가 읽는다). */
    struct AudioGraphAssetKind
    {
        /** @brief 소리 그래프 파일의 접미사입니다. */
        static constexpr string_view kExtension        = ".audiograph.xml";
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { kExtension, AssetSuffixRole::Plain },
        };
        /** @brief 소리 파일과 같이 전용 서버 패키지가 빼는 묶음 `Audio` 다(서버는 그래프를 읽지 않는다). */
        static constexpr AssetTypeDef kAssetType{ "AudioGraph", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, "Audio" };
    };

    /**
     * @struct AudioPlayableAsset
     * @brief 재생 입구(`GameSound::play`)가 받는 것을 가리키는 참조의 대상입니다 — 소리 파일이나 소리 그래프.
     * @details 표에 올리지 않고 타입 있는 참조의 검사에만 쓰는 합 종류다(`AssetTypeDef::makeUnion`). 접미사는 구성 종류의 것이라 다시 적지 않는다.
     */
    struct AudioPlayableAsset
    {
        static constexpr const AssetTypeDef* kArrMemberType[] = { &AudioClipAssetKind::kAssetType, &AudioGraphAssetKind::kAssetType };
        static constexpr AssetTypeDef        kAssetType       = AssetTypeDef::makeUnion( "AudioPlayable", kArrMemberType );
    };

    /** @brief 적응형 음악 파일(`*.music.xml`)을 가리키는 참조(`AssetRef<AudioMusicAsset>`)의 대상입니다 — `GameSound::playAdaptiveMusic` 이 받는다. 같은 까닭으로 검사에만 쓴다. */
    struct AudioMusicAsset
    {
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".music.xml", AssetSuffixRole::Plain },
        };
        static constexpr AssetTypeDef kAssetType{ "AudioMusic", kArrAssetSuffix, static_cast<uint32>( SW_COUNT_OF( kArrAssetSuffix ) ), {}, 0, {} };
    };
} // namespace sw
