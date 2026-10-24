#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
    class TaskManager;
} // namespace sw

namespace sw::editor
{
    enum class AssetImportMode : uint8;

    struct AssetImportSummary;
    struct RawImageData;
    struct TextureImportRule;

    class TextureImportConfig;
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct TextureImportResult
     * @brief 텍스처 임포트 결과입니다.
     */
    struct TextureImportResult
    {
        string                 _sourcePath;
        string                 _outputPath;
        uint64                 _sourceSizeBytes;
        uint64                 _outputSizeBytes;
        uint32                 _width;
        uint32                 _height;
        uint32                 _mipCount;
        uint32                 _sourceChannelCount; ///< 원본 이미지의 채널 수(1 회색 · 2 회색+알파 · 3 RGB · 4 RGBA). 압축 형식과 알파 유무를 고르는 근거다.
        uint8                  _bSuccess : 1;
        [[maybe_unused]] uint8 _reserved : 7;

        TextureImportResult()
            : _sourcePath{}
            , _outputPath{}
            , _sourceSizeBytes{ 0 }
            , _outputSizeBytes{ 0 }
            , _width{ 0 }
            , _height{ 0 }
            , _mipCount{ 0 }
            , _sourceChannelCount{ 0 }
            , _bSuccess{ SW_FALSE }
            , _reserved{ 0 }
        {
        }
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct TextureImporter
     * @brief DirectXTex 로 소스 이미지(PNG/JPG 등)를 DDS 텍스처(밉맵 · 압축)로 임포트하는 에디터 애셋 임포터입니다.
     */
    struct TextureImporter
    {
        /**
         * @brief 텍스처 파일 하나를 주어진 임포트 규칙에 따라 DDS 로 임포트합니다.
         * @param sourcePath 원본 소스 이미지 경로입니다.
         * @param outputPath 출력 DDS 경로입니다.
         * @param rule 적용할 임포트 규칙입니다.
         * @param pOutResult 임포트 결과의 세부 정보입니다. 생략할 수 있습니다.
         * @param pTaskManager 밴드 압축을 나눠 돌릴 작업 시스템입니다. 생략하면(nullptr) 부른 스레드가 차례로 압축하며 결과 바이트는 같습니다.
         * @details 일괄 임포트는 이 함수를 워커에서 부른다. 워커는 에디터 서비스 범위를 읽을 수 없으므로(`findEditorScope`) 작업 시스템은 안에서 찾지 않고 인자로 받는다.
         * @return 성공하면 true 입니다.
         */
        [[nodiscard]] static bool importTexture( string_view sourcePath, string_view outputPath, const TextureImportRule& rule, TextureImportResult* pOutResult = nullptr,
                                                 TaskManager* pTaskManager = nullptr );

        /** @brief 스위즐이나 그린 채널 반전 같은 채널 조작을 적용합니다. */
        static void applyChannelManipulations( RawImageData& rawImage, const TextureImportRule& rule, size_t totalPixels );

        /**
         * @brief 핫 리로드가 넘긴 텍스처 파일이 소스 이미지이면 임포트합니다. `textures_raw/` 아래의 소스 이미지를 옆 `textures/` 의 DDS 로 만듭니다.
         * @details 런타임은 DDS 만 읽으므로 소스 이미지는 임포트하는 것이 리로드다. 임포트한 DDS 를 쓰면 다음 감시 이벤트로 들어오고,
         *          텍스처 캐시가 다시 읽는다. 임포트 설정은 매번 읽으므로 규칙을 고치면 재시작 없이 반영된다.
         *          `.hdr` 는 부동소수점으로 읽어 BC6H 와 RGBA16F 로만 임포트한다. 규칙의 포맷이 8비트이면 실패한다. `textures_raw/` 밖의 소스 이미지는 경고만 한다.
         * @return 소스 이미지였으면 true 입니다. 임포트했든 경고만 했든 같으며, 호출하는 쪽은 캐시를 다시 읽지 않습니다. `.dds` 이면 false 입니다.
         */
        [[nodiscard]] static bool importChangedSourceImage( string_view relativePath );

        /**
         * @brief 리소스 루트 아래 모든 `textures_raw/` 의 원본을 그 폴더의 `import.stamp` 와 대조합니다. @p mode 가 ImportStale 이면 어긋난 것을 임포트합니다.
         * @details 절차는 `AssetImportStampUtil::importAll` 을 따른다. 스탬프 한 줄은 `<원본 해시> <DDS 해시> <textures_raw 기준 상대 경로>` 형식이다. 원본 해시는 원본 바이트와 적용한 규칙
         *          (`computeSourceHash`)으로 계산하므로 규칙만 바꿔도 어긋남이 된다. DDS 해시는 임포트된 결과 그대로이므로 손으로 바꾼 DDS 도 잡힌다.
         *          판정은 파일 시간이 아니라 내용으로 한다. 원본과 DDS 를 둘 다 커밋하므로 `git` 이 시간 순서를 임의로 뒤집기 때문이다.
         *          원본이 사라진 스탬프 줄도 어긋남이다. ImportStale 은 줄만 지우고 DDS 는 두므로, 남은 DDS 는 사람이 정리한다.
         *          `.hdr` 는 부동소수점으로 읽어 BC6H 와 RGBA16F 로만 임포트한다. 규칙의 포맷이 8비트이면 그 원본의 실패로 보고한다.
         * @param resourceRoot `Resource/` 의 절대 경로입니다.
         * @param config 원본마다 규칙을 고르는 임포트 설정입니다. 리소스 루트 기준 상대 경로로 매칭합니다.
         */
        [[nodiscard]] static AssetImportSummary importAllTextures( string_view resourceRoot, const TextureImportConfig& config, AssetImportMode mode );

        /**
         * @brief 원본 경로에 해당하는 DDS 경로를 만듭니다(`<x>/textures_raw/<y>.png` → `<x>/textures/<y>.dds`). `textures_raw/` 구간이 없으면 빈 문자열입니다.
         */
        static string makeImportedTexturePath( string_view rawTexturePath );

        /**
         * @brief 원본 바이트, 규칙(해석한 DXGI 포맷, 스위즐, 밉, sRGB, 그린 반전), 임포터 버전을 섞어 FNV-1a 64 해시를 계산합니다. 읽지 못하면 0 입니다.
         */
        static uint64 computeSourceHash( string_view sourcePath, const TextureImportRule& rule );

        /**
         * @brief 에디터 설정을 거치지 않은 기본 임포트 설정 경로(`<프로젝트>/Config/Editor/TextureImportConfig.json`)를 반환합니다.
         * @details 헤드리스 임포트와 테스트가 쓴다. 에디터가 떠 있을 때는 `EditorToolDefaults` 가 정한 경로를 쓴다(`importChangedSourceImage`).
         */
        static string makeDefaultImportConfigPath();
    };
} // namespace sw::editor
