/**
 * @file FrameBufferTable.h
 * @brief 그래프 밖에서 사는 자원(이름 · 뷰 칸) → RHI 핸들 · 처음 접근 표입니다. 프레임 그래프의 레벨 콜백이 전이를, 패스 몸통이 핸들을 여기서 찾는다.
 * @details 언리얼 RDG 의 `RegisterExternalBuffer` 가 돌려주는 버퍼 참조, 유니티 RenderGraph `ImportBuffer` 자리다. 주인(HZB · 컬링 인자 · 라이트 그리드 · 입자)이
 *          프레임 시작에 등록하고, 패스는 선언한 접근 핸들로만 핸들을 받는다(`RenderGraphPassContext::getBuffer`). 프레임 패스 하나가 모든 뷰를 도므로 한 이름은
 *          뷰마다 칸(`slot` — 주 시점 0, 추가 뷰 1 부터)을 가진다. 등록하지 않은 이름은 그래프가 전이를 추론해도 아무것도 내지 않는다 — 아직 패스가 손으로 전이하는
 *          자원이다(옮기는 차례는 DesignBacklog "렌더 그래프 접근 핸들 ② ~ ④").
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Renderer/Graph/RenderGraph.h"

namespace sw
{
    struct RHIStructuredBuffer;

    /**
     * @class FrameBufferTable
     * @brief 프레임 그래프 자원의 RHI 핸들 표입니다. 파일 머리말 참고. 렌더 스레드에서만 쓴다(레벨 콜백 · 패스 몸통은 등록이 끝난 뒤 읽기만 한다).
     */
    class SW_API FrameBufferTable
    {
    public:
        /** @brief 등록 하나입니다. 핸들은 `_viewable` 에 따라 버퍼 또는 텍스처 핸들이다(RHI 에서 둘 다 64 비트 번호). */
        struct Entry
        {
            hashed_string           _name;
            uint32                  _slot{ 0 };
            uint64                  _handle{ 0 };
            RenderGraphViewableKind _viewable{ RenderGraphViewableKind::Buffer };
            RenderGraphAccess       _initialAccess{ RenderGraphAccess::ShaderRead }; ///< 프레임 시작이자 끝의 접근 — 그래프가 이 접근을 심고(`RenderGraph::importResource`) 끝에 되돌린다(`forEachChangedAccess`)
            uint32                  _elementSize{ 0 };                               ///< 버퍼 원소 바이트(텍스처는 0)
            uint32                  _elementCount{ 0 };                              ///< 원소 수(버퍼 용량, 텍스처는 0)
        };

        /** @brief 지난 프레임의 등록을 잊습니다(용량은 남긴다). 프레임 그래프를 돌리기 전에 부른다. */
        void reset() { _listEntry.clear(); }

        /**
         * @brief 구조 버퍼 @p buffer 를 크기(원소 바이트 · 용량)와 함께 이름 @p name 의 칸 @p slot 으로 등록합니다 — 핸들만 받는 판은 없다(버퍼 보기 `RenderBufferInspector` 가 늘 읽을 길이를 안다).
         * @details 거절하고 오류를 남기는 것: 핸들 0, 크기 0(원소 바이트 · 용량), 같은 이름 · 칸을 두 번, 같은 이름의 다른 칸이 다른 처음 접근이나 종류(그래프는 이름마다 상태 하나를 든다).
         * @param initialAccess 프레임 시작에 이 버퍼가 있는 상태이자 프레임 그래프가 끝에 되돌려 둘 상태입니다(뷰 그래프 · 다음 프레임이 이 상태를 믿는다 — `forEachChangedAccess`).
         *        그래서 늘 맞다 — 그래프 밖의 다른 사용자(뷰 그래프의 그리기 · 프레임 끝 되읽기)가 바꾸면 쓰고 나서 이 상태로 되돌린다.
         */
        [[nodiscard]] bool registerBuffer( hashed_string name, uint32 slot, const RHIStructuredBuffer& buffer, RenderGraphAccess initialAccess );
        /** @brief 텍스처 @p texture 를 이름 @p name 의 칸 @p slot 으로 등록합니다. 거절 규칙은 `registerBuffer` 와 같다. */
        [[nodiscard]] bool registerTexture( hashed_string name, uint32 slot, RHITextureHandle texture, RenderGraphAccess initialAccess );

        /** @brief 이름 @p name 칸 @p slot 의 버퍼입니다. 없으면 0 입니다. */
        RHIBufferHandle findBuffer( hashed_string name, uint32 slot ) const { return findHandle( name, slot, RenderGraphViewableKind::Buffer ); }
        /** @brief 이름 @p name 칸 @p slot 의 텍스처입니다. 없으면 0 입니다. */
        RHITextureHandle findTexture( hashed_string name, uint32 slot ) const { return findHandle( name, slot, RenderGraphViewableKind::Texture ); }
        /** @brief 등록 전부입니다(등록 순서). 레벨 콜백이 이름의 모든 칸을 한 묶음으로 옮긴다. */
        const vector<Entry>& getEntries() const { return _listEntry; }
        /**
         * @brief 프레임 그래프가 등록 접근(`Entry::_initialAccess`)과 다른 접근에 둔 등록마다 @p visit( entry ) 를 부릅니다 — 실행기가 그 등록을 등록 접근으로 되돌린다.
         * @details 언리얼 RDG 외부 자원의 끝 접근(`SetBufferAccessFinal` · 추출 접근) 자리다. 등록 접근 하나가 프레임 시작이자 끝이라 주인이 따로 적을 끝 상태가 없다 —
         *          뷰 그래프가 읽는 입자 인자(간접 인자)를 프레임 그래프가 셰이더 읽기로 남겨도 형식이 되돌린다. 이번 프레임 그래프가 보지 않은 이름은 부르지 않는다.
         * @param context 프레임 그래프를 돌린 실행 문맥(`RenderGraphExecutionContext::findAccess` — 이름마다 마지막 접근)입니다.
         */
        template <typename Visit>
        void forEachChangedAccess( const RenderGraphExecutionContext& context, Visit&& visit ) const
        {
            for ( const Entry& entry : _listEntry )
            {
                const optional<RenderGraphAccess> lastAccess = context.findAccess( entry._name );
                if ( lastAccess.has_value() && *lastAccess != entry._initialAccess )
                    visit( entry );
            }
        }

    private:
        [[nodiscard]] bool registerEntry( const Entry& entry );
        uint64             findHandle( hashed_string name, uint32 slot, RenderGraphViewableKind viewable ) const;

    private:
        vector<Entry> _listEntry;
    };
} // namespace sw
