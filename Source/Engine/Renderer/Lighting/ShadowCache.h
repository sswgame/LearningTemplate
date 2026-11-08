/**
 * @file ShadowCache.h
 * @brief 서 있는 점광 · 스폿의 그림자 캐시입니다. 정적 메시의 깊이를 캐시 층(아틀라스 크기의 깊이 텍스처)에 한 번 그려 두고 프레임마다 다시 씁니다.
 * @details 언리얼의 캐시한 그림자 맵(`r.Shadow.CacheWholeSceneShadows` — 정적 프리미티브의 깊이를 캐시하고 움직이는 것만 매 프레임 그 위에 그린다)과 같은 자리다.
 *          방향광 캐스케이드는 캐시하지 않는다 — 카메라에 맞춘 볼륨이 카메라와 함께 움직여 텍셀 자리가 매 프레임 바뀐다(언리얼도 CSM 은 캐시하지 않고 가상 그림자 맵의 페이지로 캐시한다).
 *          무엇을 다시 그릴지는 데이터 규칙 하나(`update`)가 정하고 GPU 없이 테스트한다. 그리는 쪽은 `FramePassExecutor::drawShadowTiles` 다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Renderer/Frame/Resource/TransientAttachmentPool.h"
#include "Engine/Renderer/Lighting/ShadowSetup.h"

namespace sw
{
    class IRHIDevice;

    /** @brief 점광 · 스폿 타일 하나를 이번 프레임에 어떻게 그리는가입니다. */
    enum class ShadowCacheTileAction : uint8
    {
        DrawAll, ///< 캐시하지 않는다 — 모든 메시를 그린다(캐시를 끈 프레임, 이번 프레임에 움직인 빛 · 칸이 바뀐 타일). 지금까지의 그림과 같은 길이다
        Rebuild, ///< 캐시 층을 다시 그린다 — 정적 메시를 캐시 층에 그린 뒤 그 깊이를 옮기고 움직이는 메시를 그 위에 그린다
        Reuse,   ///< 캐시 층의 깊이를 옮기고 움직이는 메시만 그린다
    };

    /**
     * @class ShadowCache
     * @brief 그림자 캐시의 규칙(타일마다 다시 그릴지)과 캐시 층 텍스처를 가집니다. 렌더 스레드가 프레임마다 한 번 `update` 합니다.
     * @details 규칙 — (1) 캐시를 끄면 모두 DrawAll 이고 캐시는 무효다. (2) 타일의 키(뷰-투영 · 아틀라스 칸)가 지난 프레임과 다르면 그 빛이 움직였거나 칸이 바뀐 것이라
     *          이번 프레임은 DrawAll 이고 그 타일의 캐시는 무효다(움직이는 빛은 지금까지처럼 다 그린다). (3) 아틀라스 해상도 · 정적 그림자 세대가 바뀌었거나
     *          무효인 타일이 있으면 캐시 층 전체를 다시 그린다 — 키가 그대로인 모든 타일이 Rebuild 다(층을 통째로 지우므로 칸 하나만 다시 그리지 않는다).
     *          (4) 그 밖에는 Reuse 다. 빛이 멈추면 다음 프레임에 한 번 Rebuild 하고 그 뒤로 Reuse 다.
     */
    class SW_API ShadowCache
    {
    public:
        /** @brief 캐시 층 첨부의 이름입니다(캐시가 가진 풀 안의 이름 — 파이프라인 첨부가 아니다). */
        static constexpr const utf8* kCacheLayerName = "ShadowCacheLayer";

        /** @brief 빈 캐시(모든 타일이 무효)로 만듭니다. */
        ShadowCache();

        /**
         * @brief 이번 프레임의 타일마다 할 일을 정합니다.
         * @param listLocalTile     이번 프레임 점광 · 스폿 타일(`FrameShadowData::_listLocalTile`)
         * @param atlasResolution   그림자 아틀라스 한 변의 텍셀 수
         * @param staticGeneration  정적 그림자 세대(`GPUScene::getStaticShadowGeneration`)
         * @param bEnabled          캐시를 쓰는지(`gv_shadowCache`)
         */
        void update( const vector<ShadowTileDraw>& listLocalTile, uint32 atlasResolution, uint64 staticGeneration, bool bEnabled );

        /** @brief 타일 @p tileIndex 의 이번 프레임 할 일입니다. 범위 밖은 DrawAll 입니다. */
        ShadowCacheTileAction getAction( uint32 tileIndex ) const;
        /** @brief 이번 프레임에 캐시 층을 다시 그려야 하면 true 입니다(Rebuild 타일이 있다). */
        bool needsRebuild() const { return _bNeedsRebuild == SW_TRUE; }
        /** @brief 캐시 층을 쓰는 타일(Rebuild · Reuse)이 있으면 true 입니다. 없으면 그림자 패스는 캐시 없이 지금처럼 그린다. */
        bool usesCacheLayer() const { return _bUsesLayer == SW_TRUE; }
        /** @brief 이번 프레임에 캐시 층을 이미 다시 그렸음을 적습니다. 추가 뷰가 같은 프레임에 다시 그리지 않는다(타일은 모든 뷰가 같다). */
        void markRebuilt() { _bNeedsRebuild = SW_FALSE; }
        /** @brief 캐시를 버립니다(다음 `update` 가 모두 다시 그린다). 장치 교체 · 텍스처를 놓을 때 부른다. */
        void invalidate();

        /** @brief 캐시 층 텍스처를 아틀라스 형식 · 크기로 둡니다(아틀라스 첨부의 값). 다르면 다시 만들고 이번 프레임의 Reuse 를 Rebuild 로 바꾼다. 실패하면 false 이고 그 프레임은 캐시를 쓰지 않는다. */
        [[nodiscard]] bool ensureLayer( IRHIDevice* pDevice, RHIFormat atlasFormat, uint32 atlasResolution );
        /** @brief 캐시 층 텍스처를 놓습니다. */
        void releaseLayer( IRHIDevice* pDevice );
        /** @brief 캐시 층 텍스처와 SRV 입니다. 없으면 텍스처 0 입니다. */
        TransientAttachmentPool::Attachment getLayer() const;

        /** @brief 두 타일의 키(뷰-투영 비트 · 아틀라스 칸)가 같은지입니다. */
        static bool isSameTile( const ShadowTileDraw& first, const ShadowTileDraw& second );

    private:
        vector<ShadowTileDraw>        _listLastTile;
        vector<uint8>                 _listTileValid; ///< 타일마다 캐시 층에 그 타일의 정적 깊이가 들어 있는지
        vector<ShadowCacheTileAction> _listAction;
        TransientAttachmentPool       _layerPool;
        uint64                        _staticGeneration;
        uint32                        _atlasResolution;
        uint32                        _layerResolution;
        RHIFormat                     _layerFormat;
        uint8                         _bNeedsRebuild : 1;
        uint8                         _bUsesLayer    : 1;
        [[maybe_unused]] uint8        _reserved      : 6;
    };
} // namespace sw
