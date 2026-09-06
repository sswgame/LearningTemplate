/**
 * @file PassConstantValues.h
 * @brief 패스 상수(PassCB)를 이름으로 담아 두는 값 저장소
 * @details FrameRenderer 가 프레임/패스 시드를 채우고 ShaderBindingBinder 가 리플렉션 오프셋으로
 *          꺼내 쓴다. 두 쪽이 공유하는 값 타입이라 어느 한쪽 헤더에 두면 반대쪽이 쓰지도 않는
 *          헤더를 통째로 포함하게 된다 — 실제로 FrameRenderer.h 가 이 타입 하나 때문에
 *          ShaderBindingBinder.h 를 포함하고 있었다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class PassConstantValues
     * @brief 이름 기반 값 저장소 (float4x4/float4/float/uint). 대형 미러 struct 대체.
     */
    class SW_API PassConstantValues
    {
    public:
        static constexpr uint32 kMaxValueBytes = 64; ///< float4x4 하나 크기

        /** @brief 모든 값을 비운다. */
        void clear() { _listEntry.clear(); }

        /** @brief float4x4 값을 이름으로 설정(upsert)한다. */
        void setMatrix( hashed_string name, const float4x4& value );
        /** @brief float4 값을 이름으로 설정한다. */
        void setFloat4( hashed_string name, const float4& value );
        /** @brief float32 값을 이름으로 설정한다. */
        void setFloat( hashed_string name, float32 value );
        /** @brief uint 값을 이름으로 설정한다. */
        void setUint( hashed_string name, uint32 value );
        /** @brief 원시 바이트 값을 이름으로 설정한다. */
        void setBytes( hashed_string name, const void* pData, uint32 byteSize );

        /** @brief 이름으로 값을 찾는다 (없으면 nullptr). */
        const uint8* find( hashed_string name, uint32& outSize ) const;

    private:
        struct Entry
        {
            hashed_string                _name;
            uint32                       _size{ 0 };
            array<uint8, kMaxValueBytes> _data{};
        };

        vector<Entry> _listEntry;
    };
} // namespace sw
