/**
 * @file OpenGLVertexAttribShadow.h
 * @brief GL 메시 VAO 의 정점 속성 상태 사본입니다 — 드로우가 바뀐 속성만 다시 걸게 합니다(GL 호출 없음, 단위 테스트 가능).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"

namespace sw
{
    /**
     * @struct OpenGLVertexAttribShadow
     * @brief 메시 VAO 에 마지막으로 건 속성마다의 (버퍼 이름 · 원소 크기 · 시작 바이트 · 형식 · 인스턴스 여부 · 켜짐) 사본입니다.
     * @details 예전에는 드로우마다 속성 여섯 개를 모두 glVertexAttribPointer 로 다시 걸었다. 언리얼 OpenGLDrv 는 VAO 하나에 속성 사본을 두고
     *          다른 칸만 다시 건다(SetupVertexArrays) — 같은 방식을 고른다. VAO 를 (형식 × 버퍼) 열쇠로 늘리는 캐시나 ARB_vertex_attrib_binding 은
     *          GL 3.3 폴백(WSLg · Mesa)에서 못 쓰거나 버퍼 수만큼 VAO 가 늘어난다. 칸은 공용 속성 표의 순번이다(두 형식의 표가 순번 · location 이 같다).
     *          버퍼를 지우면 그 이름을 잊어야 한다(`invalidateBuffer`) — 묶이지 않은 VAO 는 지운 버퍼를 계속 붙들고, 같은 이름이 새 버퍼에 다시 나오면
     *          사본만 같아 보여 옛 버퍼로 그린다.
     */
    struct OpenGLVertexAttribShadow
    {
        /** @brief 속성 한 칸에 건 값입니다. 꺼진 칸은 켜짐만 본다(`makeDisabled`). */
        struct Entry
        {
            uint32                   _buffer{ 0 };
            uint32                   _stride{ 0 };
            uint32                   _offset{ 0 }; ///< 슬롯 시작 + 속성 오프셋(바이트)
            RHIVertexAttributeFormat _format{ RHIVertexAttributeFormat::Float3 };
            uint8                    _bPerInstance{ SW_FALSE };
            uint8                    _bEnabled{ SW_FALSE };

            bool operator==( const Entry& other ) const
            {
                return _buffer == other._buffer && _stride == other._stride && _offset == other._offset && _format == other._format && _bPerInstance == other._bPerInstance &&
                       _bEnabled == other._bEnabled;
            }
            bool operator!=( const Entry& other ) const { return ( *this == other ) == false; }

            static Entry makeDisabled() { return Entry{}; }
        };

        /**
         * @brief 칸 @p index 를 @p desired 로 둡니다.
         * @return 사본과 달라 GL 에 다시 걸어야 하면 true. 범위 밖 칸은 늘 true(사본에 담지 않는다).
         */
        [[nodiscard]] bool update( uint32 index, const Entry& desired )
        {
            if ( index >= constant::render::kVertexAttributeCount )
                return true;
            if ( _arrKnown[index] != SW_FALSE && _arrEntry[index] == desired )
                return false;
            _arrEntry[index] = desired;
            _arrKnown[index] = SW_TRUE;
            return true;
        }

        /** @brief GL 버퍼 이름 @p glBuffer 를 건 칸을 모른다고 둡니다(버퍼를 지울 때 — 이름이 다시 쓰이면 다시 건다). */
        void invalidateBuffer( uint32 glBuffer )
        {
            for ( uint32 index = 0; index < constant::render::kVertexAttributeCount; ++index )
            {
                if ( _arrEntry[index]._buffer == glBuffer )
                    _arrKnown[index] = SW_FALSE;
            }
        }

        /** @brief 모든 칸을 모른다고 둡니다(VAO 를 새로 만들거나 지울 때). */
        void reset()
        {
            for ( uint32 index = 0; index < constant::render::kVertexAttributeCount; ++index )
                _arrKnown[index] = SW_FALSE;
        }

    private:
        Entry _arrEntry[constant::render::kVertexAttributeCount]{};
        uint8 _arrKnown[constant::render::kVertexAttributeCount]{};
    };
} // namespace sw
