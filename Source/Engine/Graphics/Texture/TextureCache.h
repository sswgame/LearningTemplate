/**
 * @file TextureCache.h
 * @brief 경로를 키로 Texture2D 소유권을 관리합니다(MaterialCache 와 같은 구조).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetHandle.h"
#include "Engine/Asset/Cache/IAssetCache.h"
#include "Engine/FileSystem/AssetID.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Texture/Texture2D.h"

namespace sw
{
    struct DDSImageData;

    class IRHIDevice;
    class Texture2D;
    class TextureLoadBlock;

    /** @brief 텍스처 로드 핸들입니다. 핸들이 살아 있는 동안 준비된 텍스처의 참조 하나를 쥡니다(놓으면 `release` 와 같다). */
    using TextureHandle = AssetHandle<TextureLoadBlock>;

    /**
     * @class TextureCache
     * @brief 리소스 상대 경로를 키로 Texture2D 를 소유합니다. 머티리얼은 포인터만 빌립니다.
     * @note 디바이스 수명은 이 캐시의 일이 아닙니다. `Texture2D` 가 `RHIRenderResource` 라서 스스로 통보받습니다.
     * @note `MaterialCache` 와 구조가 같습니다. **갈라지는 부분은 그쪽 헤더에 적어 두었습니다.** 두 벌이
     *       어긋나 있던 적이 있으므로, 다음에 한쪽을 고칠 때 다른 쪽도 같이 보십시오.
     * @note **렌더 텍스처**: `rendertarget/` 로 시작하는 경로는 파일이 아니라 렌더 타깃입니다(`Texture2D::createRenderTarget`). 카메라가 그 경로로
     *       그리고(`CameraRenderOutput`), 머티리얼은 DDS 와 같은 경로(`acquire`)로 빌려 읽습니다. 크기는 처음 만들 때 정해집니다. 카메라가
     *       `declareRenderTarget` 으로 먼저 알리면 그 크기, 아니면 `kDefaultRenderTargetSize` 입니다.
     */
    class SW_API TextureCache final : public IAssetCache
    {
    public:
        /** @brief 빈 캐시로 만듭니다. */
        TextureCache();
        /** @brief 캐시를 해제합니다. */
        ~TextureCache() override;
        TextureCache( const TextureCache& )            = delete;
        TextureCache& operator=( const TextureCache& ) = delete;

        /** @brief 레지스트리의 종류 이름입니다. `AssetManager::getAssetCache<T>()` 가 이것으로 찾습니다. */
        static constexpr const utf8* kAssetKindName = "Texture";
        /** @brief 이 캐시가 다루는 에셋 종류의 이름입니다. */
        const utf8* getAssetKindName() const override { return kAssetKindName; }
        /// @brief 렌더 텍스처 경로의 접두사입니다.
        static constexpr const utf8* kRenderTargetPrefix = "rendertarget/";
        /// @brief 크기를 알리지 않은 렌더 텍스처의 한 변(px)입니다.
        static constexpr uint32 kDefaultRenderTargetSize = 256;
        /// @brief CPU 텍스처 경로의 접두사입니다(`createCPUTexture`).
        static constexpr const utf8* kCPUTexturePrefix = "cpu/";

        /**
         * @brief 경로의 텍스처 로드를 요청합니다. 이미 올라가 있으면 `Ready` 핸들, 같은 경로를 읽는 중이면 그 로드의 핸들, 아니면 새 로드입니다.
         * @details 로드는 두 단계다. DDS 읽기와 확인은 워커에서(`Texture2D::readImage`), GPU 업로드는 디바이스를 쓰는 스레드에서 한다 — DX11 은 즉시 컨텍스트로,
         *          GL 은 스레드에 묶인 컨텍스트로 올리므로 워커에서 올릴 수 없다. 업로드는 렌더 스레드가 프레임마다 `completePendingLoads` 로 하거나, 그 스레드에서
         *          `wait()` 한 쪽이 그 자리에서 한다. 렌더 텍스처 · CPU 텍스처는 읽을 파일이 없어 업로드 단계만 있다.
         *          **수명 계약**: 핸들이 준비된 텍스처의 참조 하나를 쥐고, 마지막 핸들이 사라질 때 놓는다. 업로드가 끝나면 `getReloadGeneration` 이 올라
         *          로드 중 텍스처를 빌려 둔 머티리얼이 새 SRV 를 받는다(`Material::refreshTextureBindings`).
         *          같은 경로를 읽는 중인 로드를 받으면 그 로드가 올릴 디바이스를 @p pDevice 로 바꾼다 — 앞서 요청한 디바이스는 이미 내려갔을 수 있다.
         * @param start 새 로드의 로드 단계(디스크 읽기)를 어디서 시작하는가입니다. `OnCallingThread` 여도 업로드는 `wait()` 나 펌프가 한다.
         */
        TextureHandle request( string_view relativePath, IRHIDevice* pDevice, AssetLoadStart start );
        /**
         * @brief 경로의 텍스처를 확보하고 GPU 에 업로드합니다(렌더 텍스처 경로면 렌더 타깃을 만듭니다). 실패하면 nullptr 입니다.
         * @details `request` 와 같은 로드 블록을 기다린다(새 로드는 이 자리에서 읽는다) — 디바이스를 쓰는 스레드에서만 부른다(업로드를 그 자리에서 한다).
         *          **수명 계약**: 성공하면 참조를 하나 셉니다. 돌려준 포인터는 짝이 되는 `release` 까지 살아 있습니다(객체는 `unique_ptr` 이라
         *          맵이 원소를 옮겨도 주소가 그대로입니다). 같은 경로를 동시에 원한 스레드는 같은 로드를 기다려 받습니다.
         */
        Texture2D* acquire( string_view relativePath, IRHIDevice* pDevice );
        /**
         * @brief 워커가 읽기를 마친 텍스처를 @p pDevice 에 올립니다(렌더 스레드, 프레임 기록 전, `FrameRenderer::executePacket`). 끝낸(bindless 에 등록해 공개한) 수를 반환합니다.
         * @details 업로드는 세 단계다(RHIAsyncUploads U8) — 워커 읽기 → 이 함수가 `beginUpload`(텍스처 생성 · 제출, 기다리지 않는다) → 이후 프레임의 이 함수가 `finishUpload`
         *          (`pollTransfer` 가 `Complete` 면 bindless 등록과 공개). 같은 호출이 이미 제출한 로드의 끝남 확인을 먼저 하고, @p maxCount 는 **새로 제출하는 수**에 적용한다
         *          (확인은 값싸다). `Busy` 면 그 프레임의 나머지 제출을 미루고 다음 프레임에 다시 낸다. 블로킹이 아닌 길이라 `wait()` 한 쪽이 있으면 그쪽이 먼저 끝낸다.
         *          한 프레임에 @p maxCount 개까지만 제출한다 — 한꺼번에 도착한 수백 장이 한 프레임을 늘리지 않게(언리얼 텍스처 스트리밍의 프레임당 예산과 같다).
         *          요청한 디바이스가 아니라 지금 올리는 디바이스로 올린다 — 요청 뒤 디바이스가 내려가고(백엔드 교체 · 테스트의 디바이스 차례) 새로 섰으면 옛 주소는 죽었다.
         */
        uint32 completePendingLoads( IRHIDevice* pDevice, uint32 maxCount );
        /** @brief 렌더 스레드가 한 프레임에 올리는 비동기 로드 수의 상한입니다(`completePendingLoads`). */
        static constexpr uint32 kMaxUploadsPerFrame = 8;
        /**
         * @brief 아래쪽 밉만 올린 텍스처(비동기 로드, `TextureResidency::StreamingBase`)의 위쪽 밉을 업로드합니다(렌더 스레드, 기록 전, `completePendingLoads` 뒤). 바꿔 끼운 수를 반환합니다.
         * @details 텍스처 밉 스트리밍(계획 PreloadAndLOD P6)이다 — 첫 화면은 작은 밉으로 빨리 나오고 위쪽 밉은 한 프레임에 @p maxCount 개씩 들어온다(먼저 읽힌 것부터).
         *          새 GPU 텍스처 · SRV 로 바꿔 끼우고 리로드 세대를 올려 머티리얼이 새 번호를 받게 한다. 옛 자원은 게임 스레드가 해제한다(`releaseRetiredTextures`).
         *          그 사이 놓인 텍스처 · 다른 디바이스의 텍스처는 건너뛴다(다시 업로드할 때 밉 전부가 올라간다).
         */
        uint32 updateStreaming( IRHIDevice* pDevice, uint32 maxCount );
        /**
         * @brief 이번 프레임 화면에서 텍스처 @p relativePath 가 차지하는 크기를 알립니다(게임 스레드). 값은 가장 큰 뷰의 화면 크기(화면 높이에 대한 비율)이고 클수록 위쪽 밉을 먼저 올린다.
         * @details 텍스처 밉 요청 우선순위다(계획 PreloadAndLOD P10 — 언리얼 텍스처 스트리머의 wanted mip · 보이는 것부터). 같은 프레임에 여러 번 부르면 큰 값을 쓴다.
         *          `updateStreaming` 이 위쪽 밉을 기다리는 줄을 이 값의 내림차순(알리지 않은 것은 0, 같으면 먼저 읽힌 순)으로 정렬해 쓰고 알림을 비운다 — 한 프레임 안의 값이다.
         */
        void noteScreenSize( string_view relativePath, float32 screenSize );
        /** @brief 위쪽 밉을 기다리는 텍스처가 있으면 true 입니다(게임 스레드가 화면 크기를 알릴 필요가 있는지 싸게 묻는다). */
        bool hasPendingStreamingUpgrades() const;
        /** @brief 렌더 스레드가 한 프레임에 업로드하는 위쪽 밉 수의 상한입니다(`updateStreaming`). 텍스처 하나가 밉 사슬 전부라 처음 로드보다 무겁다. */
        static constexpr uint32 kMaxStreamingUpgradesPerFrame = 2;
        /**
         * @brief 스트리밍이 바꿔 끼운 옛 GPU 자원을 해제합니다(게임 스레드, 프레임마다 — `Engine`). `releaseHandle` 이 이미 만든 패킷이 끝날 때까지 미룬다.
         * @details 렌더 스레드에서 바로 해제하면 옛 SRV 번호를 값으로 가진(게임 스레드가 이미 만든) 패킷이 끝나기 전에 그 번호가 다른 텍스처에 다시 쓰인다.
         */
        void releaseRetiredTextures( IRHIDevice* pDevice );
        /**
         * @brief 읽거나 올리는 중인 텍스처 수입니다(어느 스레드든). 위쪽 밉을 기다리는 텍스처도 셉니다.
         * @details 시나리오의 스크린샷이 로드 중 텍스처 · 아직 작은 밉을 찍지 않게 기다릴 때 씁니다(`AutomationRunner`).
         */
        uint32 countLoadsInFlight() const;
        /** @brief 렌더 텍스처 경로(`rendertarget/...`)이면 true 입니다. */
        static bool isRenderTargetPath( string_view relativePath );
        /** @brief CPU 텍스처 경로(`cpu/...`)이면 true 입니다. */
        static bool isCPUTexturePath( string_view relativePath );
        /**
         * @brief CPU 픽셀(RGBA8 밉 하나, 행 사이에 빈틈 없음)을 원본으로 하는 텍스처를 @p relativePath(`cpu/...`)에 만듭니다(게임 스레드). GPU 자원은 첫 `acquire` 가 업로드합니다.
         * @details 언리얼 랜드스케이프 가중치 맵이나 유니티 `TerrainData` 알파맵처럼 **한 소유자만 쓰는** 텍스처입니다(지형마다의 스플랫). 원본은 캐시가 보관하고
         *          `requestRegionUpdate` 가 고치므로 디바이스를 다시 만들어도 칠한 내용이 남습니다. 항목은 `releaseCPUTexture` 까지 남습니다(참조가 0 이면 GPU 자원만 해제합니다).
         * @return 경로가 `cpu/` 가 아니거나, 이미 있거나, 크기가 틀리면 false 입니다.
         */
        [[nodiscard]] bool createCPUTexture( string_view relativePath, uint32 width, uint32 height, vector<uint8> rgbaBytes );
        /** @brief `createCPUTexture` 로 만든 항목을 놓습니다(게임 스레드). 빌린 머티리얼이 남아 있으면 그쪽의 마지막 `release` 가 지웁니다. */
        void releaseCPUTexture( string_view relativePath );
        /**
         * @brief 렌더 텍스처의 크기를 알립니다(게임 스레드, 카메라가 붙을 때). 이미 만들어진 것은 바꾸지 않습니다(다음에 새로 만들 때부터).
         * @details 머티리얼이 카메라보다 먼저 빌려도 같은 크기로 만들어지게 하는 함수입니다. 렌더러는 항상 텍스처의 실제 크기로 그립니다.
         */
        void declareRenderTarget( string_view relativePath, uint32 width, uint32 height );
        /**
         * @brief 이미 만든 렌더 텍스처를 새 크기로 다시 만듭니다(렌더 스레드, 기록 전에). 크기가 같거나 아직 없으면 크기만 알리고 false 입니다.
         * @details 언리얼 `UTextureRenderTarget2D::ResizeTarget` 에 해당합니다. 위젯이 그리기 크기를 바꾼 캔버스 대상처럼, 먼저 만든 크기가 낡은 경우에 씁니다.
         *          `Texture2D` 객체는 **그대로 두고** GPU 자원만 바꿉니다(빌린 포인터가 그대로 유효합니다). 새 SRV 인덱스를 받으므로 `reload` 처럼 세대를 증가시켜
         *          머티리얼이 인덱스를 다시 받게 합니다. 옛 자원을 읽는 프레임이 끝나도록 디바이스를 기다리므로 크기가 바뀔 때만 부릅니다.
         * @return 다시 만들었으면 true 입니다. 실패는 로그를 남깁니다.
         */
        bool resizeRenderTarget( string_view relativePath, uint32 width, uint32 height, IRHIDevice* pDevice );
        /**
         * @brief 경로의 텍스처를 디스크에서 다시 읽어 GPU 에 업로드합니다(에디터 핫 리로드).
         * @details 캐시에 없으면 아무 일도 하지 않습니다. 아무도 안 쓰는 것을 업로드할 이유가 없습니다.
         *          `Texture2D` 객체는 **그대로 두고** 내용만 교체합니다. 머티리얼이 포인터를
         *          빌려 가 있으므로 객체를 바꾸면 빌린 쪽이 해제된 것을 가리킵니다.
         */
        void reload( const AssetID& assetID, IRHIDevice* pDevice );
        /**
         * @brief 이미 가진 텍스처를 **참조 수를 바꾸지 않고** 찾습니다. 없거나 처음 업로드하는 중이면 nullptr 입니다.
         * @warning 빌린 포인터입니다. 누군가 `acquire` 로 참조를 잡고 있는 동안만 유효하고, 마지막 `release` 나 `clear` 뒤에는 해제된 객체입니다.
         *          저장하지 말고 그 자리에서 쓰십시오. 오래 보관해야 하면 `acquire` 와 `release` 짝으로 빌립니다.
         */
        const Texture2D* find( string_view relativePath ) const;
        /**
         * @brief 텍스처의 SRV 가 바뀌었을 수 있는 횟수입니다(`reload` · `resizeRenderTarget` 이 다시 올렸거나, 비동기 로드의 업로드가 끝났다).
         * @details 다시 업로드하면 같은 `Texture2D` 에 **새 SRV 인덱스**가 붙고, 비동기 로드가 끝나면 로드 중 텍스처 대신 읽을 SRV 가 생깁니다. 머티리얼은 resolve 때
         *          받은 인덱스를 보관하므로, 이 값이 바뀌면 다시 받습니다(`Material::refreshTextureBindings`, `GPUSceneBuilder` 가 부릅니다).
         */
        uint32 getReloadGeneration() const;
        /**
         * @brief 이미 가진 텍스처의 사각형 구간을 새 픽셀로 바꿔 달라고 맡깁니다(게임 스레드, 지형 스플랫 칠하기). 실제 업로드는 렌더 스레드의 `flushRegionUpdates` 가 합니다.
         * @details @p rgbaBytes 는 RGBA8 순서의 빈틈없는 행(`width × 4` 바이트) × @p height 입니다. 텍스처가 BGRA8 이면 렌더 스레드가 채널을 바꿔 업로드하고,
         *          다른 포맷이거나 그때 캐시에 없으면 버립니다(경고). 밉 0 만 업로드하므로 밉이 없는 텍스처(지형 스플랫 같은 CPU 텍스처)용입니다.
         *          업로드는 호출당 비용이 들므로 부르는 쪽이 한 프레임의 변경을 사각형 하나로 모아 한 번 부릅니다. 디스크의 DDS 는 바꾸지 않으므로
         *          디바이스를 다시 만들거나(`initRHI`) `reload` 하면 디스크 내용으로 돌아갑니다. 내용이 남아야 하면 CPU 텍스처(`createCPUTexture`)에 씁니다.
         *          CPU 텍스처는 GPU 에 없어도 원본을 고칩니다.
         */
        void requestRegionUpdate( string_view relativePath, uint32 x, uint32 y, uint32 width, uint32 height, vector<uint8> rgbaBytes );
        /**
         * @brief 맡겨 둔 구간 업로드를 냅니다(렌더 스레드, 프레임 기록 전, `FrameRenderer::executePacket`). GPU 완료를 기다리지 않습니다.
         * @details @p pDevice 가 nullptr 이면 CPU 텍스처의 원본만 고치고 나머지는 버립니다(GPU 없는 테스트).
         *          RHI 가 `Busy` 를 돌려주면 그 구간부터 이번 차례의 나머지를 다음 프레임으로 미루고(순서 유지), `Rejected` 는 오류를 남기고 버립니다.
         * @return 반영한 구간 수입니다(원본만 고친 것과 받아들여진 제출).
         */
        uint32 flushRegionUpdates( IRHIDevice* pDevice );
        /** @brief 참조를 하나 놓습니다. 0 이 되면 GPU 자원까지 해제합니다. */
        void release( string_view relativePath, IRHIDevice* pDevice );
        /** @brief 그 경로를 지금 캐시가 보관하고 있는지 반환합니다(`MaterialCache::isCached` 와 같은 뜻). */
        bool isCached( string_view relativePath ) const override;
        /** @brief 지금 보관 중인 항목 수입니다. */
        size_t getCachedCount() const override;
        /** @brief 캐시를 비웁니다(GPU 자원은 이미 해제되어 있어야 합니다). 종료 때만 부르며, 그 뒤 `acquire`, `find` 로 받은 포인터는 모두 무효입니다. */
        void clear() override;

    private:
        friend class TextureLoadBlock; // 로드 블록이 항목 표(`Impl`)에 결과를 적고 참조를 놓는다
        struct Impl;
        shared_ptr<Impl> _impl; ///< 로드 블록이 약한 참조로 가리킨다 — 캐시가 먼저 사라지면 블록은 놓을 것이 없다
    };
} // namespace sw

