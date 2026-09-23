#include "pch.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHIResource.h"

#include "Core/Common/EnumUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIBufferSize.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include <glad/glad.h>

namespace sw
{
    SW_LOG_CALLER( "OpenGLRHIResource" );

    namespace
    {
        // S3TC 는 확장이라 glad 헤더에 없다 — 값은 EXT_texture_compression_s3tc 그대로다.
        constexpr GLenum kGlCompressedRgbaS3tcDxt1 = 0x83F1;
        constexpr GLenum kGlCompressedRgbaS3tcDxt3 = 0x83F2;
        constexpr GLenum kGlCompressedRgbaS3tcDxt5 = 0x83F3;
        constexpr GLenum kGlCompressedRgRgtc2      = 0x8DBD;

        /**
         * @brief RHIFormat 하나의 GL 세 값 — internalFormat · (pixel) format · type.
         * @details 예전에는 셋이 각자 switch 였다 — 포맷을 하나 더하면 세 자리를 같이 고쳐야 했고, `R16G16B16A16_FLOAT` 의 type 이
         *          한 자리에서만 틀렸던 적이 있다(아래 주석). 압축 포맷은 internal 만 있다(glCompressedTexImage 경로라 format · type 은
         *          쓰지 않는다). 표에 없는 포맷(`Unknown` = 첨부 없음)은 셋 다 0 이다.
         */
        struct OpenGLFormatRow
        {
            RHIFormat _format;
            GLenum    _internalFormat;
            GLenum    _pixelFormat;
            GLenum    _pixelType;
        };

        constexpr OpenGLFormatRow arrFormatRow[] = {
            {         RHIFormat::BC1_UNORM,     kGlCompressedRgbaS3tcDxt1,                0,                    0},
            {         RHIFormat::BC2_UNORM,     kGlCompressedRgbaS3tcDxt3,                0,                    0},
            {         RHIFormat::BC3_UNORM,     kGlCompressedRgbaS3tcDxt5,                0,                    0},
            {         RHIFormat::BC4_UNORM,       GL_COMPRESSED_RED_RGTC1,                0,                    0},
            {         RHIFormat::BC5_UNORM,          kGlCompressedRgRgtc2,                0,                    0},
            {         RHIFormat::BC7_UNORM, GL_COMPRESSED_RGBA_BPTC_UNORM,                0,                    0},
            {    RHIFormat::R8G8B8A8_UNORM,                      GL_RGBA8,          GL_RGBA,     GL_UNSIGNED_BYTE},
            {    RHIFormat::B8G8R8A8_UNORM,                      GL_RGBA8,          GL_BGRA,     GL_UNSIGNED_BYTE},
            // **half 는 GL_HALF_FLOAT 다.** GL_FLOAT 로 두면 GL 이 픽셀당 16 바이트를 읽고 쓰는데 엔진이 잡아 둔 버퍼는
            // 8 바이트/픽셀이다(`getRhiFormatBlockInfo` 가 정본) — 되읽기가 버퍼를 두 배로 넘겨 써서 **그냥 죽었다**. HDR 첨부를
            // CPU 로 읽는 경로(스크린샷 · 렌더 타깃 패널)가 생기기 전에는 이 포맷을 되읽을 일이 없어 드러나지 않았다.
            {RHIFormat::R16G16B16A16_FLOAT,                    GL_RGBA16F,          GL_RGBA,        GL_HALF_FLOAT},
            { RHIFormat::D24_UNORM_S8_UINT,           GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8},
            {   RHIFormat::R32G32B32_FLOAT,                     GL_RGB32F,           GL_RGB,             GL_FLOAT},
            {      RHIFormat::R32G32_FLOAT,                      GL_RG32F,            GL_RG,             GL_FLOAT},
            {         RHIFormat::R32_FLOAT,                       GL_R32F,           GL_RED,             GL_FLOAT},
        };

        const OpenGLFormatRow* findFormatRow( RHIFormat format )
        {
            for ( const OpenGLFormatRow& row : arrFormatRow )
            {
                if ( row._format == format )
                    return &row;
            }
            return nullptr;
        }

        GLenum toGlInternalFormat( RHIFormat format )
        {
            const OpenGLFormatRow* pRow = findFormatRow( format );
            return pRow != nullptr ? pRow->_internalFormat : 0;
        }

        GLenum toGlFormat( RHIFormat format )
        {
            const OpenGLFormatRow* pRow = findFormatRow( format );
            return pRow != nullptr ? pRow->_pixelFormat : 0;
        }

