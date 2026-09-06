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
} // namespace sw