namespace sw
{
    /**
     * @class TextureLoadBlock
     * @brief 텍스처 로드 하나의 공유 상태입니다. 로드 단계(워커)는 DDS 를 읽고, 완료 단계(디바이스 스레드)는 GPU 에 올려 항목을 공개합니다.
     * @details 준비되면 참조 하나를 쥐고 소멸할 때 놓는다. 로드 단계의 실패(파일 없음 · 포맷)도 완료 단계에서 드러낸다 — 항목 표의 "올리는 중" 표시를
     *          내리는 곳을 완료 단계 하나로 두어, 실패한 로드가 표시를 남기는 길이 없게 한다.
     */
    class SW_API TextureLoadBlock final : public AssetLoadBlock, public std::enable_shared_from_this<TextureLoadBlock>
    {
    public:
        using AssetType = Texture2D;

        /** @brief 항목이 무엇으로 올라가는지입니다. */
        enum class Source : uint8
        {
            File = 0,     ///< DDS 파일
            RenderTarget, ///< `rendertarget/` — 알린 크기의 렌더 타깃
            CPUBytes,     ///< `cpu/` — 캐시가 보관한 CPU 원본
        };

        TextureLoadBlock( string_view key, IRHIDevice* pDevice, Source source, Texture2D* pTexture, weak_ptr<TextureCache::Impl> pCache );
        /** @brief 쥔 참조를 놓습니다(`TextureCache::release` 와 같은 길). */
        ~TextureLoadBlock() override;