        GLenum toGlType( RHIFormat format )
        {
            const OpenGLFormatRow* pRow = findFormatRow( format );
            return pRow != nullptr ? pRow->_pixelType : 0;
        }
    } // namespace

    uint32 OpenGLRHIDevice::getGlTextureName( RHITextureHandle texture ) const
    {
        if ( texture == 0 )
            return 0;
        const OpenGLTextureRecord* pRec = resolveTexture( texture );
        return pRec != nullptr ? pRec->_texture : 0;
    }

    RHIBufferHandle OpenGLRHIResource::createConstantBuffer( uint32 size )
    {
        // 머티리얼 상수버퍼는 **게임 스레드**(Material::initialize) 에서 만들어진다. GL 컨텍스트는 렌더 스레드가
        // 프레임 동안만 쥐고 executePacket 끝에 놓으므로(RenderThread), 여기서도 createBuffer 처럼 잠깐 빌려야 한다.
        // 가드 없이는 glGenBuffers 가 조용히 아무것도 안 해 초기화 안 된 이름이 그대로 저장됐고(0xFFFFFFFF),
        // 이후 update 도 무시돼 PS 가 color=0 을 읽어 큐브가 전부 검게 나왔다 — GL 에러도, 로그도 없이.
        ScopedOpenGLContext ctxScope( _pDevice );
        const uint32        alignedSize = MathUtil::align( size, constant::kConstantBufferAlignment );
        GLuint              ubo{ 0 };
        glGenBuffers( 1, &ubo );
        if ( ubo == 0 )
            return 0;
        glBindBuffer( GL_UNIFORM_BUFFER, ubo );
        glBufferData( GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>( alignedSize ), nullptr, GL_DYNAMIC_DRAW );
        glBindBuffer( GL_UNIFORM_BUFFER, 0 );

        return _pDevice->storeGlBuffer( ubo );
    }

