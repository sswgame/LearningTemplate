/**
 * @file FrameResourceRegistry.h
 * @brief 패스 스코프 이름 → {텍스처/버퍼, bindless 인덱스} 레지스트리.
 * @details ShaderBindingBinder 가 `g_<Name>Index` 패턴의 CB 멤버를 자동으로 채울 때 사용한다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /// @brief 등록된 텍스처 (핸들 + SRV bindless 인덱스).
    struct RegisteredTexture
    {
        RHITextureHandle   _handle{ 0 };
        RHIDescriptorIndex _srv{ kInvalidDescriptorIndex };
    };

    /// @brief 등록된 버퍼 (핸들 + bindless 인덱스).
    struct RegisteredBuffer
    {
        RHIBufferHandle    _handle{ 0 };
        RHIDescriptorIndex _index{ kInvalidDescriptorIndex };
    };

    /// @brief 엔진이 예약한 프레임 리소스 이름 (registerPassTexture 호출용 상수).
    namespace framres
    {
        inline constexpr const utf8* kSceneColor    = "SceneColor";
        inline constexpr const utf8* kSceneDepth    = "SceneDepth";
        inline constexpr const utf8* kShadowMap     = "ShadowMap";
        inline constexpr const utf8* kGBufferAlbedo = "GBufferAlbedo";
        inline constexpr const utf8* kGBufferNormal = "GBufferNormal";
        inline constexpr const utf8* kHistoryColor  = "HistoryColor";
        inline constexpr const utf8* kSourceColor   = "SourceColor";
        inline constexpr const utf8* kSourceDepth   = "SourceDepth";
    } // namespace framres

    /**
     * @class FrameResourceRegistry
     * @brief 패스 실행 동안만 유효한 이름→리소스 매핑. 패스 시작마다 reset() 한다.
     */
    class SW_API FrameResourceRegistry
    {
    public:
        /** @brief 등록된 텍스처/버퍼를 모두 비운다. */
        void reset();

        /** @brief 텍스처를 이름으로 등록한다. */
        void registerTexture( hashed_string name, RHITextureHandle handle, RHIDescriptorIndex srv );
        /** @brief 버퍼를 이름으로 등록한다. */
        void registerBuffer( hashed_string name, RHIBufferHandle handle, RHIDescriptorIndex index );

        /** @brief 이름으로 텍스처를 찾는다 (없으면 nullptr). */
        const RegisteredTexture* findTexture( hashed_string name ) const;
        /** @brief 이름으로 버퍼를 찾는다 (없으면 nullptr). */
        const RegisteredBuffer* findBuffer( hashed_string name ) const;

    private:
        unordered_map<hashed_string, RegisteredTexture> _mapTexture;
        unordered_map<hashed_string, RegisteredBuffer>  _mapBuffer;
    };
} // namespace sw
