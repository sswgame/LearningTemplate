/**
 * @file TextureFormatFamily.h
 * @brief 대상별 텍스처 압축 형식 표(`EngineConfig::_textureFormats`)를 풀고, 실행 때 장치가 지원하는 첫 묶음을 고릅니다.
 * @details 임포트한 DDS(`a.dds`)는 BC 묶음 그대로이고 다른 묶음은 쿠킹이 곁에 쓴다(`a.astc.dds` — `TextureImporter::cookTextureVariants`). 고르기가 BC 면 접미사가 비어 파일을 묻지도 않으므로
 *          데스크톱의 읽기 · 그림은 이 표가 생기기 전과 같다. Godot 의 형식별 파일(`.s3tc.ctex` · `.etc2.ctex`)과 첫 지원 형식 고르기와 같은 모양이다(기억에 의존한 비교). 언리얼은 플랫폼마다 한 벌을 굽는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIEnums.h"

namespace sw
{
    class IRHIDevice;
    class TypeRegistry;

    struct TextureFormatSettings;
} // namespace sw

namespace sw
{
    /** @brief 텍스처의 용도입니다. 임포트 규칙의 형식에서 나오고(`TextureImportFormatUtil::getUsage`) 묶음마다 쓸 형식을 정합니다. */
    enum class TextureFormatUsage : uint8
    {
        Color,  ///< 색 · 일반 데이터
        Normal, ///< 2채널 노멀맵
        Mask,   ///< 단일 채널
        HDR,    ///< 1 을 넘는 값
        Raw,    ///< 압축하지 않은 형식 — 어느 장치나 읽어 묶음과 무관하다
    };

    /** @struct TextureFormatFamily @brief 푼 형식 묶음 하나입니다. */
    struct SW_API TextureFormatFamily
    {
        string    _name;
        string    _suffix;
        RHIFormat _color{ RHIFormat::Unknown };
        RHIFormat _normal{ RHIFormat::Unknown };
        RHIFormat _mask{ RHIFormat::Unknown };
        RHIFormat _hdr{ RHIFormat::Unknown };

        /** @brief @p usage 에 쓸 형식입니다. `Raw` 는 `Unknown` 입니다(임포트 규칙의 형식 그대로). */
        [[nodiscard]] RHIFormat getFormat( TextureFormatUsage usage ) const;

        // 파일 머리말 참고.
        /**
         * @brief 표에서 대상 @p targetName 의 묶음을 우선순위대로 @p outListFamily 에 풉니다.
         * @return 모르는 대상 · 묶음 · 형식 이름이 있으면 오류를 남기고 false 입니다(반쯤 푼 목록을 남기지 않는다).
         */
        [[nodiscard]] static bool resolveTarget( const TextureFormatSettings& settings, string_view targetName, const TypeRegistry& types, vector<TextureFormatFamily>& outListFamily );
        /**
         * @brief 빌드 플랫폼 @p platformName(`build::kPlatformName`)이 쓰는 대상의 이름입니다. 실행은 이 대상의 묶음에서 고른다.
         * @return 그 플랫폼을 적은 대상이 없거나 둘 이상이면 오류를 남기고 빈 글입니다(데스크톱 대상으로 대신 고르지 않는다).
         */
        [[nodiscard]] static string findPlatformTarget( const TextureFormatSettings& settings, string_view platformName );
        /**
         * @brief 쿠킹이 굽는 묶음 — 모든 대상이 적은 묶음 가운데 접미사가 있는 것(임포트한 DDS 가 아닌 것)을 처음 나온 순서로 겹치지 않게 @p outListFamily 에 풉니다.
         * @details 쿠킹은 대상마다 따로 하지 않는다 — 한 번 구운 팩이 모든 배포 대상(데스크톱 · Android · 웹)에 같고, 실행이 장치에 맞는 곁 변형을 고른다(Godot 의 형식별 파일과 같은 모양).
         * @return 모르는 묶음 · 형식 이름이 있으면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool resolveCookFamilies( const TextureFormatSettings& settings, const TypeRegistry& types, vector<TextureFormatFamily>& outListFamily );
        /** @brief @p listFamily 가운데 @p device 가 네 용도를 모두 지원하는 첫 묶음입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] static const TextureFormatFamily* selectFamily( const vector<TextureFormatFamily>& listFamily, const IRHIDevice& device );
        /**
         * @brief 엔진 설정에서 이 빌드 플랫폼의 대상(`findPlatformTarget`)으로 @p device 가 쓸 묶음의 접미사입니다. 엔진 서비스가 없는 도구 · 테스트는 빈 글입니다.
         * @details 표가 틀렸거나 지원하는 묶음이 없으면 오류를 남기고 빈 글입니다 — 읽기는 임포트한 DDS 로 가고, 장치가 못 쓰는 형식이면 텍스처 생성이 오류로 멈춘다.
         */
        [[nodiscard]] static string selectVariantSuffix( const IRHIDevice& device );
        /** @brief `a/b.dds` + `.astc` → `a/b.astc.dds` 입니다. `.dds` 가 아니면 빈 글입니다. */
        [[nodiscard]] static string makeVariantPath( string_view ddsPath, string_view suffix );
    };

} // namespace sw
