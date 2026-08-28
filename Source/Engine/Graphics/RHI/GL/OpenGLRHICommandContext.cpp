#include "pch.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHICommandContext.h"

#include "Engine/Graphics/RHI/GL/OpenGLRHIDevice.h"

#include <glad/glad.h>

namespace sw
{
	static GLenum toGlPrimitive( RHIPrimitiveTopology topology )
	{
		switch ( topology )
		{
			case RHIPrimitiveTopology::TriangleList:
				return GL_TRIANGLES;
			case RHIPrimitiveTopology::LineList:
				return GL_LINES;
			case RHIPrimitiveTopology::PointList:
				return GL_POINTS;
			default:
				return GL_TRIANGLES;
		}
	}

	void OpenGLRHICommandContext::beginOffscreenPass( RHITextureHandle colorTarget, float32 clearColor[4] )
	{
		if ( colorTarget == 0 )
		{
			_pDevice->beginFrame( clearColor );
			return;
		}

		if ( _pDevice->_bInitialized == false )
			return;

		const OpenGLRHIDevice::OpenGLTextureRecord* pRecord = _pDevice->resolveTexture( colorTarget );
		if ( pRecord == nullptr || pRecord->fbo == 0 )
			return;

		glBindFramebuffer( GL_FRAMEBUFFER, pRecord->fbo );
		glViewport( 0, 0, static_cast<GLsizei>( pRecord->width ), static_cast<GLsizei>( pRecord->height ) );
		glClearColor( clearColor[0], clearColor[1], clearColor[2], clearColor[3] );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	}

	void OpenGLRHICommandContext::endOffscreenPass( RHITextureHandle colorTarget )
	{
		if ( colorTarget == 0 || _pDevice->_bInitialized == false )
			return;

		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		// Make color attachment readable as a texture for subsequent sampling (ImGui Game View etc.).
		glMemoryBarrier( GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT );
	}

