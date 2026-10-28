/**
 * @file Texture2D.h
 * @brief DDS 파일을 GPU 텍스처로 업로드하고 bindless SRV 인덱스를 보관하는 텍스처 에셋입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"

#include "Engine/Asset/AssetID.h"
#include "Engine/Common/AssetTypeRegistry.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    struct DDSImageData;

    /** @brief 텍스처 품질이 읽을 때 버리는 위쪽 밉입니다(`Texture2D::computeMipDrop`). */
    struct Texture2DMipDrop
    {
        uint32 _mipCount{ 0 };     ///< 버리는 밉 수
        uint64 _skippedBytes{ 0 }; ///< 페이로드 앞에서 건너뛸 바이트(버린 밉들의 크기 합)
    };
} // namespace sw

namespace sw
{
    /**
     * @enum TextureResidency
     * @brief DDS 를 GPU 에 업로드할 때 밉 사슬을 어디까지 올리는지입니다(텍스처 밉 스트리밍, 계획 PreloadAndLOD P6).
     */
    enum class TextureResidency : uint8
    {
        Full = 0,      ///< 품질 설정(`gv_textureMipDrop`)이 남긴 밉 사슬 전부
        StreamingBase, ///< 아래쪽 밉만(밉 0 의 짧은 변이 `Texture2D::kMinMipDropExtent` 근처) — 위쪽 밉은 `upgradeResidency` 가 뒤에 올린다
    };
} // namespace sw

namespace sw
{
    /** @brief `Texture2D::beginUploadFromImage` · `beginUploadFromCPUBytes` 의 결과입니다. */
    enum class Texture2DUploadStep : uint8
    {
        Submitted = 0, ///< 텍스처를 만들고 업로드를 냈다. `finishUpload` 가 끝남을 확인하고 bindless 에 등록한다
        Busy,          ///< 스테이징이 차서 못 냈다(만든 텍스처는 지웠다). 다음 프레임에 다시 부른다
        Failed,        ///< 되풀이해도 안 된다(오류를 남겼고 만든 것은 지웠다)
    };

    /** @brief `Texture2D::finishUpload` 의 결과입니다. */
    enum class Texture2DUploadStatus : uint8
    {
        Pending = 0, ///< GPU 가 아직 끝내지 않았다
        Ready,       ///< 끝났고 bindless 에 등록했다(`isRHIValid` 가 참이다)
        Failed,      ///< 끝나지 않을 업로드이거나 등록이 실패했다(오류를 남겼고 만든 것은 지웠다)
    };

    /** @brief 스트리밍이 위쪽 밉을 올리며 내놓은 옛 GPU 자원입니다(`Texture2D::upgradeResidency`). 그 SRV 를 읽는 패킷이 끝난 뒤 해제합니다. */
    struct Texture2DRetiredResource
    {
        RHITextureHandle   _handle{ 0 };
        RHIDescriptorIndex _srv{ kInvalidDescriptorIndex };
    };
} // namespace sw

namespace sw
{
    class IRHIDevice;
    class IRHIResourceFactory;

    /**
     * @class Texture2D
     * @brief 리소스 경로의 DDS 를 읽어 createTexture2D + uploadTexture2DAndWait + registerBindlessTexture 까지 한 번에 하는 에셋입니다.
     * @details 셰이더는 SRV 인덱스만 받습니다(MaterialCB 의 uint 슬롯). 머티리얼이 Texture2D 프로퍼티의 assetPath 로
     *          이 에셋을 얻어 인덱스를 패킹합니다. 소유권은 TextureCache 가 갖습니다.
     */
    class SW_API Texture2D final : public RHIRenderResource
    {
    public:
        /** @brief 텍스처 파일의 접미사입니다(쿠킹한 DDS). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".dds", AssetSuffixRole::Plain },
        };
        /** @brief 텍스처 에셋 종류입니다(등록은 `Texture2D.cpp` 의 `SW_ASSET_TYPE`). 전용 서버 패키지가 빼는 묶음 `Texture` 다. */
        static constexpr AssetTypeDef kAssetType{ "Texture", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, "Texture" };

        /** @brief 빈 텍스처로 만듭니다. */
        Texture2D();
        /** @brief GPU 자원이 남아 있으면 경고만 남깁니다. 해제는 releaseRHI 로 명시합니다. */
        ~Texture2D() override;
        Texture2D( const Texture2D& )            = delete;
        Texture2D& operator=( const Texture2D& ) = delete;