        /** @brief 준비된 텍스처입니다. 블록(곧 참조)을 쥔 채 텍스처를 가리킵니다 — 받은 쪽이 쥐는 동안 참조가 남습니다. */
        shared_ptr<Texture2D> getAsset() const;
        /** @brief 텍스처 객체입니다(`Ready` 가 아니면 쓰지 않는다). 핸들이 살아 있는 동안 유효합니다. */
        Texture2D* getTexture() const { return _pTexture; }

    private:
        [[nodiscard]] bool runLoad( string& outError ) override;
        bool               hasCompletion() const override { return true; }
        /** @brief 완료 1단계 — 텍스처를 만들고 업로드를 제출합니다(`Texture2D::beginUploadFromImage`). 블로킹이면 이어서 끝까지 기다립니다. */
        [[nodiscard]] AssetCompletionStep beginCompletion( bool bBlocking, string& outError ) override;
        /** @brief 완료 2단계 — 업로드가 끝났는지 보고 bindless 에 등록해 항목을 공개합니다(`Texture2D::finishUpload`). */
        [[nodiscard]] AssetCompletionStep pollCompletion( bool bBlocking, string& outError ) override;
        void                              onAwaitingCompletion() override;
        void                              onUploadSubmitted() override;
        /** @brief 두 대기 목록에서 이 블록을 뺍니다(다른 스레드가 먼저 집었거나 끝났을 때). */
        void removeFromPumpLists( TextureCache::Impl& cache );
        /** @brief 업로드가 끝난(또는 실패한) 뒤의 마무리입니다 — 항목을 공개하고 결과를 단계 결과로 바꿉니다. */
        [[nodiscard]] AssetCompletionStep concludeCompletion( TextureCache::Impl& cache, bool bReady, string& outError );

    private:
        friend class TextureCache;               // 캐시 적중은 단계를 거치지 않고 참조를 쥔 `Ready` 로 만든다 · 렌더 타깃 크기를 적는다
        unique_ptr<DDSImageData>     _pImage;    ///< 로드 단계가 읽은 그림(파일만). 완료 단계가 올리고 버린다
        string                       _loadError; ///< 로드 단계의 실패 이유 — 완료 단계가 드러낸다
        weak_ptr<TextureCache::Impl> _pCache;
        atomic<IRHIDevice*>          _pDevice;  ///< 올릴 디바이스 — 마지막으로 요청한 쪽 · 펌프가 자기 것으로 바꾼다(처음 요청한 디바이스는 이미 내려갔을 수 있다)
        Texture2D*                   _pTexture; ///< 항목의 텍스처 객체(항목이 소유한다)
        uint32                       _renderTargetWidth;
        uint32                       _renderTargetHeight;
        Source                       _source;
        TextureResidency             _residency;       ///< 파일을 업로드할 밉 범위 — 비동기 로드는 아래쪽 밉부터(`TextureCache::request`)
        bool                         _bHoldsReference; ///< 항목의 참조 하나를 쥐었는가(완료에 성공했거나 캐시 적중)
    };
} // namespace sw
