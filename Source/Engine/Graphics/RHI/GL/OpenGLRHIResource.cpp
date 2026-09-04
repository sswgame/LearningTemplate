#include "pch.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHIResource.h"

#include "Core/Common/EnumUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/Shader/ShaderCache.h"

#include <glad/glad.h>

namespace sw
{
    namespace
    {
        ShaderCompileResult compileShader( const ShaderCompileDesc& desc )
        {
            if ( engine::areEngineServicesBound() )
                return engine::getShaderCache().getOrCompile( desc );
            return ShaderCompiler::compileHLSL( desc );
        }
    } // namespace

    SW_LOG_CALLER( "OpenGLRHIResource" );

    static GLenum toGlInternalFormat( RHIFormat format )
    {
        switch ( format )
        {
            case RHIFormat::R8G8B8A8_UNORM:
            case RHIFormat::B8G8R8A8_UNORM:
                return GL_RGBA8;
            case RHIFormat::R16G16B16A16_FLOAT:
                return GL_RGBA16F;
            case RHIFormat::D24_UNORM_S8_UINT:
                return GL_DEPTH24_STENCIL8;
            case RHIFormat::R32G32B32_FLOAT:
                return GL_RGB32F;
            case RHIFormat::R32G32_FLOAT:
                return GL_RG32F;
            case RHIFormat::R32_FLOAT:
                return GL_R32F;
            default:
                return 0;
        }
    }

    static GLenum toGlFormat( RHIFormat format )
    {
        switch ( format )
        {
            case RHIFormat::R8G8B8A8_UNORM:
            case RHIFormat::R16G16B16A16_FLOAT:
                return GL_RGBA;
            case RHIFormat::B8G8R8A8_UNORM:
                return GL_BGRA;
            case RHIFormat::D24_UNORM_S8_UINT:
                return GL_DEPTH_STENCIL;
            case RHIFormat::R32G32B32_FLOAT:
                return GL_RGB;
            case RHIFormat::R32G32_FLOAT:
                return GL_RG;
            case RHIFormat::R32_FLOAT:
                return GL_RED;
            default:
                return 0;
        }
    }

    static GLenum toGlType( RHIFormat format )
    {
        switch ( format )
        {
            case RHIFormat::R8G8B8A8_UNORM:
            case RHIFormat::B8G8R8A8_UNORM:
                return GL_UNSIGNED_BYTE;
            case RHIFormat::R16G16B16A16_FLOAT:
            case RHIFormat::R32G32B32_FLOAT:
            case RHIFormat::R32G32_FLOAT:
            case RHIFormat::R32_FLOAT:
                return GL_FLOAT;
            case RHIFormat::D24_UNORM_S8_UINT:
                return GL_UNSIGNED_INT_24_8;
            default:
                return 0;
        }
    }

    uint32 OpenGLRHIDevice::getGLTextureName( RHITextureHandle texture ) const
    {
        if ( texture == 0 )
            return 0;
        const OpenGLTextureRecord* pRec = resolveTexture( texture );
        return pRec != nullptr ? pRec->_texture : 0;
    }

    RHIPipelineStateHandle OpenGLRHIResource::createPipelineState( const RHIPipelineStateDesc& desc )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return 0;

        ScopedOpenGLContext                        ctxScope( _pDevice );
        OpenGLRHIDevice::OpenGLPipelineStateRecord record{};

        auto fillDefines = [&]( ShaderCompileDesc& cd )
        {
            for ( const string& def : desc._listShaderDefine )
            {
                ShaderMacroDefine m{};
                const size_t      eq = def.find( '=' );
                if ( eq == string::npos )
                {
                    m._name  = def;
                    m._value = "1";
                }
                else
                {
                    m._name  = def.substr( 0, eq );
                    m._value = def.substr( eq + 1 );
                }
                cd._listDefine.push_back( std::move( m ) );
            }
        };

        ShaderCompileDesc vsDesc{};
        vsDesc._filePath     = desc._vertexShaderPath;
        vsDesc._entryPoint   = desc._vertexEntryPoint.empty() ? "VSMain" : desc._vertexEntryPoint;
        vsDesc._stage        = ShaderStage::Vertex;
        vsDesc._targetFormat = ShaderTargetFormat::SPIRV_OpenGL;
        fillDefines( vsDesc );
        ShaderCompileResult vsResult = compileShader( vsDesc );

        const bool          bDepthOnly      = ( desc._numRenderTargets == 0 && desc._bEnableDepthTest != 0 );
        const bool          bHasPixelShader = desc._pixelShaderPath.empty() == false && bDepthOnly == false;
        ShaderCompileResult psResult{};
        ShaderCompileDesc   psDesc{};
        if ( bHasPixelShader )
        {
            psDesc._filePath     = desc._pixelShaderPath;
            psDesc._entryPoint   = desc._pixelEntryPoint.empty() ? "PSMain" : desc._pixelEntryPoint;
            psDesc._stage        = ShaderStage::Pixel;
            psDesc._targetFormat = ShaderTargetFormat::SPIRV_OpenGL;
            fillDefines( psDesc );
            psResult = compileShader( psDesc );
        }