        /** @brief 리소스 상대 경로(전역 id, 예: engine/textures/random/grass.dds)의 DDS 를 GPU 에 업로드합니다(`readImage` 다음 `loadFromImage`). */
        [[nodiscard]] bool loadFromAsset( IRHIDevice* pDevice, const AssetID& assetID );
        /**
         * @brief 리소스의 DDS 를 읽고 이 엔진이 올릴 수 있는 형식인지 확인합니다. 디바이스를 쓰지 않으므로 어느 스레드에서든 부릅니다(비동기 로드의 워커 단계).
         * @return 읽지 못했거나 포맷 · 차원이 맞지 않으면 오류를 남기고 false 를 반환합니다.
         */
        [[nodiscard]] static bool readImage( const AssetID& assetID, DDSImageData& outImage );
        /**
         * @brief `readImage` 가 읽은 이미지를 GPU 에 업로드합니다. 디바이스를 쓰는 스레드에서만 부릅니다(DX11 즉시 컨텍스트 · GL 컨텍스트는 스레드에 묶인다).
         * @param residency `StreamingBase` 면 아래쪽 밉만 업로드합니다 — 위쪽 밉은 `upgradeResidency` 가 같은 이미지로 뒤에 업로드합니다(`isFullyResident`).
         */
        [[nodiscard]] bool loadFromImage( IRHIDevice* pDevice, string_view relativePath, const DDSImageData& image, TextureResidency residency );
        /**
         * @brief 새 업로드의 1단계입니다 — GPU 텍스처를 만들고 밉 사슬 업로드를 제출합니다. 기다리지 않습니다(`bBlocking` 이 아니면). 디바이스를 쓰는 스레드에서만 부릅니다.
         * @details 세 단계(워커 읽기 → `beginUploadFromImage` → 이후 프레임의 `finishUpload`)의 가운데다(RHIAsyncUploads U8). 제출한 텍스처는 `finishUpload` 가 `Ready` 를 돌려주기 전에는
         *          bindless 에 등록하지 않는다(전송 계약 1 · 8) — 그 사이 `isRHIValid` 는 거짓이다.
         * @param bBlocking 참이면 `Busy` 를 기다려 넘긴다(블로킹 헬퍼 — 동기 `acquire` 의 `wait()`). 거짓이면 `Busy` 를 그대로 돌려준다.
         */
        [[nodiscard]] Texture2DUploadStep beginUploadFromImage( IRHIDevice* pDevice, string_view relativePath, const DDSImageData& image, TextureResidency residency, bool bBlocking );
        /**
         * @brief 제출한 업로드(`beginUploadFromImage` · `beginUploadFromCPUBytes`)가 끝났는지 보고, 끝났으면 bindless 에 등록합니다.
         * @param bBlocking 참이면 끝날 때까지 기다립니다(10 초 상한 — 넘으면 오류와 `Failed`). 거짓이면 `Pending` 을 그대로 돌려줍니다.
         * @details 제출한 업로드가 없거나(장치 교체로 `forgetRHI` · `releaseRHI` 가 지움) 다른 디바이스면 `Failed` 입니다. 실패하면 만든 GPU 텍스처를 지운다.
         */
        [[nodiscard]] Texture2DUploadStatus finishUpload( IRHIDevice* pDevice, bool bBlocking );
        /** @brief 제출했고 아직 `finishUpload` 가 끝내지 않은 업로드가 있으면 true 입니다. */
        bool hasUploadInFlight() const { return _uploadTicket.isValid(); }
        /**
         * @brief 아래쪽 밉만 있는 텍스처(`StreamingBase`)에 밉 사슬 전부를 새 GPU 텍스처로 올리는 1단계입니다 — 새 텍스처를 만들고 업로드를 제출합니다(렌더 스레드, 기록 전).
         * @details 첫 업로드와 같은 세 단계다(RHIAsyncUploads U8): 이 함수 → 이후 프레임의 `finishUpgradeResidency`. 그 사이 이 텍스처는 아래쪽 밉 그대로 그려진다. 실패하면 아래쪽 밉 그대로
         *          남는다(오류를 남긴다). 이미 올리는 중이거나 올릴 것이 없으면 `Failed` 다(오류 없음).
         * @param bBlocking 참이면 `Busy` 를 기다려 넘긴다. 거짓이면 `Busy` 를 그대로 돌려줍니다(만든 텍스처는 지웠다).
         */
        [[nodiscard]] Texture2DUploadStep beginUpgradeResidency( IRHIDevice* pDevice, const DDSImageData& image, bool bBlocking );
        /**
         * @brief `beginUpgradeResidency` 가 낸 업로드가 끝났는지 보고, 끝났으면 새 텍스처를 bindless 에 등록해 바꿔 끼웁니다(렌더 스레드).
         * @details 옛 자원은 @p outRetired 로 내놓는다: 게임 스레드가 이미 만든 패킷이 옛 SRV 번호를 값으로 가지고 있으므로 그 패킷이 끝난 뒤에 해제한다(`TextureCache::releaseRetiredTextures`).
         *          SRV 가 바뀌므로 부르는 쪽이 리로드 세대를 올려 머티리얼이 새 번호를 다시 받게 한다.
         * @return `Ready` 면 바꿔 끼웠다. `Failed` 면 아래쪽 밉 그대로이고 새 텍스처는 지웠다(장치 교체로 낸 업로드가 사라진 경우 포함).
         */
        [[nodiscard]] Texture2DUploadStatus finishUpgradeResidency( IRHIDevice* pDevice, bool bBlocking, Texture2DRetiredResource& outRetired );
        /** @brief 위쪽 밉 업로드를 제출했고 아직 `finishUpgradeResidency` 가 끝내지 않았으면 true 입니다. */
        bool hasUpgradeInFlight() const { return _upgrade._ticket.isValid(); }
        /**
         * @brief 가진 GPU 텍스처 · SRV 를 해제하지 않고 @p outRetired 로 내놓고 이 객체를 빈 상태(디바이스 없음)로 만듭니다(렌더 타깃 크기 변경 — `TextureCache::resizeRenderTarget`).
         * @details `releaseRHI` 와 달리 핸들을 반환하지 않는다. 내놓은 자원은 부르는 쪽이 지연 해제 줄(`TextureCache::releaseRetiredTextures`)에 넘겨, 이미 만든 패킷이 끝난 뒤에 해제한다.
         *          크기 · 포맷 · 렌더 타깃 표시는 남겨 다시 만들 때 쓴다(`releaseRHI` 와 같다). 렌더 타깃 전용이다 — 위쪽 밉을 올리는 중인 텍스처에는 부르지 않는다(assert).
         */
        void detachRHI( Texture2DRetiredResource& outRetired );
        /** @brief 품질 설정이 남긴 밉 사슬이 모두 GPU 에 있으면 true 입니다(스트리밍이 아직 위쪽 밉을 올리지 않았으면 false). */
        bool isFullyResident() const { return _streamedMipCount == 0; }
        /** @brief 아직 GPU 에 없는 위쪽 밉 수입니다(품질 설정이 버린 밉은 세지 않는다). */
        uint32 getStreamedMipCount() const { return _streamedMipCount; }
        /**
         * @brief 그리고 읽는 렌더 타깃으로 만듭니다(렌더 텍스처: CCTV 모니터, 백미러). 경로는 이름입니다(`rendertarget/<이름>`, 파일이 아닙니다).
         * @details 렌더러가 카메라의 이미지를 여기에 그리고, 머티리얼은 DDS 텍스처와 같은 SRV 로 읽습니다. 디바이스가 바뀌면 같은 크기로 다시 만듭니다(내용은 비어 있습니다).
         */
        [[nodiscard]] bool createRenderTarget( IRHIDevice* pDevice, string_view name, uint32 width, uint32 height, RHIFormat format );
        /**
         * @brief CPU 픽셀(RGBA8, 밉 하나)을 원본으로 둡니다(CPU 텍스처: 지형마다의 스플랫). GPU 자원은 만들지 않습니다(`loadFromCPUBytes`).
         * @details 원본은 이 객체가 보관해 디바이스가 바뀌어도 칠한 내용이 남습니다. 공개 전(게임 스레드, `TextureCache::createCPUTexture`)에만 부릅니다.
         */
        void setCPUBytes( string_view name, uint32 width, uint32 height, vector<uint8> rgbaBytes );
        /** @brief CPU 픽셀 원본을 GPU 에 업로드합니다(렌더 스레드). 이미 업로드되어 있으면 true 입니다. */
        [[nodiscard]] bool loadFromCPUBytes( IRHIDevice* pDevice );
        /** @brief CPU 픽셀 원본 업로드의 1단계입니다(`beginUploadFromImage` 와 같은 규칙). 호출 전에 `isRHIValid` 로 이미 올라간 텍스처를 거릅니다. 원본이 없으면 `Failed` 입니다. */
        [[nodiscard]] Texture2DUploadStep beginUploadFromCPUBytes( IRHIDevice* pDevice, bool bBlocking );
        /**
         * @brief CPU 픽셀 원본의 사각형을 @p desc(RGBA8 행)로 바꾸고, GPU 에 올라가 있으면 그 구간의 업로드를 냅니다(렌더 스레드). GPU 완료를 기다리지 않습니다.
         * @return 원본만 고쳤거나 업로드가 받아들여졌으면 `Accepted` 입니다. `Busy` 면 원본은 고쳤고 GPU 구간은 못 냈으므로 호출자가 다음 프레임에 같은 호출을 다시 합니다.
         *         원본이 없거나 범위나 크기가 틀리거나 RHI 가 거절하면 `Rejected` 입니다.
         */
        [[nodiscard]] RHISubmitResult writeCPUBytes( IRHIDevice* pDevice, const RHITextureRegionUploadDesc& desc );
        /** @brief CPU 픽셀 원본을 가진 텍스처이면 true 입니다. */
        bool                 hasCPUBytes() const { return _cpuBytes.empty() == false; }
        const vector<uint8>& getCPUBytes() const { return _cpuBytes; }
        /** @brief (RHIRenderResource) 살아 있는 디바이스에 텍스처를 돌려줍니다. */
        void releaseRHI( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 디바이스가 이미 없을 때 부릅니다. 핸들만 잊습니다. */
        void forgetRHI( IRHIDevice* pDevice ) override;
        /**
         * @brief (RHIRenderResource) 새 디바이스에 같은 경로의 DDS 를 다시 업로드합니다(렌더 타깃이면 같은 크기로 다시 만듭니다).
         * @details CPU 텍스처는 여기서 업로드하지 않습니다. 빌린 머티리얼이 디바이스 교체 때 놓았다가 다시 빌리면 `TextureCache::acquire` 가 업로드합니다.
         *          참조가 0 인 CPU 텍스처는 GPU 자원이 없다는 캐시의 불변식을 지키기 위해서입니다.
         */
        [[nodiscard]] bool initRHI( IRHIDevice* pDevice ) override;

        bool isRHIValid() const { return getHandle() != 0 && getSRV() != kInvalidDescriptorIndex; }
        /** @brief GPU 텍스처 핸들입니다. 스트리밍이 렌더 스레드에서 바꿔 끼우므로 원자값으로 읽는다(`upgradeResidency`). */
        RHITextureHandle getHandle() const { return _handle.load( std::memory_order_acquire ); }
        /** @brief bindless SRV 번호입니다. 게임 스레드(머티리얼)가 읽는 동안 렌더 스레드가 바꿔 끼울 수 있어 원자값으로 읽는다. */
        RHIDescriptorIndex getSRV() const { return _srv.load( std::memory_order_acquire ); }
        uint32             getWidth() const { return _width; }
        uint32             getHeight() const { return _height; }
        RHIFormat          getFormat() const { return _format; }
        const string&      getPath() const { return _path; }
        /** @brief 렌더 타깃(`createRenderTarget`)으로 만든 텍스처이면 true 입니다. */
        bool isRenderTarget() const { return _bRenderTarget == SW_TRUE; }

        /** @brief DDS 가 알려 주는 DXGI 포맷 번호를 RHIFormat 으로 바꿉니다. 대응이 없으면 Unknown 입니다. */
        static RHIFormat toRHIFormatFromDxgi( uint32 dxgiFormat );
        /**
         * @brief 텍스처 품질(`gv_textureMipDrop`)로 버릴 위쪽 밉을 고릅니다. 밉 사슬이 있는 텍스처만 줄이고, 남은 밉 0 은 `kMinMipDropExtent` 이상이고
         *        블록 크기의 배수여야 합니다(BC 는 4 의 배수 — 아니면 백엔드가 만들지 못한다). 마지막 밉 하나는 늘 남깁니다.
         * @details 밉이 하나뿐인 텍스처(UI 아이콘 · 글꼴 아틀라스 · 룩업 표)는 줄이지 않는다. 화면에 1:1 로 그리는 것이라 밉을 만들지 않기 때문이다.
         */
        static Texture2DMipDrop computeMipDrop( RHIFormat format, uint32 width, uint32 height, uint32 mipCount, int32 requestedDrop );

        /** @brief 밉을 버린 뒤 밉 0 의 짧은 변이 이보다 작아지지 않습니다(작은 텍스처는 버려도 얻는 메모리가 없다). */
        static constexpr uint32 kMinMipDropExtent = 64;

    private:
        /**
         * @brief 만든 텍스처 @p handle 에 @p upload 를 제출합니다. `Accepted` 면 @p outTicket 에 티켓을 담습니다. `Busy`(비차단일 때) · 거절 · 시간 초과면 텍스처를 지우고 그 결과를 돌려줍니다.
         * @details 첫 업로드 두 갈래(밉 사슬 · CPU 원본)가 같은 정리를 쓴다 — 따로 쓰면 한쪽만 지우기를 빠뜨린다.
         */
        [[nodiscard]] static Texture2DUploadStep submitCreatedTexture( IRHIResourceFactory& factory, RHITextureHandle handle, const RHITextureUploadDesc& upload, bool bBlocking,
                                                                       string_view label, RHITransferTicket& outTicket );
        /**
         * @brief 새 GPU 텍스처를 만들고 위쪽 @p drop 밉을 뺀 사슬의 업로드를 제출합니다(SRV 는 등록하지 않는다). 실패하면 오류를 남기고 만든 것을 지우며 @p outHandle 은 0 입니다.
         * @details 처음 업로드(`beginUploadFromImage`)와 스트리밍의 바꿔 끼우기(`beginUpgradeResidency`)가 같은 순서를 쓴다 — 둘이 따로 쓰면 한쪽만 실패 정리를 빠뜨린다.
         */
        [[nodiscard]] static Texture2DUploadStep createAndSubmitMips( IRHIDevice& device, string_view relativePath, const DDSImageData& image, RHIFormat format,
                                                                      const Texture2DMipDrop& drop, bool bBlocking, RHITextureDesc& outDesc, RHITextureHandle& outHandle,
                                                                      RHITransferTicket& outTicket );

        /** @brief 제출했고 끝나지 않은 위쪽 밉 업로드입니다(`beginUpgradeResidency`). 새 텍스처는 `finishUpgradeResidency` 가 등록해 바꿔 끼운다. */
        struct PendingUpgrade
        {
            RHITextureHandle  _handle{ 0 };
            RHITransferTicket _ticket{};
            uint32            _width{ 0 };
            uint32            _height{ 0 };
            uint32            _mipCount{ 0 };
        };

    private:
        /** @brief 이 텍스처를 업로드한 디바이스입니다. 통보가 내 것인지 가릴 때 씁니다. */
        IRHIDevice*                _pDevice;
        string                     _path;
        atomic<RHITextureHandle>   _handle;
        atomic<RHIDescriptorIndex> _srv;
        RHITransferTicket          _uploadTicket{}; ///< 제출했고 `finishUpload` 가 끝내지 않은 업로드의 티켓(무효면 없음). 디바이스를 쓰는 스레드만 만지는 값이다
        PendingUpgrade             _upgrade{};      ///< 제출했고 `finishUpgradeResidency` 가 끝내지 않은 위쪽 밉 업로드(티켓이 무효면 없음). 디바이스를 쓰는 스레드만 만진다
        uint32                     _width;
        uint32                     _height;
        uint32                     _mipCount;
        uint32                     _streamedMipCount; ///< 아직 GPU 에 없는 위쪽 밉 수(`TextureResidency::StreamingBase` 로 올렸을 때). 0 이면 다 올라와 있다
        RHIFormat                  _format;
        uint8                      _bRenderTarget; ///< `createRenderTarget` 으로 만들었다. 다시 만들 때 DDS 를 읽지 않는다
        vector<uint8>              _cpuBytes;      ///< CPU 텍스처의 원본(RGBA8 밉 0). 비었으면 DDS 또는 렌더 타깃이다
    };
} // namespace sw
