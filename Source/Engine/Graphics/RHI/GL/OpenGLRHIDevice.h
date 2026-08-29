/**
 * @file OpenGLRHIDevice.h
 * @brief OpenGL 4.6 Core Profile 기반 RHI 백엔드 클래스 정의
 * @note GLAD/OpenGL 심볼은 OpenGLRHIDevice.cpp 에서만 include합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHIHandleTable.h"
#include "Engine/Graphics/RHI/RHIReleaseQueue.h"

namespace sw
{
	class OpenGLRHICommandContext;
	class OpenGLRHIResource;
	class OpenGLRHISwapChain;

	/**
	 * @class OpenGLRHIDevice
	 * @brief OpenGL 4.6 그래픽스 및 컴퓨트 디바이스 구현체 (SSBO/UBO 바인딩 지원)
	 */
	class OpenGLRHIDevice : public IRHIDevice
	{
		friend class OpenGLRHICommandContext;

	public:
		RHIBufferHandle createBuffer( const RHIBufferDesc& desc );
		RHIBufferHandle createIndexBuffer( const void* pData, uint32 sizeBytes, uint32 indexStride );
		friend class OpenGLRHISwapChain;
		friend class OpenGLRHIResource;
		/** @brief 빈 OpenGL 디바이스. */
		OpenGLRHIDevice();
		/** @brief 컨텍스트와 GL 객체를 정리합니다. */
		virtual ~OpenGLRHIDevice() override;

		/** @brief OpenGL 렌더링 컨텍스트 (wglCreateContext / EGL) 및 GLAD 로드 */
		bool initializeInternal( const RHISwapChainDesc& desc ) override;

		/** @brief OpenGL 컨텍스트 및 GL 객체 해제 */
		void shutdownInternal() override;

		/** @brief glViewport 크기 변경 */
		void resize( uint32 width, uint32 height );

		/** @brief 프레임 시작 (glClearColor 및 glClear) */
		void beginFrame( const float4& clearColor );

		/** @brief 프레임 종료 (SwapBuffers / wglSwapBuffers) */
		void endFrame( bool vsync, bool bPresent = true );

		IRHISwapChain* getSwapChain() override;
		IRHIResource*  getResource() override;
		/** @brief Present/offscreen/replay Immediate Context. */
		IRHICommandContext* getImmediateContext() override;
		/** @brief Mode=Deferred CL 바인딩용 soft Deferred Context. */
		IRHICommandContext* getDeferredCommandContext() override;

		/** @brief glFinish — GPU 대기 */
		void waitIdle() override;

		/** @brief 백엔드 타입 반환 (OpenGL) */
		RHIBackend getBackendType() const override { return RHIBackend::OpenGL; }

		/** @brief Descriptor-index tables for UBO/SSBO/texture (bind-at-draw / image units). */
		bool supportsBindless() const override { return true; }

		/** @brief RHI 텍스처 핸들에 대응하는 GL texture name (없으면 0) */
		uint32 getGLTextureName( RHITextureHandle texture ) const;

		/** @brief 네이티브 GL 텍스처 이름을 반환합니다. */
		uint32 getNativeTextureName( RHITextureHandle texture ) const override { return getGLTextureName( texture ); }

		/** @brief 네이티브 GL 텍스처 핸들을 포인터 형태로 반환합니다. */
		void* getNativeTexturePointer( RHITextureHandle texture ) const override { return reinterpret_cast<void*>( static_cast<uintptr_t>( getGLTextureName( texture ) ) ); }

		/** @brief 백엔드 이름 문자열 반환 */
		const utf8* getBackendName() const override { return "OpenGL (glad 4.6 Core)"; }

		/** @brief Native DC / Display 포인터 반환 */
		void* getNativeDevice() const override { return _pHDC; }

		/** @brief Native HGLRC 컨텍스트 포인터 반환 */
		void* getNativeContext() const override { return _pHRC; }

		bool requiresExclusiveContextThread() const override { return true; }
		bool bindGraphicsContext() override;
		/** @brief 그래픽스 컨텍스트 바인딩을 해제합니다. */
		void unbindGraphicsContext() override;

		/** @brief Native DC 포인터 반환 */
		void* getNativeSwapChain() const override { return _pHDC; }

		/** @brief OpenGL은 커맨드 큐가 없음 (nullptr) */
		void* getNativeCommandQueue() const override { return nullptr; }

		/** @brief glDrawArraysIndirect 실행 */
		void drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset = 0,
						   RHIDescriptorIndex materialDescriptorIndex = kInvalidDescriptorIndex );

		/** @brief glMultiDrawArraysIndirect when available. */
		void multiDrawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset, uint32 maxCommandCount,
								RHIBufferHandle countBuffer = 0, uint32 countBufferOffset = 0 );

		/** @brief 커맨드 리스트 객체 생성 */
		unique_ptr<IRHICommandList> createCommandList( RHICommandListMode mode ) override;

		/** @brief 커맨드 리스트 실행 */
		void executeCommandList( IRHICommandList* pCmdList ) override;

	private:
		/**
		 * @brief 풀스크린 삼각형 VAO/VBO를 만듭니다.
		 */

		/** @brief 컴퓨트 루트 상수 UBO를 확보합니다. */
		bool ensureComputeRootConstantUbo();
		/** @brief 합성 FBO를 확보합니다. */
		uint32 ensureCompositeFbo( RHITextureHandle color, RHITextureHandle depth );
		/** @brief MRT 합성 FBO를 확보합니다. */
		uint32 ensureCompositeFboMRT( const RHITextureHandle* pColors, uint32 colorCount, RHITextureHandle depth );
		/** @brief 불투명 핸들을 GLuint 이름으로 풉니다. */
		uint32 resolveGlBuffer( RHIBufferHandle handle ) const;
		/** @brief GL 버퍼 이름을 테이블에 넣고 핸들을 반환합니다. */
		RHIBufferHandle storeGlBuffer( uint32 glName );
		/** @brief 불투명 텍스처 핸들을 OpenGLTextureRecord로 풉니다. */
		struct OpenGLTextureRecord;
		OpenGLTextureRecord*	   resolveTexture( RHITextureHandle handle );
		const OpenGLTextureRecord* resolveTexture( RHITextureHandle handle ) const;

		static constexpr uint32 kMaxComputeRootConstantDwords = 64;

		/// @brief 드로우 시 바인드할 버퍼/텍스처 슬롯
		struct BindlessResourceRecord
		{
			RHIBufferHandle _buffer{ 0 };
		};

		/// @brief GLuint 텍스처 + 타깃/포맷
		struct OpenGLTextureRecord
		{
			uint32	  _texture{ 0 };
			uint32	  _fbo{ 0 };
			uint32	  _width{ 0 };
			uint32	  _height{ 0 };
			uint32	  _mipLevels{ 1 };
			RHIFormat _format = RHIFormat::R8G8B8A8_UNORM;
			uint8	  _bDepthStencil : 1;
			uint8	  _bUAV			 : 1;
			uint8	  _reserved		 : 6;
		};

		/// @brief MRT FBO 캐시 키
		struct CompositeFboKey
		{
			RHITextureHandle _arrColors[kMaxColorAttachments]{};
			uint32			 _colorCount{ 0 };
			RHITextureHandle _depth{ 0 };
			/** @brief 같으면 true를 반환합니다. */
			bool operator==( const CompositeFboKey& other ) const
			{
				if ( _colorCount != other._colorCount || _depth != other._depth )
					return false;
				for ( uint32 colorIndex = 0; colorIndex < _colorCount; ++colorIndex )
				{
					if ( _arrColors[colorIndex] != other._arrColors[colorIndex] )
						return false;
				}
				return true;
			}
		};

		/// @brief CompositeFboKey 해시
		struct CompositeFboKeyHash
		{
			/** @brief 호출 연산자입니다. */
			size_t operator()( const CompositeFboKey& key ) const
			{
				size_t h = static_cast<size_t>( key._depth ) * 1315423911u;
				h ^= static_cast<size_t>( key._colorCount ) + 0x9e3779b9u;
				for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
				{
					h ^= static_cast<size_t>( key._arrColors[colorIndex] ) + 0x9e3779b9u + ( h << 6 ) + ( h >> 2 );
				}
				return h;
			}
		};

		/// @brief 인덱스 → GL 텍스처 유닛 매핑
		struct BindlessTextureRecord
		{
			RHITextureHandle _texture{ 0 };
		};

		/// @brief 프로그램 + 래스터/블렌드/깊이 상태
		struct OpenGLPipelineStateRecord
		{
			uint32				 _program{ 0 };
			uint32				 _vao{ 0 };
			RHIPrimitiveTopology _topology = RHIPrimitiveTopology::TriangleList;
			RHIFillMode			 _fillMode = RHIFillMode::Solid;
			RHICullMode			 _cullMode = RHICullMode::None;
			uint8				 _bEnableDepthTest	: 1;
			uint8				 _bEnableDepthWrite : 1;
			uint8				 _bEnableBlend		: 1;
			uint8				 _reserved			: 5;
		};

		/// @brief 렌더 패스 서술 캐시
		struct OpenGLRenderPassRecord
		{
			RHIRenderPassDesc _desc{};
			uint8			  _bAlive	: 1;
			uint8			  _reserved : 7;
		};

		void*  _pHDC;
		void*  _pHRC;
		void*  _pHWnd;
		uint32 _width;
		uint32 _height;
		uint32 _shaderProgram;
		uint32 _vao;
		uint32 _vbo;	 ///< Fullscreen stub (3 verts)
		uint32 _meshVao; ///< VAO for scene mesh draw()
		uint32 _defaultSampler;
		uint32 _defaultTexture;

		RHIHandleTable<uint32> _gpuBuffers;
		RHIBufferHandle		   _boundMeshVb;
		uint32				   _boundMeshStride; ///< 바인딩된 VB stride
		uint32				   _boundMeshOffset;
		RHIBufferHandle		   _boundIndexBuffer;
		uint32				   _boundIndexStride;
		uint32				   _boundIndexOffset;

		vector<BindlessResourceRecord> _listRegisteredBindlessVector;
		vector<uint32>				   _listBindlessFree;
		vector<BindlessResourceRecord> _listRegisteredUAV;
		vector<uint32>				   _listUavFree;

		RHIHandleTable<OpenGLTextureRecord>							_gpuTextures;
		unordered_map<CompositeFboKey, uint32, CompositeFboKeyHash> _mapCompositeFbo;

		vector<BindlessTextureRecord> _listRegisteredTexture;
		vector<uint32>				  _listTextureFree;

		uint32 _computeRootConstantUbo;
		uint32 _arrComputeRootConstantShadow[kMaxComputeRootConstantDwords];

		RHIHandleTable<OpenGLPipelineStateRecord> _pipelineStates;
		vector<OpenGLRenderPassRecord>			  _listRenderPass;

		RHIReleaseQueue _releaseQueue;

		sw::unique_ptr<OpenGLRHICommandContext> _immContext;
		sw::unique_ptr<OpenGLRHICommandContext> _deferredContext;
		sw::unique_ptr<OpenGLRHISwapChain>		_swapChainImpl;
		sw::unique_ptr<OpenGLRHIResource>		_resourceImpl;

		RHIPipelineStateHandle _boundGraphicsPso;
		int8				   _lastVsync; ///< -1 unset, 0/1 last applied
		uint8				   _bInitialized  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 7;
	};

	/**
	 * @struct ScopedOpenGLContext
	 * @brief OpenGL 배타적 그래픽스 컨텍스트 바인딩이 필요한 작업 시 컨텍스트를 획득하고 해제하는 RAII 가드
	 */
	struct ScopedOpenGLContext
	{
		IRHIDevice* _pDevice{ nullptr };
		bool		_bNeedsUnbind{ false };

		explicit ScopedOpenGLContext( IRHIDevice* pDevice )
			: _pDevice{ pDevice }
			, _bNeedsUnbind{ false }
		{
			if ( _pDevice != nullptr && _pDevice->requiresExclusiveContextThread() )
			{
				_bNeedsUnbind = _pDevice->bindGraphicsContext();
			}
		}

		~ScopedOpenGLContext()
		{
			if ( _bNeedsUnbind && _pDevice != nullptr )
			{
				_pDevice->unbindGraphicsContext();
			}
		}
	};
} // namespace sw