        if ( vsResult._bSuccess && ( bHasPixelShader == false || psResult._bSuccess ) )
        {
            if ( glad_glShaderBinary == nullptr || glad_glSpecializeShader == nullptr )
            {
                SW_LOG_ERROR( "GL_ARB_gl_spirv unavailable (glShaderBinary/glSpecializeShader null)" );
                return 0;
            }

            GLuint vs = glCreateShader( GL_VERTEX_SHADER );
            GLuint ps = bHasPixelShader ? glCreateShader( GL_FRAGMENT_SHADER ) : 0;

            glShaderBinary( 1, &vs, GL_SHADER_BINARY_FORMAT_SPIR_V, vsResult._bytecode.data(), static_cast<GLsizei>( vsResult._bytecode.size() ) );
            glSpecializeShader( vs, vsDesc._entryPoint.c_str(), 0, nullptr, nullptr );

            if ( bHasPixelShader )
            {
                glShaderBinary( 1, &ps, GL_SHADER_BINARY_FORMAT_SPIR_V, psResult._bytecode.data(), static_cast<GLsizei>( psResult._bytecode.size() ) );
                glSpecializeShader( ps, psDesc._entryPoint.c_str(), 0, nullptr, nullptr );
            }

            GLint vsCompiled = GL_FALSE;
            GLint psCompiled = GL_FALSE;
            glGetShaderiv( vs, GL_COMPILE_STATUS, &vsCompiled );
            if ( bHasPixelShader )
                glGetShaderiv( ps, GL_COMPILE_STATUS, &psCompiled );

            if ( vsCompiled == GL_TRUE && ( bHasPixelShader == false || psCompiled == GL_TRUE ) )
            {
                GLuint program = glCreateProgram();
                glAttachShader( program, vs );
                if ( bHasPixelShader )
                    glAttachShader( program, ps );
                glLinkProgram( program );

                GLint isLinked{ 0 };
                glGetProgramiv( program, GL_LINK_STATUS, &isLinked );
                if ( isLinked == GL_TRUE )
                    record._program = program;
                else
                {
                    GLchar infoLog[constant::kMaxBuffer1024];
                    glGetProgramInfoLog( program, sizeof( infoLog ), nullptr, infoLog );
                    SW_LOG_ERROR( "Graphics program link failed (%# / %#): %#",
                                  desc._vertexShaderPath, desc._pixelShaderPath, infoLog );
                    glDeleteProgram( program );
                }
            }
            else
            {
                GLchar infoLog[constant::kMaxBuffer1024];
                if ( vsCompiled != GL_TRUE )
                {
                    glGetShaderInfoLog( vs, sizeof( infoLog ), nullptr, infoLog );
                    SW_LOG_ERROR( "VS specialize failed (%#): %#", desc._vertexShaderPath, infoLog );
                }
                if ( bHasPixelShader && psCompiled != GL_TRUE )
                {
                    glGetShaderInfoLog( ps, sizeof( infoLog ), nullptr, infoLog );
                    SW_LOG_ERROR( "PS specialize failed (%#): %#", desc._pixelShaderPath, infoLog );
                }
            }
            glDeleteShader( vs );
            if ( ps != 0 )
                glDeleteShader( ps );
        }

        if ( record._program == 0 )
            return 0;

        record._topology          = desc._topology;
        record._fillMode          = desc._fillMode;
        record._cullMode          = desc._cullMode;
        record._bEnableDepthTest  = desc._bEnableDepthTest ? 1 : 0;
        record._bEnableDepthWrite = desc._bEnableDepthWrite ? 1 : 0;
        record._bEnableBlend      = desc._bEnableBlend ? 1 : 0;
        record._reserved          = 0;

