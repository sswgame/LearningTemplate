/**
 * @file TransientTextureLeases.h
 * @brief 뷰들이 나눠 쓰는 트랜지언트 텍스처 풀입니다 — 같은 서술의 첨부를 뷰마다 만들지 않고 차례로 그리는 뷰들이 한 텍스처를 쓴다.
 * @details 언리얼 `FRenderTargetPool`(서술로 찾는 렌더 타깃 풀) 자리다. 추가 뷰(CCTV · 분할 화면 · 미니맵)는 한 프레임에 순서대로 기록하고 첨부 내용은 그 뷰의 그래프 안에서만 산다 —
 *          크기 · 형식이 같은 뷰 여덟이 그림자 맵 · G버퍼를 여덟 벌 들 까닭이 없다. 뷰 하나가 같은 텍스처를 두 첨부로 받지는 않는다(뷰 안의 나눔은 앨리어싱 계획이 한다).
 *          프레임을 넘어 남아야 하는 것(장면 깊이 — 다음 프레임의 깊이 기록이 읽는다, TAA 기록)과 밖에서 보는 주 시점의 첨부는 빌리지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHITextureSlot.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class TransientTextureLeases
     * @brief 텍스처와 빌린 쪽들입니다. 마지막으로 빌린 쪽이 돌려주면 텍스처를 놓습니다(백엔드의 해제 큐가 앞 프레임들이 끝난 뒤에 지운다). 렌더 스레드에서만 쓴다.
     */
    class SW_API TransientTextureLeases
    {
    public:
        TransientTextureLeases()                                           = default;
        TransientTextureLeases( const TransientTextureLeases& )            = delete;
        TransientTextureLeases& operator=( const TransientTextureLeases& ) = delete;

        /**
         * @brief @p pHolder 가 @p desc 텍스처 하나를 빌립니다. 같은 서술이고 @p pHolder 가 아직 빌리지 않은 것이 있으면 그것을, 없으면 새로 만듭니다.
         * @return 만들지 못하면 무효 번호(`constant::invalid_index::kUint32`)입니다. 번호는 돌려줄 때까지 바뀌지 않는다.
         */
        [[nodiscard]] uint32 lease( IRHIDevice& device, const RHITextureDesc& desc, const void* pHolder, const utf8* pDebugName );
        /** @brief 빌린 텍스처입니다. */
        const RHITextureSlot& get( uint32 index ) const { return _listEntry[index]._texture; }
        /** @brief 돌려줍니다. 빌린 쪽이 더 없으면 텍스처를 놓는다(디바이스가 없으면 핸들만 잊는다). */
        void release( IRHIDevice* pDevice, uint32 index, const void* pHolder );
        /** @brief 살아 있는 텍스처 수입니다(테스트 · 진단). */
        uint32 getLiveCount() const;

    private:
        struct Entry
        {
            RHITextureDesc      _desc{};
            RHITextureSlot      _texture{};
            vector<const void*> _listHolder; ///< 비면 놓은 칸이다(다시 쓴다)
        };

        /** @brief 같은 텍스처를 만드는 서술인지입니다(클리어 값까지 — 최적 클리어 값이 다르면 다른 자원이다). */
        static bool isSameDesc( const RHITextureDesc& left, const RHITextureDesc& right );

    private:
        vector<Entry> _listEntry;
    };
} // namespace sw
