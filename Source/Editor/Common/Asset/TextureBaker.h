#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw::editor
{
    struct RawImageData;
    struct TextureImportRule;

    class TextureImportConfig;

    /**
     * @enum TextureBakeMode
     * @brief 일괄 굽기(`TextureBaker::bakeAllTextures`)가 어긋난 원본을 만났을 때 할 일입니다.
     */
    enum class TextureBakeMode : uint8
    {
        BakeStale = 0, ///< 스탬프와 어긋난 원본만 다시 굽고 `bake.stamp` 를 갱신합니다.
        CheckOnly,     ///< 아무 파일도 쓰지 않고 어긋난 것만 보고합니다(시험 · CI).
    };

    /**
     * @struct TextureBakeSummary
     * @brief 일괄 굽기 한 번의 결과입니다. `_listProblem` 이 비어 있어야 원본과 DDS 가 맞는 것입니다.
     */
    struct TextureBakeSummary
    {
        /** @brief 사람이 읽는 한 줄씩입니다. CheckOnly 는 어긋남, BakeStale 은 굽지 못한 것입니다. */
        vector<string> _listProblem;
        uint32         _sourceCount;
        uint32         _bakedCount;

        TextureBakeSummary()
            : _listProblem{}
            , _sourceCount{ 0 }
            , _bakedCount{ 0 }
        {
        }

        bool isClean() const { return _listProblem.empty(); }
    };
    /**
     * @struct TextureBakeResult
     * @brief 텍스처 베이킹 결과입니다.
     */
    struct TextureBakeResult
    {
        string                 _sourcePath;
        string                 _outputPath;
        uint64                 _sourceSizeBytes;
        uint64                 _outputSizeBytes;
        uint32                 _width;
        uint32                 _height;
        uint32                 _mipCount;
        uint8                  _bSuccess : 1;
        [[maybe_unused]] uint8 _reserved : 7;

        TextureBakeResult()
            : _sourcePath{}
            , _outputPath{}
            , _sourceSizeBytes{ 0 }
            , _outputSizeBytes{ 0 }
            , _width{ 0 }
            , _height{ 0 }
            , _mipCount{ 0 }
            , _bSuccess{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };

    /**
     * @struct TextureBaker
     * @brief DirectXTex 로 소스 이미지(PNG/JPG 등)를 DDS 텍스처(밉맵 · 압축)로 굽는 에디터 애셋 베이커입니다.
     */
    struct TextureBaker
    {
        /**
         * @brief 텍스처 파일 하나를 주어진 임포트 규칙에 따라 DDS 로 굽습니다.
         * @param sourcePath 원본 소스 이미지 경로
         * @param outputPath 출력 DDS 경로
         * @param rule 적용할 임포트 규칙
         * @param pOutResult 베이킹 결과 세부 정보(선택)
         * @return 성공하면 true
         */
        [[nodiscard]] static bool bakeTexture( string_view sourcePath, string_view outputPath, const TextureImportRule& rule, TextureBakeResult* pOutResult = nullptr );

        /** @brief 스위즐 · 그린 채널 반전 같은 채널 조작을 적용합니다. */
        static void applyChannelManipulations( RawImageData& rawImage, const TextureImportRule& rule, size_t totalPixels );

        /**
         * @brief 핫 리로드가 넘긴 텍스처 파일이 소스 이미지면 굽습니다(`textures_raw/` 아래 → 옆 `textures/` 의 DDS).
         * @details 런타임은 DDS 만 읽으므로 소스 이미지는 굽는 것이 리로드입니다. 구운 DDS 의 쓰기가 다음 감시 이벤트로 와서
         *          텍스처 캐시가 다시 읽습니다. 임포트 설정은 매번 읽습니다(규칙을 고치면 재시작 없이 반영됩니다).
         *          `.hdr` 는 굽지 않고 경고합니다 — 디코더가 8비트(stb_image)라 값이 잘립니다. `textures_raw/` 밖의 소스 이미지도 경고만 합니다.
         * @return 소스 이미지였으면(구웠든 경고했든) true 이고, 호출자는 캐시를 다시 읽지 않습니다. `.dds` 면 false 입니다.
         */
        [[nodiscard]] static bool importChangedSourceImage( string_view relativePath );

        /**
         * @brief 리소스 루트 아래 모든 `textures_raw/` 의 원본을 그 폴더의 `bake.stamp` 와 대조하고, @p mode 가 BakeStale 이면 어긋난 것을 굽습니다.
         * @details 스탬프 한 줄은 `<원본 해시> <DDS 해시> <textures_raw 기준 상대 경로>` 입니다. 원본 해시는 원본 바이트와 적용한 규칙
         *          (`computeSourceHash`)이라 규칙만 바꿔도 어긋남이 됩니다. DDS 해시는 구운 결과 그대로라 손으로 바꾼 DDS 도 잡힙니다.
         *          판정은 파일 시간이 아니라 내용입니다 — 원본과 DDS 를 둘 다 커밋하므로 `git` 이 시간 순서를 임의로 뒤집습니다.
         *          원본이 사라진 스탬프 줄도 어긋남입니다(BakeStale 은 줄만 지우고 DDS 는 두므로, 남은 DDS 는 사람이 정리합니다).
         *          `.hdr` 는 굽지 못한 것으로 보고합니다 — 디코더가 8비트라 값이 잘립니다.
         * @param resourceRoot `Resource/` 의 절대 경로
         * @param config 원본마다 규칙을 고를 임포트 설정(리소스 루트 기준 상대 경로로 매칭합니다)
         */
        [[nodiscard]] static TextureBakeSummary bakeAllTextures( string_view resourceRoot, const TextureImportConfig& config, TextureBakeMode mode );

        /**
         * @brief 원본 경로에 대응하는 DDS 경로입니다(`<x>/textures_raw/<y>.png` → `<x>/textures/<y>.dds`). `textures_raw/` 구간이 없으면 빈 문자열입니다.
         */
        static string makeBakedTexturePath( string_view rawTexturePath );

        /**
         * @brief 원본 바이트와 규칙(해석한 DXGI 포맷 · 스위즐 · 밉 · sRGB · 그린 반전)과 베이커 버전을 섞은 FNV-1a 64 입니다. 읽지 못하면 0 입니다.
         */
        static uint64 computeSourceHash( string_view sourcePath, const TextureImportRule& rule );

        /**
         * @brief 에디터 설정을 거치지 않은 기본 임포트 설정 경로(`<프로젝트>/Config/Editor/TextureImportConfig.json`)입니다.
         * @details 헤드리스 굽기 · 시험이 씁니다. 에디터가 떠 있을 때는 `EditorData` 가 정한 경로를 씁니다(`importChangedSourceImage`).
         */
        static string makeDefaultImportConfigPath();
    };
} // namespace sw::editor
