/**
 * @file RenderTargetRegistry.h
 * @brief 프레임 렌더 타깃(트랜지언트 첨부) 목록을 UI 가 볼 수 있게 공개합니다.
 *
 * [왜 필요한가]
 * 렌더 타깃은 `FrameRenderer` 안의 private 맵이고, 에디터는 그 인스턴스를 쥘 방법이 없습니다.
 * 이 레지스트리 없이 "지금 G버퍼에 뭐가 들어 있나" 를 보려면 `-gv_screenshotAttachment` 로 **프로세스를 다시
 * 띄워** PPM 을 찍어야 하고, 한 장 볼 때마다 앱을 새로 켜야 합니다.
 *
 * [무엇을 지키는가]
 * 목록은 렌더 스레드가 쓰고 UI 스레드가 읽습니다. 그래서 **스냅샷을 복사해 줍니다.** 포인터나 참조를
 * 넘기면 읽는 도중에 트랜지언트가 재생성될 수 있습니다(창 크기가 바뀌면 모두 다시 만들어집니다).
 * 텍스처 핸들 자체는 값이므로 복사해도 안전하고, 죽은 핸들은 RHI 가 generation 으로 거릅니다.
 *
 * [수명]
 * `EngineLoop` 이 소유하고 `engine::getRenderTargetRegistry()` 로 조회합니다. `FrameProfiler` 와 같은
 * 자리입니다. 싱글턴으로 두면 수명이 아무에게도 속하지 않아 종료 순서를 정할 수 없습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @struct RenderTargetInfo
     * @brief 렌더 타깃 하나의 표시용 정보입니다. 값으로만 이루어져 스레드 사이 복사가 안전합니다.
     */
    struct RenderTargetInfo
    {
        string           _name;         ///< 파이프라인 XML 의 첨부 이름(검색 키이기도 함)
        RHITextureHandle _texture{ 0 }; ///< RHI 텍스처 핸들(generation 이 들어 있어 죽은 핸들은 걸러짐)
        uint32           _width{ 0 };
        uint32           _height{ 0 };
        RHIFormat        _format{ RHIFormat::R8G8B8A8_UNORM };
        uint8            _bDepth{ SW_FALSE };     ///< 깊이 첨부면 1. 미리보기가 색으로 안 나옴
        uint8            _bPresented{ SW_FALSE }; ///< 화면에 나가는 첨부면 1(Present 패스의 입력)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RenderTargetReadback
     * @brief 렌더 타깃 하나를 CPU 로 읽은 결과입니다(에디터 렌더 타깃 창의 채널 · 깊이 보기와 픽셀 값).
     * @details 바이트 배치는 `IRHIResourceFactory::readbackTexture2D` 그대로입니다 — 깊이는 `kRHIDepthReadbackFormat`(0..1 float32)입니다.
     */
    struct RenderTargetReadback
    {
        vector<uint8>     _byte;                   ///< 첫 밉 · 첫 면의 픽셀(행은 빈틈없이)
        RHITextureMipSpan _layout{};               ///< 크기와 행 바이트
        RHITextureHandle  _texture{ 0 };           ///< 읽은 텍스처
        uint32            _serial{ 0 };            ///< 요청 번호(`requestReadback` 이 돌려준 값)
        uint8             _bSucceeded{ SW_FALSE }; ///< 읽기에 성공했으면 1
    };
} // namespace sw

namespace sw
{
    /**
     * @class RenderTargetRegistry
     * @brief 이번 프레임 구성의 렌더 타깃 목록을 담아 두고 스냅샷으로 반환합니다.
     * @note 매 프레임 갱신하지 않습니다. 트랜지언트는 **구성이 바뀔 때만** 다시 만들어지므로
     *       그때 한 번 공개하면 됩니다. 세대 번호로 "목록이 바뀌었나" 를 알 수 있습니다.
     */
    class SW_API RenderTargetRegistry
    {
    public:
        RenderTargetRegistry()                                         = default;
        ~RenderTargetRegistry()                                        = default;
        RenderTargetRegistry( const RenderTargetRegistry& )            = delete;
        RenderTargetRegistry& operator=( const RenderTargetRegistry& ) = delete;

        /** @brief 목록을 통째로 갈아 끼웁니다(렌더 스레드). */
        void publish( vector<RenderTargetInfo> listTarget );
        /** @brief 목록을 비웁니다. 트랜지언트를 놓을 때 부릅니다. */
        void clear();

        /** @brief 목록의 사본을 반환합니다(UI 스레드). */
        void snapshot( vector<RenderTargetInfo>& outList ) const;
        /** @brief 목록이 바뀔 때마다 오르는 번호. 값이 같으면 다시 읽을 이유가 없습니다. */
        uint64 getGeneration() const { return _generation.load( std::memory_order_acquire ); }

        // ------------------------------------------------------------------------------
        // CPU 읽기 — UI 가 요청하고 렌더 스레드가 프레임 끝(endFrame 뒤)에 읽어 둔다
        // ------------------------------------------------------------------------------
        /**
         * @brief @p texture 를 CPU 로 읽어 달라고 요청합니다(UI 스레드). 아직 처리하지 않은 요청은 이 요청으로 바뀝니다.
         * @return 요청 번호. 결과의 `_serial` 이 이 값이면 그 요청의 결과입니다.
         */
        uint32 requestReadback( RHITextureHandle texture );
        /** @brief 처리할 요청을 꺼냅니다(렌더 스레드). 없으면 false 입니다. */
        [[nodiscard]] bool takeReadbackRequest( RHITextureHandle& outTexture, uint32& outSerial );
        /** @brief 읽은 결과를 둡니다(렌더 스레드). 이전 결과는 버립니다. */
        void publishReadback( RenderTargetReadback readback );
        /** @brief 마지막 결과의 번호입니다. 0 이면 아직 결과가 없습니다. */
        uint32 getReadbackSerial() const { return _readbackSerial.load( std::memory_order_acquire ); }
        /** @brief 마지막 결과를 복사해 줍니다(UI 스레드). 결과가 없으면 false 입니다. */
        [[nodiscard]] bool copyReadback( RenderTargetReadback& outReadback ) const;

    private:
        mutable mutex            _mutex;
        vector<RenderTargetInfo> _listTarget;
        atomic<uint64>           _generation{ 0 };

        mutable mutex        _readbackMutex;
        RenderTargetReadback _readback;                    ///< 마지막 결과(`_readbackMutex`)
        RHITextureHandle     _pendingReadbackTexture{ 0 }; ///< 처리할 요청(0 이면 없음, `_readbackMutex`)
        uint32               _pendingReadbackSerial{ 0 };  ///< 그 요청의 번호(`_readbackMutex`)
        uint32               _lastRequestSerial{ 0 };      ///< 마지막으로 낸 요청 번호(`_readbackMutex`)
        atomic<uint32>       _readbackSerial{ 0 };         ///< 마지막 결과의 번호
    };
} // namespace sw