	void OpenGLRHICommandContext::blitTexture( RHITextureHandle src, RHITextureHandle dst )
	{
		if ( _pDevice->_bInitialized == false || src == 0 )
			return;

		const OpenGLRHIDevice::OpenGLTextureRecord* pSrcRec = _pDevice->resolveTexture( src );
		if ( pSrcRec == nullptr || pSrcRec->fbo == 0 || pSrcRec->_bDepthStencil != 0 )
			return;

		GLuint dstFbo{ 0 };
		uint32 dstW = _pDevice->_width;
		uint32 dstH = _pDevice->_height;
		if ( dst != 0 )
		{
			const OpenGLRHIDevice::OpenGLTextureRecord* pDstRec = _pDevice->resolveTexture( dst );
			if ( pDstRec == nullptr || pDstRec->fbo == 0 || pDstRec->_bDepthStencil != 0 )
				return;
			dstFbo = pDstRec->fbo;
			dstW   = pDstRec->width;
			dstH   = pDstRec->height;
		}

		glBindFramebuffer( GL_READ_FRAMEBUFFER, pSrcRec->fbo );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, dstFbo );
		glBlitFramebuffer( 0, 0, static_cast<GLint>( pSrcRec->width ), static_cast<GLint>( pSrcRec->height ),
						   0, 0, static_cast<GLint>( dstW ), static_cast<GLint>( dstH ),
						   GL_COLOR_BUFFER_BIT, GL_LINEAR );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	}

	void OpenGLRHICommandContext::setPipelineState( RHIPipelineStateHandle pso )
	{
		const OpenGLRHIDevice::OpenGLPipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
		if ( pRecord == nullptr )
			return;

		if ( pRecord->program == 0 )
			return;

		_pDevice->_boundGraphicsPso = pso;
		glUseProgram( pRecord->program );

		if ( pRecord->vao != 0 )
			glBindVertexArray( pRecord->vao );
		else if ( _pDevice->_vao != 0 )
			glBindVertexArray( _pDevice->_vao );

		glPolygonMode( GL_FRONT_AND_BACK, pRecord->fillMode == RHIFillMode::Wireframe ? GL_LINE : GL_FILL );

		if ( pRecord->cullMode == RHICullMode::None )
			glDisable( GL_CULL_FACE );
		else
		{
			glEnable( GL_CULL_FACE );
			glCullFace( pRecord->cullMode == RHICullMode::Front ? GL_FRONT : GL_BACK );
			glFrontFace( GL_CW ); // match DirectX / clip-control path
		}

		if ( pRecord->_bEnableDepthTest )
		{
			glEnable( GL_DEPTH_TEST );
			glDepthFunc( GL_LESS );
			glDepthMask( pRecord->_bEnableDepthWrite ? GL_TRUE : GL_FALSE );
		}
		else
		{
			glDisable( GL_DEPTH_TEST );
			glDepthMask( GL_FALSE );
		}

		if ( pRecord->_bEnableBlend )
		{
			glEnable( GL_BLEND );
			glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		}
		else
			glDisable( GL_BLEND );
	}

	void OpenGLRHICommandContext::setComputePipelineState( RHIPipelineStateHandle pso )
	{
		const OpenGLRHIDevice::OpenGLPipelineStateRecord* pRecord = _pDevice->_pipelineStates.get( pso );
		if ( pRecord == nullptr )
			return;

		if ( pRecord->program != 0 )
			glUseProgram( pRecord->program );
	}

	void OpenGLRHICommandContext::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
	{
		if ( _pDevice->_bInitialized == false )
			return;

		uint32 w = beginInfo._width > 0 ? beginInfo._width : _pDevice->_width;
		uint32 h = beginInfo._height > 0 ? beginInfo._height : _pDevice->_height;

		const bool		 bBindColor = beginInfo._bBindColor != 0;
		const bool		 bHasDepth	= beginInfo._depthTarget != 0;
		const uint32	 colorCount = bBindColor ? ( beginInfo._colorTargetCount > 0 ? beginInfo._colorTargetCount : 1u ) : 0u;
		RHITextureHandle colorHandles[kMaxColorAttachments]{};
		for ( uint32 attachmentIndex = 0; attachmentIndex < colorCount && attachmentIndex < kMaxColorAttachments; ++attachmentIndex )
		{
			colorHandles[attachmentIndex] = ( beginInfo._colorTargetCount > 0 ) ? beginInfo._arrColorTargets[attachmentIndex] : beginInfo._colorTarget;
		}

		bool bDepthOnly = ( bBindColor == false );
		if ( bDepthOnly == false && colorCount == 1 && colorHandles[0] != 0 )
		{
			const OpenGLRHIDevice::OpenGLTextureRecord* pColorRec = _pDevice->resolveTexture( colorHandles[0] );
			if ( pColorRec != nullptr && pColorRec->_bDepthStencil != 0 )
				bDepthOnly = true;
		}

		GLuint fbo{ 0 };
		if ( bBindColor && bDepthOnly == false && colorCount > 0 && colorHandles[0] != 0 )
			fbo = _pDevice->ensureCompositeFboMRT( colorHandles, colorCount, beginInfo._depthTarget );
		else if ( bBindColor == false && bHasDepth )
			fbo = _pDevice->ensureCompositeFboMRT( nullptr, 0, beginInfo._depthTarget );

		if ( fbo == 0 && colorCount == 1 && colorHandles[0] != 0 )
		{
			const OpenGLRHIDevice::OpenGLTextureRecord* pRec = _pDevice->resolveTexture( colorHandles[0] );
			if ( pRec != nullptr )
			{
				fbo		   = pRec->fbo;
				bDepthOnly = pRec->_bDepthStencil != 0;
				if ( beginInfo._width == 0 )
					w = pRec->width;
				if ( beginInfo._height == 0 )
					h = pRec->height;
			}
		}

		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		for ( uint32 unit = 0; unit < 16; ++unit )
		{
			if ( glad_glBindTextureUnit != nullptr )
				glBindTextureUnit( unit, 0 );
			else
			{
				glActiveTexture( GL_TEXTURE0 + unit );
				glBindTexture( GL_TEXTURE_2D, 0 );
			}
		}
		glViewport( 0, 0, static_cast<GLsizei>( w ), static_cast<GLsizei>( h ) );

		if ( bHasDepth || bDepthOnly )
		{
			glEnable( GL_DEPTH_TEST );
			glDepthMask( GL_TRUE );
		}
		else
		{
			glDisable( GL_DEPTH_TEST );
			glDepthMask( GL_FALSE );
		}

		if ( bDepthOnly == false )
		{
			glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
			if ( fbo != 0 )
			{
				if ( colorCount > 1 )
				{
					GLenum drawBuffers[kMaxColorAttachments]{};
					for ( uint32 attachmentIndex = 0; attachmentIndex < colorCount; ++attachmentIndex )
					{
						drawBuffers[attachmentIndex] = GL_COLOR_ATTACHMENT0 + attachmentIndex;
					}
					glDrawBuffers( static_cast<GLsizei>( colorCount ), drawBuffers );
				}
				else
					glDrawBuffer( GL_COLOR_ATTACHMENT0 );
			}
			else
				glDrawBuffer( GL_BACK );

			for ( uint32 attachmentIndex = 0; attachmentIndex < colorCount; ++attachmentIndex )
			{
				const RHIRenderPassLoadOp loadOp = ( beginInfo._colorTargetCount > 0 ) ? beginInfo._arrLoadOps[attachmentIndex] : beginInfo._loadOp;
				const float32*			  pClear = ( beginInfo._colorTargetCount > 0 ) ? beginInfo._arrClearColors[attachmentIndex] : beginInfo._arrClearColor;
				if ( loadOp != RHIRenderPassLoadOp::Clear )
					continue;

				if ( fbo != 0 )
				{
					GLenum drawBuf = GL_COLOR_ATTACHMENT0 + attachmentIndex;
					glDrawBuffers( 1, &drawBuf );
				}
				else
				{
					glDrawBuffer( GL_BACK );
				}
				glClearColor( pClear[0], pClear[1], pClear[2], pClear[3] );
				glClear( GL_COLOR_BUFFER_BIT );
			}
		}
		if ( ( bHasDepth || bDepthOnly ) && beginInfo._depthLoadOp == RHIRenderPassLoadOp::Clear )
		{
			glDepthMask( GL_TRUE );
			glClearDepth( static_cast<GLclampd>( beginInfo._clearDepth ) );
			glClear( GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
		}
	}

	void OpenGLRHICommandContext::endRenderPass()
	{
		if ( _pDevice->_bInitialized == false )
			return;
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	}

	void OpenGLRHICommandContext::transitionBuffer( RHIBufferHandle buffer, RHIBufferState newState )
	{
		(void)buffer;
		if ( _pDevice->_bInitialized == false )
			return;

		switch ( newState )
		{
			case RHIBufferState::IndirectArgument:
				glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT );
				break;
			case RHIBufferState::ShaderResource:
			case RHIBufferState::VertexOrConstant:
			case RHIBufferState::Index:
				glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT );
				break;
			case RHIBufferState::UnorderedAccess:
				glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT );
				break;
			case RHIBufferState::CopyDest:
				glMemoryBarrier( GL_BUFFER_UPDATE_BARRIER_BIT );
				break;
			case RHIBufferState::Common:
				break;
		}
	}

	void OpenGLRHICommandContext::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
	{
		if ( _pDevice->_bInitialized == false || index == kInvalidDescriptorIndex )
			return;

		if ( index < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredUAV.size() ) &&
			 _pDevice->_listRegisteredUAV[index].buffer != 0 )
		{
			GLuint ssbo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredUAV[index].buffer );
			if ( ssbo != 0 )
			{
				glBindBufferBase( GL_SHADER_STORAGE_BUFFER, 48 + slot, ssbo );
				return;
			}
		}

		if ( index < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindlessVector.size() ) &&
			 _pDevice->_listRegisteredBindlessVector[index].buffer != 0 )
		{
			GLuint ssbo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindlessVector[index].buffer );
			if ( ssbo != 0 )
			{
				glBindBufferBase( GL_SHADER_STORAGE_BUFFER, 48 + slot, ssbo );
				return;
			}
		}
	}

	void OpenGLRHICommandContext::bindShaderResource( RHIDescriptorIndex index, uint32 slot )
	{
		if ( _pDevice->_bInitialized == false || index == kInvalidDescriptorIndex )
			return;

		if ( index < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredTexture.size() ) &&
			 _pDevice->_listRegisteredTexture[index].texture != 0 )
		{
			const OpenGLRHIDevice::OpenGLTextureRecord* pRec = _pDevice->resolveTexture( _pDevice->_listRegisteredTexture[index].texture );
			const GLuint								tex	 = pRec != nullptr ? pRec->texture : 0;
			if ( tex != 0 )
			{
				glBindTextureUnit( slot, tex );
				return;
			}
		}

		if ( index < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindlessVector.size() ) &&
			 _pDevice->_listRegisteredBindlessVector[index].buffer != 0 )
		{
			GLuint ssbo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindlessVector[index].buffer );
			if ( ssbo != 0 )
			{
				glBindBufferBase( GL_SHADER_STORAGE_BUFFER, slot, ssbo );
				return;
			}
		}
	}

	void OpenGLRHICommandContext::setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset )
	{
		(void)slot;
		_pDevice->_boundMeshVb	   = buffer;
		_pDevice->_boundMeshStride = stride > 0 ? stride : static_cast<uint32>( sizeof( RHIVertex ) );
		_pDevice->_boundMeshOffset = offset;
	}

	void OpenGLRHICommandContext::setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset )
	{
		_pDevice->_boundIndexBuffer = buffer;
		_pDevice->_boundIndexStride = ( indexStride == 2 ) ? 2u : 4u;
		_pDevice->_boundIndexOffset = offset;
	}

	void OpenGLRHICommandContext::draw( uint32 vertexCount, uint32 startVertex, RHIDescriptorIndex materialDescriptorIndex )
	{
		if ( _pDevice->_bInitialized == false || vertexCount == 0 )
			return;

		GLuint program = _pDevice->_shaderProgram;
		GLenum mode	   = GL_TRIANGLES;

		const OpenGLRHIDevice::OpenGLPipelineStateRecord* pPso = _pDevice->_pipelineStates.get( _pDevice->_boundGraphicsPso );
		if ( pPso != nullptr )
		{
			if ( pPso->program != 0 )
			{
				program = pPso->program;
				mode	= toGlPrimitive( pPso->topology );
			}
		}

		if ( program == 0 )
			return;

		glUseProgram( program );

		if ( materialDescriptorIndex != kInvalidDescriptorIndex && materialDescriptorIndex < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindlessVector.size() ) )
		{
			GLuint ubo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindlessVector[materialDescriptorIndex].buffer );
			if ( ubo != 0 )
				glBindBufferBase( GL_UNIFORM_BUFFER, 16, ubo );
		}

		if ( _pDevice->_boundMeshVb != 0 )
		{
			const GLuint vbo = _pDevice->resolveGlBuffer( _pDevice->_boundMeshVb );
			if ( vbo != 0 && _pDevice->_meshVao != 0 )
			{
				const GLsizei stride = static_cast<GLsizei>( _pDevice->_boundMeshStride );
				glBindVertexArray( _pDevice->_meshVao );
				glBindBuffer( GL_ARRAY_BUFFER, vbo );
				glEnableVertexAttribArray( 0 );
				glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, stride,
									   reinterpret_cast<const void*>( static_cast<uintptr_t>( _pDevice->_boundMeshOffset + offsetof( RHIVertex, position ) ) ) );
				glEnableVertexAttribArray( 1 );
				glVertexAttribPointer( 1, 4, GL_FLOAT, GL_FALSE, stride,
									   reinterpret_cast<const void*>( static_cast<uintptr_t>( _pDevice->_boundMeshOffset + offsetof( RHIVertex, color ) ) ) );
				glDrawArrays( mode, static_cast<GLint>( startVertex ), static_cast<GLsizei>( vertexCount ) );
				glBindVertexArray( 0 );
				glBindBuffer( GL_ARRAY_BUFFER, 0 );
			}
		}
		else if ( _pDevice->_vao != 0 )
		{
			glBindVertexArray( _pDevice->_vao );
			glDrawArrays( mode, static_cast<GLint>( startVertex ), static_cast<GLsizei>( vertexCount ) );
			glBindVertexArray( 0 );
		}
	}

	void OpenGLRHICommandContext::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
	{
		if ( _pDevice->_bInitialized == false )
			return;

		GLuint											  program = _pDevice->_shaderProgram;
		const OpenGLRHIDevice::OpenGLPipelineStateRecord* pPso	  = _pDevice->_pipelineStates.get( _pDevice->_boundGraphicsPso );
		if ( pPso != nullptr && pPso->program != 0 )
			program = pPso->program;

		if ( program != 0 )
			glUseProgram( program );

		glDispatchCompute( threadGroupCountX, threadGroupCountY, threadGroupCountZ );
	}

	void OpenGLRHICommandContext::setViewport( const RHIViewport& viewport )
	{
		if ( _pDevice->_bInitialized == false )
			return;
		glViewport( static_cast<GLint>( viewport._x ),
					static_cast<GLint>( viewport._y ),
					static_cast<GLsizei>( viewport._width ),
					static_cast<GLsizei>( viewport._height ) );
	}

	void OpenGLRHICommandContext::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* pData, uint32 destOffsetIn32BitValues )
	{
		if ( _pDevice->_bInitialized == false || num32BitValues == 0 || pData == nullptr )
			return;
		if ( destOffsetIn32BitValues >= OpenGLRHIDevice::kMaxComputeRootConstantDwords )
			return;

		const uint32 maxCount = OpenGLRHIDevice::kMaxComputeRootConstantDwords - destOffsetIn32BitValues;
		const uint32 count	  = ( num32BitValues > maxCount ) ? maxCount : num32BitValues;

		if ( _pDevice->ensureComputeRootConstantUbo() == false )
			return;

		Memory::copy( _pDevice->_arrComputeRootConstantShadow + destOffsetIn32BitValues, pData, static_cast<size_t>( count ) * sizeof( uint32 ) );

		if ( glad_glNamedBufferSubData != nullptr )
			glNamedBufferSubData( _pDevice->_computeRootConstantUbo, 0, static_cast<GLsizeiptr>( sizeof( _pDevice->_arrComputeRootConstantShadow ) ), _pDevice->_arrComputeRootConstantShadow );
		else
		{
			glBindBuffer( GL_UNIFORM_BUFFER, _pDevice->_computeRootConstantUbo );
			glBufferSubData( GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>( sizeof( _pDevice->_arrComputeRootConstantShadow ) ), _pDevice->_arrComputeRootConstantShadow );
			glBindBuffer( GL_UNIFORM_BUFFER, 0 );
		}
		glBindBufferBase( GL_UNIFORM_BUFFER, rootParameterIndex, _pDevice->_computeRootConstantUbo );
	}

	void OpenGLRHICommandContext::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
												RHIDescriptorIndex materialDescriptorIndex )
	{
		if ( _pDevice->_bInitialized == false || argumentBuffer == 0 )
			return;

		GLuint program = _pDevice->_shaderProgram;
		GLenum mode	   = GL_TRIANGLES;

		const OpenGLRHIDevice::OpenGLPipelineStateRecord* pPso = _pDevice->_pipelineStates.get( _pDevice->_boundGraphicsPso );
		if ( pPso != nullptr )
		{
			if ( pPso->program != 0 )
			{
				program = pPso->program;
				mode	= toGlPrimitive( pPso->topology );
			}
		}

		if ( program == 0 )
			return;

		glUseProgram( program );

		if ( materialDescriptorIndex != kInvalidDescriptorIndex &&
			 materialDescriptorIndex < static_cast<RHIDescriptorIndex>( _pDevice->_listRegisteredBindlessVector.size() ) )
		{
			GLuint ubo = _pDevice->resolveGlBuffer( _pDevice->_listRegisteredBindlessVector[materialDescriptorIndex].buffer );
			if ( ubo != 0 )
				glBindBufferBase( GL_UNIFORM_BUFFER, 16, ubo );
		}

		const GLuint vbo = _pDevice->resolveGlBuffer( _pDevice->_boundMeshVb );
		const GLuint buf = _pDevice->resolveGlBuffer( argumentBuffer );
		if ( vbo == 0 || _pDevice->_meshVao == 0 || buf == 0 || glad_glDrawArraysIndirect == nullptr )
			return;

		const GLsizei stride = static_cast<GLsizei>( _pDevice->_boundMeshStride );
		glBindVertexArray( _pDevice->_meshVao );
		glBindBuffer( GL_ARRAY_BUFFER, vbo );
		glEnableVertexAttribArray( 0 );
		glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, stride,
							   reinterpret_cast<const void*>( static_cast<uintptr_t>( _pDevice->_boundMeshOffset + offsetof( RHIVertex, position ) ) ) );
		glEnableVertexAttribArray( 1 );
		glVertexAttribPointer( 1, 4, GL_FLOAT, GL_FALSE, stride,
							   reinterpret_cast<const void*>( static_cast<uintptr_t>( _pDevice->_boundMeshOffset + offsetof( RHIVertex, color ) ) ) );

		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, buf );
		glDrawArraysIndirect( mode, reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset ) ) );
		glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );

		glBindVertexArray( 0 );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
	}

	void OpenGLRHICommandContext::drawIndexedIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		if ( _pDevice->_bInitialized == false || argumentBuffer == 0 )
			return;

		GLuint program = _pDevice->_shaderProgram;
		GLenum mode	   = GL_TRIANGLES;

		const OpenGLRHIDevice::OpenGLPipelineStateRecord* pPso = _pDevice->_pipelineStates.get( _pDevice->_boundGraphicsPso );
		if ( pPso != nullptr )
		{
			if ( pPso->program != 0 )
			{
				program = pPso->program;
				mode	= toGlPrimitive( pPso->topology );
			}
		}

		if ( program == 0 )
			return;

		glUseProgram( program );

		const GLuint vbo = _pDevice->resolveGlBuffer( _pDevice->_boundMeshVb );
		const GLuint ibo = _pDevice->resolveGlBuffer( _pDevice->_boundIndexBuffer );

		if ( vbo != 0 && _pDevice->_meshVao != 0 )
		{
			const GLsizei stride = static_cast<GLsizei>( _pDevice->_boundMeshStride );
			glBindVertexArray( _pDevice->_meshVao );
			glBindBuffer( GL_ARRAY_BUFFER, vbo );
			glEnableVertexAttribArray( 0 );
			glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, stride,
								   reinterpret_cast<const void*>( static_cast<uintptr_t>( _pDevice->_boundMeshOffset + offsetof( RHIVertex, position ) ) ) );
			glEnableVertexAttribArray( 1 );
			glVertexAttribPointer( 1, 4, GL_FLOAT, GL_FALSE, stride,
								   reinterpret_cast<const void*>( static_cast<uintptr_t>( _pDevice->_boundMeshOffset + offsetof( RHIVertex, color ) ) ) );
			if ( ibo != 0 )
				glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ibo );
		}

		GLuint buf = _pDevice->resolveGlBuffer( argumentBuffer );
		if ( buf != 0 && glad_glDrawElementsIndirect != nullptr )
		{
			const GLenum indexType = ( _pDevice->_boundIndexStride == 2 ) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
			glBindBuffer( GL_DRAW_INDIRECT_BUFFER, buf );
			glDrawElementsIndirect( mode, indexType, reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset ) ) );
			glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
		}

		if ( vbo != 0 && _pDevice->_meshVao != 0 )
		{
			glBindVertexArray( 0 );
			glBindBuffer( GL_ARRAY_BUFFER, 0 );
			if ( ibo != 0 )
				glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
		}
	}

	void OpenGLRHICommandContext::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		if ( _pDevice->_bInitialized == false || argumentBuffer == 0 )
			return;

		GLuint buf = _pDevice->resolveGlBuffer( argumentBuffer );
		if ( buf == 0 )
			return;
		glBindBuffer( GL_DISPATCH_INDIRECT_BUFFER, buf );
		if ( glad_glDispatchComputeIndirect != nullptr )
			glDispatchComputeIndirect( static_cast<GLintptr>( argumentBufferOffset ) );
		glBindBuffer( GL_DISPATCH_INDIRECT_BUFFER, 0 );
	}

	void OpenGLRHICommandContext::prepareTextureForShaderRead( RHITextureHandle texture )
	{
		(void)texture;
	}

	void OpenGLRHICommandContext::multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset,
													 uint32 maxCommandCount, RHIBufferHandle countBuffer,
													 uint32 countBufferOffset )
	{
		if ( _pDevice->_bInitialized == false || argumentBuffer == 0 || maxCommandCount == 0 )
			return;

		const GLuint vbo = _pDevice->resolveGlBuffer( _pDevice->_boundMeshVb );
		if ( vbo != 0 && _pDevice->_meshVao != 0 )
		{
			const GLsizei stride = static_cast<GLsizei>( _pDevice->_boundMeshStride );
			glBindVertexArray( _pDevice->_meshVao );
			glBindBuffer( GL_ARRAY_BUFFER, vbo );
			glEnableVertexAttribArray( 0 );
			glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, stride,
								   reinterpret_cast<const void*>( static_cast<uintptr_t>( _pDevice->_boundMeshOffset + offsetof( RHIVertex, position ) ) ) );
			glEnableVertexAttribArray( 1 );
			glVertexAttribPointer( 1, 4, GL_FLOAT, GL_FALSE, stride,
								   reinterpret_cast<const void*>( static_cast<uintptr_t>( _pDevice->_boundMeshOffset + offsetof( RHIVertex, color ) ) ) );
		}

		GLuint buf = _pDevice->resolveGlBuffer( argumentBuffer );
		if ( buf != 0 )
		{
			glBindBuffer( GL_DRAW_INDIRECT_BUFFER, buf );

			constexpr GLsizei stride  = sizeof( RHIDrawIndirectCommand );
			const void*		  pOffset = reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset ) );

			if ( countBuffer != 0 && glad_glMultiDrawArraysIndirectCount != nullptr )
			{
				GLuint count = _pDevice->resolveGlBuffer( countBuffer );
				if ( count != 0 )
					glBindBuffer( GL_PARAMETER_BUFFER, count );
				glMultiDrawArraysIndirectCount( GL_TRIANGLES, pOffset, static_cast<GLintptr>( countBufferOffset ),
												static_cast<GLsizei>( maxCommandCount ), stride );
				glBindBuffer( GL_PARAMETER_BUFFER, 0 );
			}
			else if ( glad_glMultiDrawArraysIndirect != nullptr )
				glMultiDrawArraysIndirect( GL_TRIANGLES, pOffset, static_cast<GLsizei>( maxCommandCount ), stride );
			else if ( glad_glDrawArraysIndirect != nullptr )
			{
				for ( uint32 commandIndex = 0; commandIndex < maxCommandCount; ++commandIndex )
				{
					const void* pCmdOffset = reinterpret_cast<const void*>( static_cast<uintptr_t>( argumentBufferOffset + commandIndex * sizeof( RHIDrawIndirectCommand ) ) );
					glDrawArraysIndirect( GL_TRIANGLES, pCmdOffset );
				}
			}

			glBindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
		}

		if ( _pDevice->_meshVao != 0 )
		{
			glBindVertexArray( 0 );
			glBindBuffer( GL_ARRAY_BUFFER, 0 );
		}
	}

	void OpenGLRHICommandContext::beginEventMarker( const utf8* pName )
	{
		if ( _pDevice->_bInitialized == false || pName == nullptr )
			return;
		// OpenGL 4.3+ / 4.6 Core: KHR_debug
		glPushDebugGroup( GL_DEBUG_SOURCE_APPLICATION, 0, -1, pName );
	}

	void OpenGLRHICommandContext::endEventMarker()
	{
		if ( _pDevice->_bInitialized == false )
			return;
		glPopDebugGroup();
	}
} // namespace sw