    void OpenGLRHIResource::updateConstantBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( buffer == 0 || pData == nullptr )
            return;
        GLuint ubo = _pDevice->resolveGlBuffer( buffer );
        if ( ubo == 0 )
            return;
        ScopedOpenGLContext ctxScope( _pDevice );
        glBindBuffer( GL_UNIFORM_BUFFER, ubo );
        glBufferSubData( GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>( size ), pData );
        glBindBuffer( GL_UNIFORM_BUFFER, 0 );
    }

    RHIBufferHandle OpenGLRHIResource::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || elementSize == 0 || elementCount == 0 )
            return 0;

        // 32비트 API 다 — 담기지 않으면 만들지 않는다(RHIBufferSize 가 세 백엔드의 규칙 하나).
        uint32 totalBytes{ 0 };
        if ( RHIBufferSize::computeStructuredBytes( elementSize, elementCount, totalBytes ) == false )
            return 0;

        RHIBufferDesc desc{};
        desc._elementSize  = elementSize;
        desc._elementCount = elementCount;
        desc._sizeBytes    = static_cast<uint32>( totalBytes );
        desc._usage        = RHIBufferUsage::Structured | RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs;
        return createBuffer( desc );
    }

    void OpenGLRHIResource::updateStructuredBufferRegions( RHIBufferHandle buffer, const void* pBaseSource,
                                                           const RHIBufferCopyRegion* pRegions, uint32 regionCount )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || buffer == 0 || pBaseSource == nullptr || pRegions == nullptr || regionCount == 0 )
            return;

        GLuint ssbo = _pDevice->resolveGlBuffer( buffer );
        if ( ssbo == 0 )
            return;

        // GL 은 제출·배리어가 없어 조각마다 부르는 비용이 거의 없다 — 바인딩만 한 번 하고 돈다.
        ScopedOpenGLContext ctxScope( _pDevice );
        glBindBuffer( GL_SHADER_STORAGE_BUFFER, ssbo );
        const uint8* pBase = static_cast<const uint8*>( pBaseSource );
        for ( uint32 regionIndex = 0; regionIndex < regionCount; ++regionIndex )
        {
            const RHIBufferCopyRegion& region = pRegions[regionIndex];
            if ( region._size == 0 )
                continue;
            glBufferSubData( GL_SHADER_STORAGE_BUFFER, static_cast<GLintptr>( region._dstOffset ),
                             static_cast<GLsizeiptr>( region._size ), pBase + region._srcOffset );
        }
        glBindBuffer( GL_SHADER_STORAGE_BUFFER, 0 );
    }

    RHIBufferHandle OpenGLRHIResource::createBuffer( const RHIBufferDesc& desc )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return 0;

        ScopedOpenGLContext ctxScope( _pDevice );

        if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Vertex ) && desc._pInitialData != nullptr && desc._sizeBytes > 0 )
            return createVertexBuffer( desc._pInitialData, desc._sizeBytes );
        if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Constant ) )
            return createConstantBuffer( desc._sizeBytes > 0 ? desc._sizeBytes : 256u );
        if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::Index ) )
        {
            const uint32 stride = ( desc._elementSize == 2 ) ? 2u : 4u;
            return _pDevice->createIndexBuffer( desc._pInitialData, desc._sizeBytes, stride );
        }

        uint32 sizeBytes = desc._sizeBytes;
        if ( sizeBytes == 0 )
        {
            const uint32 elemSize  = desc._elementSize > 0 ? desc._elementSize : 4u;
            const uint32 elemCount = desc._elementCount > 0 ? desc._elementCount : 1u;
            sizeBytes              = elemSize * elemCount;
        }
        if ( sizeBytes == 0 )
            return 0;

        const uint32 alignedSize = MathUtil::align( sizeBytes, constant::kConstantBufferAlignment );

        // SSBO allocation; same name can bind as GL_DRAW_INDIRECT_BUFFER / DISPATCH_INDIRECT_BUFFER.
        //
        // **할당은 정렬 크기로, 채우기는 실제 크기로 나눈다.** 예전엔 `glBufferData` 에 정렬 크기와
        // 초기 데이터를 함께 넘겼는데, 호출자가 준 버퍼는 `_sizeBytes` 뿐이라 GL 이 그 뒤를 읽었다
        // (정렬이 256 바이트라 최대 255 바이트를 넘겨 읽는다). 읽은 쓰레기가 버퍼 꼬리에 들어갈 뿐
        // 아니라, 호출자 버퍼가 페이지 끝에 걸리면 그대로 죽는다.
        GLuint ssbo{ 0 };
        glGenBuffers( 1, &ssbo );
        glBindBuffer( GL_SHADER_STORAGE_BUFFER, ssbo );
        glBufferData( GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>( alignedSize ), nullptr, GL_DYNAMIC_DRAW );
        if ( desc._pInitialData != nullptr && sizeBytes > 0 )
            glBufferSubData( GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>( sizeBytes ), desc._pInitialData );
        glBindBuffer( GL_SHADER_STORAGE_BUFFER, 0 );

        if ( EnumUtil::hasFlag( desc._usage, RHIBufferUsage::IndirectArgs ) )
        {
            glBindBuffer( GL_DRAW_INDIRECT_BUFFER, ssbo );
            glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
        }

        return _pDevice->storeGlBuffer( ssbo );
    }

    RHIBufferHandle OpenGLRHIResource::createVertexBuffer( const void* pData, uint32 sizeBytes )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || pData == nullptr || sizeBytes == 0 )
            return 0;

        ScopedOpenGLContext ctxScope( _pDevice );
        GLuint              vbo{ 0 };
        glGenBuffers( 1, &vbo );
        glBindBuffer( GL_ARRAY_BUFFER, vbo );
        glBufferData( GL_ARRAY_BUFFER, static_cast<GLsizeiptr>( sizeBytes ), pData, GL_STATIC_DRAW );
        glBindBuffer( GL_ARRAY_BUFFER, 0 );

        return _pDevice->storeGlBuffer( vbo );
    }

    void OpenGLRHIResource::destroyBuffer( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return;

        ScopedOpenGLContext ctxScope( _pDevice );
        if ( buffer == _pDevice->_recordingState._boundMeshVb )
            _pDevice->_recordingState._boundMeshVb = 0;
        if ( buffer == _pDevice->_recordingState._boundIndexBuffer )
            _pDevice->_recordingState._boundIndexBuffer = 0;

        uint32 glName{ 0 };
        if ( _pDevice->_gpuBuffers.take( buffer, glName ) == false )
            return;

        for ( OpenGLRHIDevice::BindlessResourceRecord& rec : _pDevice->_listRegisteredBindless )
        {
            if ( rec._buffer != buffer )
                continue;
            rec._buffer = 0;
        }
        for ( OpenGLRHIDevice::BindlessResourceRecord& rec : _pDevice->_listRegisteredUAV )
        {
            if ( rec._buffer != buffer )
                continue;
            rec._buffer = 0;
        }

        auto releaseCb = [glBuffer = glName]()
        {
            GLuint name = glBuffer;
            glDeleteBuffers( 1, &name );
        };
        _pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
    }

    bool OpenGLRHIResource::uploadTexture2D( RHITextureHandle texture, const RHITextureUploadDesc& desc )
    {
        OpenGLRHIDevice::OpenGLTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == 0 || _pDevice->_bInitialized == SW_FALSE )
            return false;
        if ( pRecord->_bDepthStencil != SW_FALSE )
            return false;

        RHITextureMipSpan arrMip[constant::kMaxTextureMipCount]{};
        const uint32      mipCount = resolveTextureUploadMips( desc, pRecord->_format, pRecord->_width, pRecord->_height,
                                                               pRecord->_mipLevels, arrMip, constant::kMaxTextureMipCount );
        if ( mipCount == 0 )
        {
            SW_LOG_ERROR( "uploadTexture2D: unsupported format or not enough data (%# bytes for %#×%#, %# mips)",
                          desc._sizeBytes, pRecord->_width, pRecord->_height, pRecord->_mipLevels );
            return false;
        }

        ScopedOpenGLContext ctxScope( _pDevice );
        const bool          bCompressed = isRhiFormatBlockCompressed( pRecord->_format );
        const GLenum        glInternal  = toGlInternalFormat( pRecord->_format );
        const GLenum        glFormat    = toGlFormat( pRecord->_format );
        const GLenum        glType      = toGlType( pRecord->_format );

        glBindTexture( GL_TEXTURE_2D, pRecord->_texture );
        // 행이 빈틈없이 이어진 데이터라 기본 4바이트 행 정렬을 끈다(R32G32B32 12바이트 행 같은 경우).
        glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
        for ( uint32 mip = 0; mip < mipCount; ++mip )
        {
            const RHITextureMipSpan& span = arrMip[mip];
            if ( bCompressed )
            {
                glCompressedTexSubImage2D( GL_TEXTURE_2D, static_cast<GLint>( span._mip ), 0, 0,
                                           static_cast<GLsizei>( span._width ), static_cast<GLsizei>( span._height ), glInternal,
                                           static_cast<GLsizei>( span._sizeBytes ), span._pData );
            }
            else
            {
                glTexSubImage2D( GL_TEXTURE_2D, static_cast<GLint>( span._mip ), 0, 0,
                                 static_cast<GLsizei>( span._width ), static_cast<GLsizei>( span._height ), glFormat, glType, span._pData );
            }
        }
        glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
        glBindTexture( GL_TEXTURE_2D, 0 );
        return true;
    }

    RHIFormat OpenGLRHIResource::getTextureFormat( RHITextureHandle texture ) const
    {
        const OpenGLRHIDevice::OpenGLTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        return pRecord != nullptr ? pRecord->_format : RHIFormat::Unknown;
    }

    bool OpenGLRHIResource::readbackTexture2D( RHITextureHandle texture, uint32 mip, vector<uint8>& outBytes, RHITextureMipSpan& outLayout )
    {
        OpenGLRHIDevice::OpenGLTextureRecord* pRecord = _pDevice->resolveTexture( texture );
        if ( pRecord == nullptr || pRecord->_texture == 0 || _pDevice->_bInitialized == SW_FALSE )
            return false;
        if ( pRecord->_bDepthStencil != SW_FALSE || mip >= pRecord->_mipLevels )
            return false;
        if ( computeRhiTextureMipLayout( pRecord->_format, pRecord->_width, pRecord->_height, mip, outLayout ) == false )
            return false;

        ScopedOpenGLContext ctxScope( _pDevice );
        outBytes.assign( outLayout._sizeBytes, 0 );
        glBindTexture( GL_TEXTURE_2D, pRecord->_texture );
        glPixelStorei( GL_PACK_ALIGNMENT, 1 );
        if ( isRhiFormatBlockCompressed( pRecord->_format ) )
            glGetCompressedTexImage( GL_TEXTURE_2D, static_cast<GLint>( mip ), outBytes.data() );
        else
            glGetTexImage( GL_TEXTURE_2D, static_cast<GLint>( mip ), toGlFormat( pRecord->_format ), toGlType( pRecord->_format ), outBytes.data() );
        glPixelStorei( GL_PACK_ALIGNMENT, 4 );
        glBindTexture( GL_TEXTURE_2D, 0 );
        return true;
    }

    RHITextureHandle OpenGLRHIResource::createTexture2D( const RHITextureDesc& desc )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || desc._width == 0 || desc._height == 0 )
            return 0;

        ScopedOpenGLContext ctxScope( _pDevice );
        const uint32        mipLevels   = desc._mipLevels > 0 ? desc._mipLevels : 1;
        const GLenum        internalFmt = toGlInternalFormat( desc._format );
        const bool          bDepth      = desc._bIsDepthStencil || desc._format == RHIFormat::D24_UNORM_S8_UINT;

        GLuint tex{ 0 };
        glGenTextures( 1, &tex );
        glBindTexture( GL_TEXTURE_2D, tex );

        if ( glad_glTexStorage2D != nullptr )
        {
            glTexStorage2D( GL_TEXTURE_2D, static_cast<GLsizei>( mipLevels ), internalFmt,
                            static_cast<GLsizei>( desc._width ), static_cast<GLsizei>( desc._height ) );
        }
        else
        {
            glTexImage2D( GL_TEXTURE_2D, 0, static_cast<GLint>( internalFmt ),
                          static_cast<GLsizei>( desc._width ), static_cast<GLsizei>( desc._height ),
                          0, toGlFormat( desc._format ), toGlType( desc._format ), nullptr );
        }

        if ( bDepth )
        {
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
            glTexParameteri( GL_TEXTURE_2D, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_DEPTH_COMPONENT );
        }
        else
        {
            const GLint minFilter = ( mipLevels > 1 ) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        }
        glBindTexture( GL_TEXTURE_2D, 0 );

        OpenGLRHIDevice::OpenGLTextureRecord record{};
        record._texture        = tex;
        record._width          = desc._width;
        record._height         = desc._height;
        record._mipLevels      = mipLevels;
        record._format         = desc._format;
        record._internalFormat = static_cast<uint32>( internalFmt );
        record._bDepthStencil  = bDepth ? 1 : 0;
        record._bUAV           = desc._bIsUnorderedAccess ? 1 : 0;
        record._reserved       = 0;

        if ( desc._bIsRenderTarget || bDepth )
        {
            GLuint fbo{ 0 };
            glGenFramebuffers( 1, &fbo );
            glBindFramebuffer( GL_FRAMEBUFFER, fbo );
            if ( bDepth )
            {
                const GLenum depthAttachment = ( desc._format == RHIFormat::D24_UNORM_S8_UINT )
                                                 ? GL_DEPTH_STENCIL_ATTACHMENT
                                                 : GL_DEPTH_ATTACHMENT;
                glFramebufferTexture2D( GL_FRAMEBUFFER, depthAttachment, GL_TEXTURE_2D, tex, 0 );
                glDrawBuffer( GL_NONE );
                glReadBuffer( GL_NONE );
            }
            else
                glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0 );
            const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
            glBindFramebuffer( GL_FRAMEBUFFER, 0 );
            if ( status != GL_FRAMEBUFFER_COMPLETE )
            {
                SW_LOG_WARNING( "createTexture2D FBO incomplete (status=%#) — texture kept without FBO.",
                                static_cast<uint32>( status ) );
                glDeleteFramebuffers( 1, &fbo );
            }
            else
                record._fbo = fbo;
        }

        return _pDevice->_gpuTextures.insert( record );
    }

    void OpenGLRHIResource::destroyTexture( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return;

        ScopedOpenGLContext                  ctxScope( _pDevice );
        OpenGLRHIDevice::OpenGLTextureRecord owned;
        if ( _pDevice->_gpuTextures.take( texture, owned ) == false )
            return;

        for ( auto compIt = _pDevice->_mapCompositeFbo.begin(); compIt != _pDevice->_mapCompositeFbo.end(); )
        {
            bool bUsesTexture = ( compIt->first._depth == texture );
            for ( uint32 colorIndex = 0; colorIndex < compIt->first._colorCount && bUsesTexture == false; ++colorIndex )
            {
                bUsesTexture = ( compIt->first._arrColor[colorIndex] == texture );
            }

            if ( bUsesTexture )
            {
                GLuint fbo = compIt->second;
                if ( fbo != 0 )
                    glDeleteFramebuffers( 1, &fbo );
                compIt = _pDevice->_mapCompositeFbo.erase( compIt );
            }
            else
                ++compIt;
        }

        const GLuint fboName = owned._fbo;
        const GLuint texName = owned._texture;

        for ( size_t textureIndex = 0; textureIndex < _pDevice->_listRegisteredTexture.size(); ++textureIndex )
        {
            if ( _pDevice->_listRegisteredTexture[textureIndex]._texture != texture )
                continue;
            releaseFreeListIndex( _pDevice->_listRegisteredTexture, _pDevice->_listTextureFree,
                                  static_cast<uint32>( textureIndex ), OpenGLRHIDevice::BindlessTextureRecord{} );
        }

        auto releaseCb = [fboName, texName]()
        {
            if ( fboName != 0 )
            {
                GLuint name = fboName;
                glDeleteFramebuffers( 1, &name );
            }
            if ( texName != 0 )
            {
                GLuint name = texName;
                glDeleteTextures( 1, &name );
            }
        };
        _pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
    }
} // namespace sw
