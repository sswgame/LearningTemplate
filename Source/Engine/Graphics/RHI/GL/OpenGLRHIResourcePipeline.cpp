/**
 * @file OpenGLRHIResourcePipeline.cpp
 * @brief OpenGL 의 파이프라인 상태 객체 — PSO, 셰이더 스테이지, 렌더패스 객체
 * @details `OpenGLRHIResource` 의 일부다. 리소스(버퍼/텍스처)를 만드는 것과 파이프라인을 만드는 것은
 *          배우는 내용이 다르고 백엔드별 차이도 가장 크게 드러나는 곳이라 따로 둔다.
 */
#include "pch.h"

#include "Core/Common/EnumUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"
#include "Engine/Graphics/RHI/GL/OpenGLRHIResource.h"
#include "Engine/Graphics/RHI/Support/RHIIndexFreeList.h"
#include "Engine/Graphics/RHI/Support/RHIShaderRequest.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include <glad/glad.h>

namespace sw
{
    SW_LOG_CALLER( "OpenGLRHIResource" );

    RHIPipelineStateHandle OpenGLRHIResource::createPipelineState( const RHIPipelineStateDesc& desc )
    {
        if ( _pDevice->_bInitialized == SW_FALSE )
            return 0;

        ScopedOpenGLContext                        ctxScope( _pDevice );
        OpenGLRHIDevice::OpenGLPipelineStateRecord record{};

        // 서술체 해석(진입점 기본값·define·뎁스 전용 판정)은 RHIShaderRequest 하나가 한다 — 백엔드는 받기만 한다.
        const RHIGraphicsShaderRequest request         = RHIShaderRequest::resolveGraphics( desc, ShaderTargetFormat::SPIRV_OpenGL );
        const ShaderCompileDesc&       vsDesc          = request._vertex;
        const ShaderCompileDesc&       psDesc          = request._pixel;
        const bool                     bHasPixelShader = request._bHasPixelShader != SW_FALSE;
        ShaderCompileResult            vsResult        = RHIShaderRequest::compile( vsDesc );
        ShaderCompileResult            psResult{};
        if ( bHasPixelShader )
            psResult = RHIShaderRequest::compile( psDesc );

        if ( vsResult._bSuccess && ( bHasPixelShader == false || psResult._bSuccess ) )
        {
            if ( glad_glShaderBinary == nullptr || glad_glSpecializeShader == nullptr )
            {
                SW_LOG_ERROR( "GL_ARB_gl_spirv unavailable (glShaderBinary/glSpecializeShader null)" );
                return 0;
            }

            GLuint vertexShader = glCreateShader( GL_VERTEX_SHADER );
            GLuint pixelShader  = bHasPixelShader ? glCreateShader( GL_FRAGMENT_SHADER ) : 0;

            glShaderBinary( 1, &vertexShader, GL_SHADER_BINARY_FORMAT_SPIR_V, vsResult._bytecode.data(), static_cast<GLsizei>( vsResult._bytecode.size() ) );
            glSpecializeShader( vertexShader, vsDesc._entryPoint.c_str(), 0, nullptr, nullptr );

            if ( bHasPixelShader )
            {
                glShaderBinary( 1, &pixelShader, GL_SHADER_BINARY_FORMAT_SPIR_V, psResult._bytecode.data(), static_cast<GLsizei>( psResult._bytecode.size() ) );
                glSpecializeShader( pixelShader, psDesc._entryPoint.c_str(), 0, nullptr, nullptr );
            }

            GLint vsCompiled = GL_FALSE;
            GLint psCompiled = GL_FALSE;
            glGetShaderiv( vertexShader, GL_COMPILE_STATUS, &vsCompiled );
            if ( bHasPixelShader )
                glGetShaderiv( pixelShader, GL_COMPILE_STATUS, &psCompiled );

            if ( vsCompiled == GL_TRUE && ( bHasPixelShader == false || psCompiled == GL_TRUE ) )
            {
                GLuint program = glCreateProgram();
                glAttachShader( program, vertexShader );
                if ( bHasPixelShader )
                    glAttachShader( program, pixelShader );
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
                    glGetShaderInfoLog( vertexShader, sizeof( infoLog ), nullptr, infoLog );
                    SW_LOG_ERROR( "VS specialize failed (%#): %#", desc._vertexShaderPath, infoLog );
                }
                if ( bHasPixelShader && psCompiled != GL_TRUE )
                {
                    glGetShaderInfoLog( pixelShader, sizeof( infoLog ), nullptr, infoLog );
                    SW_LOG_ERROR( "PS specialize failed (%#): %#", desc._pixelShaderPath, infoLog );
                }
            }
            glDeleteShader( vertexShader );
            if ( pixelShader != 0 )
                glDeleteShader( pixelShader );
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
        ShaderCompileResult csResult = RHIShaderRequest::compile( csDesc );

        if ( csResult._bSuccess )
        {
            if ( glad_glShaderBinary == nullptr || glad_glSpecializeShader == nullptr )
            {
                SW_LOG_ERROR( "GL_ARB_gl_spirv unavailable for compute PSO" );
                return 0;
            }

            GLuint computeShader = glCreateShader( GL_COMPUTE_SHADER );

            glShaderBinary( 1, &computeShader, GL_SHADER_BINARY_FORMAT_SPIR_V, csResult._bytecode.data(), static_cast<GLsizei>( csResult._bytecode.size() ) );
            glSpecializeShader( computeShader, csDesc._entryPoint.c_str(), 0, nullptr, nullptr );

            GLint csCompiled = GL_FALSE;
            glGetShaderiv( computeShader, GL_COMPILE_STATUS, &csCompiled );

            if ( csCompiled == GL_TRUE )
            {
                GLuint program = glCreateProgram();
                glAttachShader( program, computeShader );
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
                glGetShaderInfoLog( computeShader, sizeof( infoLog ), nullptr, infoLog );
                SW_LOG_ERROR( "Compute shader specialize/compile failed: %#", infoLog );
            }
            glDeleteShader( computeShader );
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
        if ( _pDevice->_boundComputePso == pso )
            _pDevice->_boundComputePso = 0;

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
        record._bAlive   = SW_TRUE;
        record._reserved = 0;
        _pDevice->_listRenderPass.push_back( record );
        return _pDevice->_listRenderPass.size();
    }

    void OpenGLRHIResource::destroyRenderPass( RHIRenderPassHandle pass )
    {
        if ( pass == 0 || pass > _pDevice->_listRenderPass.size() )
            return;
        _pDevice->_listRenderPass[pass - 1]._bAlive = SW_FALSE;
        _pDevice->_listRenderPass[pass - 1]._desc   = RHIRenderPassDesc{};
    }
} // namespace sw