        return _pDevice->_pipelineStates.insert( std::move( record ) );
    }

    RHIPipelineStateHandle OpenGLRHIResource::createComputePipelineState( string_view shaderPath, string_view entryPoint )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return 0;

        ScopedOpenGLContext                        ctxScope( _pDevice );
        OpenGLRHIDevice::OpenGLPipelineStateRecord record{};

        ShaderCompileDesc csDesc{};
        csDesc._filePath             = shaderPath;
        csDesc._entryPoint           = entryPoint;
        csDesc._stage                = ShaderStage::Compute;
        csDesc._targetFormat         = ShaderTargetFormat::SPIRV_OpenGL;
        ShaderCompileResult csResult = compileShader( csDesc );

        if ( csResult._bSuccess )
        {
            if ( glad_glShaderBinary == nullptr || glad_glSpecializeShader == nullptr )
            {
                SW_LOG_ERROR( "GL_ARB_gl_spirv unavailable for compute PSO" );
                return 0;
            }

            GLuint cs = glCreateShader( GL_COMPUTE_SHADER );

            glShaderBinary( 1, &cs, GL_SHADER_BINARY_FORMAT_SPIR_V, csResult._bytecode.data(), static_cast<GLsizei>( csResult._bytecode.size() ) );
            glSpecializeShader( cs, csDesc._entryPoint.c_str(), 0, nullptr, nullptr );

            GLint csCompiled = GL_FALSE;
            glGetShaderiv( cs, GL_COMPILE_STATUS, &csCompiled );

            if ( csCompiled == GL_TRUE )
            {
                GLuint program = glCreateProgram();
                glAttachShader( program, cs );
                glLinkProgram( program );

                GLint isLinked{ 0 };
                glGetProgramiv( program, GL_LINK_STATUS, &isLinked );
                if ( isLinked == GL_TRUE )
                    record._program = program;
                else
                {
                    GLchar infoLog[constant::kMaxBuffer1024];
                    glGetProgramInfoLog( program, sizeof( infoLog ), nullptr, infoLog );
                    SW_LOG_ERROR( "Compute shader program link failed: %#", infoLog );
                    glDeleteProgram( program );
                }
            }
            else
            {
                GLchar infoLog[constant::kMaxBuffer1024];
                glGetShaderInfoLog( cs, sizeof( infoLog ), nullptr, infoLog );
                SW_LOG_ERROR( "Compute shader specialize/compile failed: %#", infoLog );
            }
            glDeleteShader( cs );
        }
        if ( record._program == 0 )
            return 0;

        return _pDevice->_pipelineStates.insert( std::move( record ) );
    }

    void OpenGLRHIResource::destroyPipelineState( RHIPipelineStateHandle pso )
    {
        OpenGLRHIDevice::OpenGLPipelineStateRecord* pRecordPtr = _pDevice->_pipelineStates.get( pso );
        if ( pRecordPtr == nullptr )
            return;

        OpenGLRHIDevice::OpenGLPipelineStateRecord& record  = *pRecordPtr;
        const GLuint                                program = record._program;
        const GLuint                                vao     = record._vao;
        _pDevice->_pipelineStates.erase( pso );

        if ( _pDevice->_boundGraphicsPso == pso )
            _pDevice->_boundGraphicsPso = 0;

        if ( program == 0 && vao == 0 )
            return;

        auto releaseCb = [program, vao]()
        {
            if ( program != 0 )
            {
                GLuint name = program;
                glDeleteProgram( name );
            }
            if ( vao != 0 )
            {
                GLuint name = vao;
                glDeleteVertexArrays( 1, &name );
            }
        };
        _pDevice->_releaseQueue.enqueueRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ) );
    }

    RHIRenderPassHandle OpenGLRHIResource::createRenderPass( const RHIRenderPassDesc& desc )
    {
        OpenGLRHIDevice::OpenGLRenderPassRecord record{};
        record._desc     = desc;
        record._bAlive   = 1;
        record._reserved = 0;
        _pDevice->_listRenderPass.push_back( record );
        return _pDevice->_listRenderPass.size();
    }

    void OpenGLRHIResource::destroyRenderPass( RHIRenderPassHandle pass )
    {
        if ( pass == 0 || pass > _pDevice->_listRenderPass.size() )
            return;
        _pDevice->_listRenderPass[pass - 1]._bAlive = 0;
        _pDevice->_listRenderPass[pass - 1]._desc   = RHIRenderPassDesc{};
    }

    RHIBufferHandle OpenGLRHIResource::createConstantBuffer( uint32 size )
    {
        const uint32 alignedSize = MathUtil::align( size, 256u );
        GLuint       ubo;
        glGenBuffers( 1, &ubo );
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
        glBindBuffer( GL_UNIFORM_BUFFER, ubo );
        glBufferSubData( GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>( size ), pData );
        glBindBuffer( GL_UNIFORM_BUFFER, 0 );
    }

    RHIBufferHandle OpenGLRHIResource::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || elementSize == 0 || elementCount == 0 )
            return 0;

        RHIBufferDesc desc{};
        desc._elementSize  = elementSize;
        desc._elementCount = elementCount;
        desc._sizeBytes    = elementSize * elementCount;
        desc._usage        = RHIBufferUsage::Structured | RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs;
        return createBuffer( desc );
    }

    void OpenGLRHIResource::updateStructuredBuffer( RHIBufferHandle buffer, const void* pData, uint32 size )
    {
        if ( _pDevice->_bInitialized == SW_FALSE || buffer == 0 || pData == nullptr || size == 0 )
            return;

        GLuint ssbo = _pDevice->resolveGlBuffer( buffer );
        if ( ssbo == 0 )
            return;
        glBindBuffer( GL_SHADER_STORAGE_BUFFER, ssbo );
        glBufferSubData( GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>( size ), pData );
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

        const uint32 alignedSize = MathUtil::align( sizeBytes, 256u );

        // SSBO allocation; same name can bind as GL_DRAW_INDIRECT_BUFFER / DISPATCH_INDIRECT_BUFFER.
        GLuint ssbo{ 0 };
        glGenBuffers( 1, &ssbo );
        glBindBuffer( GL_SHADER_STORAGE_BUFFER, ssbo );
        glBufferData( GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>( alignedSize ), desc._pInitialData, GL_DYNAMIC_DRAW );
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
        if ( buffer == _pDevice->_boundMeshVb )
            _pDevice->_boundMeshVb = 0;
        if ( buffer == _pDevice->_boundIndexBuffer )
            _pDevice->_boundIndexBuffer = 0;

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
        record._texture       = tex;
        record._width         = desc._width;
        record._height        = desc._height;
        record._mipLevels     = mipLevels;
        record._format        = desc._format;
        record._bDepthStencil = bDepth ? 1 : 0;
        record._bUAV          = desc._bIsUnorderedAccess ? 1 : 0;
        record._reserved      = 0;

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

        return _pDevice->_gpuTextures.insert( std::move( record ) );
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
            _pDevice->_listRegisteredTexture[textureIndex]._texture = 0;
            _pDevice->_listTextureFree.push_back( static_cast<uint32>( textureIndex ) );
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

    RHIDescriptorIndex OpenGLRHIResource::registerBindlessTexture( RHITextureHandle texture )
    {
        if ( texture == 0 )
            return kInvalidDescriptorIndex;

        const uint32 glName = _pDevice->getGLTextureName( texture );
        if ( glName == 0 )
            return kInvalidDescriptorIndex;

        RHIDescriptorIndex index;
        if ( _pDevice->_listTextureFree.empty() == false )
        {
            index = _pDevice->_listTextureFree.back();
            _pDevice->_listTextureFree.pop_back();
        }
        else
            index = static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredTexture.size() );

        if ( index >= _pDevice->_listRegisteredTexture.size() )
            _pDevice->_listRegisteredTexture.resize( index + 1 );

        _pDevice->_listRegisteredTexture[index]._texture = texture;
        return index;
    }

    RHIDescriptorIndex OpenGLRHIResource::registerBindlessResource( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return kInvalidDescriptorIndex;

        GLuint ubo = _pDevice->resolveGlBuffer( buffer );
        if ( ubo == 0 )
            return kInvalidDescriptorIndex;
        RHIDescriptorIndex index;
        if ( _pDevice->_listBindlessFree.empty() == false )
        {
            index = _pDevice->_listBindlessFree.back();
            _pDevice->_listBindlessFree.pop_back();
        }
        else
            index = static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindless.size() );

        if ( index >= _pDevice->_listRegisteredBindless.size() )
            _pDevice->_listRegisteredBindless.resize( index + 1 );
        _pDevice->_listRegisteredBindless[index]._buffer = buffer;
        return index;
    }

    void OpenGLRHIResource::unregisterBindlessResource( RHIDescriptorIndex index )
    {
        if ( index < _pDevice->_listRegisteredBindless.size() )
        {
            _pDevice->_listRegisteredBindless[index]._buffer = 0;
            _pDevice->_listBindlessFree.push_back( index );
        }
    }

    RHIDescriptorIndex OpenGLRHIResource::registerBindlessUAV( RHIBufferHandle buffer )
    {
        if ( buffer == 0 )
            return kInvalidDescriptorIndex;

        GLuint ssbo = _pDevice->resolveGlBuffer( buffer );
        if ( ssbo == 0 )
            return kInvalidDescriptorIndex;
        RHIDescriptorIndex index;
        if ( _pDevice->_listUavFree.empty() == false )
        {
            index = _pDevice->_listUavFree.back();
            _pDevice->_listUavFree.pop_back();
        }
        else
            index = static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredUAV.size() );

        if ( index >= _pDevice->_listRegisteredUAV.size() )
            _pDevice->_listRegisteredUAV.resize( index + 1 );
        _pDevice->_listRegisteredUAV[index]._buffer = buffer;
        return index;
    }

    void OpenGLRHIResource::unregisterBindlessUAV( RHIDescriptorIndex index )
    {
        if ( index < _pDevice->_listRegisteredUAV.size() )
        {
            _pDevice->_listRegisteredUAV[index]._buffer = 0;
            _pDevice->_listUavFree.push_back( index );
        }
    }
} // namespace sw
