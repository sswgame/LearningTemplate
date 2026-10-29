/**
 * @file CanvasTextureLeases.h
 * @brief 캔버스 렌더러가 `TextureCache` 에서 빌린 텍스처입니다(렌더 스레드). 경로로 고른 이미지와 렌더 텍스처 대상(월드 공간 UI)을 빌리고 돌려줍니다.
 * @details `CanvasRenderer` 가 값으로 소유합니다. 텍스처는 캐시가 소유하고 여기는 빌린 기록만 듭니다(언리얼 Slate 의 리소스 매니저가 브러시 텍스처를 잡는 자리).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    struct CanvasDrawList;
    struct CanvasFrameData;

    class IRHIDevice;
    class RenderPSOCache;
    class Texture2D;

    /** @brief 렌더 텍스처 대상 하나입니다. 빌린 텍스처와 마지막으로 그린 내용 번호, 텍스처를 가집니다(`CanvasRenderer::drawTargets` 가 그린 뒤 고친다). */
    struct CanvasTargetLease
    {
        hashed_string    _path{};
        Texture2D*       _pTexture{ nullptr }; ///< 캐시가 소유하며, 돌려줄 때까지 빌려 쓴다.
        uint64           _drawnRevision{ 0 };  ///< 마지막으로 그린 내용 번호(0이면 그린 적 없음)
        RHITextureHandle _drawnTexture{ 0 };   ///< 그때의 텍스처(다시 만들어졌으면 내용이 없으므로 다시 그린다)
        uint8            _bSeen{ SW_FALSE };   ///< 이번 프레임 목록에 있다.
    };

    /**
     * @class CanvasTextureLeases
     * @brief 캔버스가 빌린 경로 이미지와 렌더 텍스처 대상입니다. 텍스처 생성과 bindless 등록은 기록 중에 할 수 없으므로 빌리기는 기록 전(`CanvasRenderer::prepareFrame`)에만 합니다.
     */
    class SW_API CanvasTextureLeases
    {
    public:
        CanvasTextureLeases();
        ~CanvasTextureLeases();

        CanvasTextureLeases( const CanvasTextureLeases& )            = delete;
        CanvasTextureLeases& operator=( const CanvasTextureLeases& ) = delete;

        /**
         * @brief 이번 목록의 렌더 텍스처 대상을 빌리고(위젯이 그리기 크기를 바꾸면 다시 만듭니다), 사라진 대상은 돌려줍니다.
         * @details 처음 빌린 대상의 포맷에 셋업 PSO(@p psoCache, `FramePSOManager::createOutputPSOVariants`)가 없으면 여기서 오류로 거절한다(대상마다 한 번 — 다음 프레임에는 빌린 상태라 다시 묻지 않는다).
         */
        void prepareTargets( IRHIDevice& device, const CanvasFrameData& frame, const RenderPSOCache& psoCache );
        /** @brief 목록의 경로 이미지를 `TextureCache` 에서 빌립니다(처음 보는 경로만). 엔진 서비스가 없으면(테스트 하네스 밖) 아무것도 하지 않습니다. */
        void acquirePathTextures( IRHIDevice& device, const CanvasDrawList& list );
        /** @brief 경로로 대상 상태를 찾습니다. 이번 목록과 순서가 같지 않으므로 경로로 찾습니다. 없으면 nullptr 입니다. */
        CanvasTargetLease* findTarget( const hashed_string& path );
        /** @brief 경로로 빌린 이미지의 SRV 입니다. 빌리지 않았거나 읽지 못한 경로면 kInvalidDescriptorIndex 입니다. */
        RHIDescriptorIndex findPathTextureSRV( const hashed_string& path ) const;
        /** @brief 빌린 이미지와 대상을 캐시에 돌려줍니다(다음 `prepareTargets` · `acquirePathTextures` 가 다시 빌립니다). */
        void release( IRHIDevice* pDevice );

    private:
        /** @brief 경로로 빌린 이미지 하나입니다(텍스처는 `TextureCache` 가 소유하며 `release` 가 돌려줍니다). */
        struct PathTexture
        {
            hashed_string _path{};
            Texture2D*    _pTexture{ nullptr }; ///< 읽지 못했으면 nullptr(캐시가 이유를 남겼으므로 다시 묻지 않는다)
        };

        vector<PathTexture>       _listPathTexture; ///< 경로 이미지(HUD 아이콘, 조준선. 몇 개뿐이라 선형 검색)
        vector<CanvasTargetLease> _listTarget;      ///< 렌더 텍스처 대상(월드 공간 UI)
    };
} // namespace sw
