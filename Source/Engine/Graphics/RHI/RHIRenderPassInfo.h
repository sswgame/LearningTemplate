/**
 * @file RHIRenderPassInfo.h
 * @brief 렌더 패스를 여는 인자(첨부 · 로드 · 저장 동작)입니다.
 */
#pragma once
#include "Core/Math/VectorMath.h"

#include "Engine/Graphics/RHI/RHIEnums.h"
#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    class IRHISwapChain;

    /** @brief 렌더 타깃 기본 클리어 색(RGBA)입니다. */
    inline constexpr float4 kDefaultClearColor = { 0.1f, 0.1f, 0.1f, 1.0f };

    /** @brief 첨부 하나를 만들고 여는 데 쓰는 값들입니다(`TransientAttachmentPool`). 파이프라인 XML 의 선언에서 온다(`RenderPassAttachment::getOps`). */
    struct RHIAttachmentOps
    {
        float4               _clearColor{};
        float32              _clearDepth{ 1.0f };
        uint8                _clearStencil{ 0 };
        RHIRenderPassLoadOp  _loadOp{ RHIRenderPassLoadOp::Clear };
        RHIRenderPassStoreOp _storeOp{ RHIRenderPassStoreOp::Store };
    };

    /**
     * @struct RHIRenderPassInfo
     * @brief 백엔드가 읽는 렌더 패스 값입니다. 쓰는 쪽은 `RHIRenderPassBeginInfo` 의 만들기 함수뿐이다(공개 기록 면은 이 형식을 받지 않는다).
     */
    struct SW_API RHIRenderPassInfo
    {
        IRHISwapChain*         _pSwapChain;                                ///< 컬러 타깃 `kRHIBackBufferTexture` 가 가리킬 백버퍼의 스왑체인. nullptr 이면 메인 창이다 (8 bytes)
        RHITextureHandle       _arrColorTarget[kMaxColorAttachments];      ///< RT별 핸들. 백버퍼는 `kRHIBackBufferTexture`, 0 은 오류(패스를 열지 않는다) (32 bytes)
        RHITextureHandle       _depthTarget;                               ///< 깊이/스텐실 핸들. 0 이면 깊이 없음 (8 bytes)
        float4                 _arrClearColor[kMaxColorAttachments];       ///< RT별 클리어 색상 (64 bytes)
        uint32                 _colorTargetCount;                          ///< 컬러 RT 개수 (4 bytes)
        uint32                 _width;                                     ///< 렌더 영역 너비 (4 bytes)
        uint32                 _height;                                    ///< 렌더 영역 높이 (4 bytes)
        float32                _clearDepth;                                ///< 깊이 클리어 값 (4 bytes)
        RHIRenderPassLoadOp    _arrLoadOp[kMaxColorAttachments];           ///< RT별 로드 동작 (16 bytes)
        RHIRenderPassLoadOp    _depthLoadOp;                               ///< 깊이 로드 동작 (4 bytes)
        uint16                 _arrColorTargetSlice[kMaxColorAttachments]; ///< RT별로 그릴 면(배열 원소, 큐브 면). 2D 는 0 (8 bytes)
        uint16                 _depthTargetSlice;                          ///< 깊이 타깃의 그릴 면 (2 bytes)
        uint8                  _arrColorTargetMip[kMaxColorAttachments];   ///< RT별로 그릴 밉(다운샘플 체인 — R-10). 0 이 가장 큰 밉 (4 bytes)
        uint8                  _depthTargetMip;                            ///< 깊이 타깃의 그릴 밉 (1 byte)
        uint8                  _clearStencil;                              ///< 스텐실 클리어 값. 깊이 로드가 Clear 일 때 함께 쓴다 (1 byte)
        uint8                  _bBindColor    : 1;                         ///< 컬러 바인딩 여부 — 만들기 함수가 컬러 수 > 0 으로 정한다 (1 byte)
        [[maybe_unused]] uint8 _reservedFlags : 7;
        RHIRenderPassStoreOp   _arrStoreOp[kMaxColorAttachments]; ///< RT별 저장 동작 (16 bytes)
        RHIRenderPassStoreOp   _depthStoreOp;                     ///< 깊이 · 스텐실 저장 동작 (4 bytes)

        /** @brief 기본값(타깃 없음, 로드 동작 Clear, 저장 동작 Store, 깊이 클리어 1.0)으로 만듭니다. */
        RHIRenderPassInfo() noexcept;
    };

    /** @brief 렌더 패스의 컬러 첨부 하나입니다(`RHIRenderPassBeginInfo::colors`). */
    struct RHIRenderPassColorTarget
    {
        RHITextureHandle     _texture{ 0 };                           ///< 그릴 텍스처. 백버퍼는 `kRHIBackBufferTexture`
        RHIRenderPassLoadOp  _loadOp{ RHIRenderPassLoadOp::Clear };   ///< 로드 동작
        RHIRenderPassStoreOp _storeOp{ RHIRenderPassStoreOp::Store }; ///< 저장 동작
        float4               _clearColor{ kDefaultClearColor };       ///< 로드가 Clear 일 때의 색
    };

    /**
     * @class RHIRenderPassBeginInfo
     * @brief 렌더 패스를 시작할 때 넘기는 인자입니다. 만들기 함수로만 생긴다 — 컬러 수 · 컬러 걸기 · 첨부 배열을 손으로 맞추는 모양을 쓸 수 없다.
     * @details 언리얼 `FRHIRenderPassInfo` 의 생성자 묶음(컬러 하나 · 컬러 여럿 · 깊이만) 자리다. 손 채우기는 컬러 수를 빠뜨리면 0 번 핸들 하나를 걸고,
     *          깊이만 그리는 패스에 컬러 걸기를 켜 둔 채 넘기는 일이 22 곳에 흩어져 있었다. 필드는 비공개이고 백엔드는 `getInfo` 로 읽기만 한다.
     */
    class SW_API RHIRenderPassBeginInfo
    {
    public:
        /** @brief 컬러 타깃 하나로 엽니다. 크기는 타깃 크기다(`setExtent` 로 좁힐 수 있다). */
        [[nodiscard]] static RHIRenderPassBeginInfo color( RHITextureHandle target, RHIRenderPassLoadOp loadOp, const float4& clearColor = kDefaultClearColor );
        /** @brief 컬러 타깃 여럿(MRT)으로 엽니다. @p count 가 `kMaxColorAttachments` 를 넘으면 넘는 몫을 버리고 알린다. */
        [[nodiscard]] static RHIRenderPassBeginInfo colors( const RHIRenderPassColorTarget* pTargets, uint32 count );
        /** @brief 스왑체인 @p pSwapChain 의 백버퍼로 엽니다. nullptr 이면 메인 창이다. */
        [[nodiscard]] static RHIRenderPassBeginInfo backBuffer( IRHISwapChain* pSwapChain, RHIRenderPassLoadOp loadOp, const float4& clearColor = kDefaultClearColor );
        /** @brief 깊이 타깃만으로 엽니다(그림자 · 깊이 프리패스). 컬러는 걸지 않는다. */
        [[nodiscard]] static RHIRenderPassBeginInfo depthOnly( RHITextureHandle depth, RHIRenderPassLoadOp loadOp, RHIRenderPassStoreOp storeOp = RHIRenderPassStoreOp::Store,
                                                               float32 clearDepth = 1.0f, uint8 clearStencil = 0 );

        /** @brief 깊이 타깃을 더합니다. @p depth 가 0 이면 깊이 없이 둔다. */
        RHIRenderPassBeginInfo& setDepth( RHITextureHandle depth, RHIRenderPassLoadOp loadOp, RHIRenderPassStoreOp storeOp = RHIRenderPassStoreOp::Store, float32 clearDepth = 1.0f,
                                          uint8 clearStencil = 0 );
        /** @brief 렌더 영역을 좁힙니다. 0 이면 타깃 크기다(타깃보다 크면 타깃 크기로 자른다 — `IRHICommandContext::computePassExtent`). */
        RHIRenderPassBeginInfo& setExtent( uint32 width, uint32 height );

        [[nodiscard]] const RHIRenderPassInfo& getInfo() const { return _info; }
        [[nodiscard]] uint32                   getWidth() const { return _info._width; }
        [[nodiscard]] uint32                   getHeight() const { return _info._height; }

    private:
        RHIRenderPassBeginInfo() noexcept = default;

    private:
        RHIRenderPassInfo _info;
    };
} // namespace sw
