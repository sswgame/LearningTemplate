/**
 * @file RHITypes.h
 * @brief RHI 공통 핸들, 열거형, 서술체
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Common/Common.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) 핸들 — 버퍼/텍스처/디스크립터, 무효 인덱스
	// ------------------------------------------------------------------------------
	/** @brief GPU 버퍼 리소스 64비트 핸들 */
	using RHIBufferHandle = uint64;

	/** @brief GPU 텍스처 리소스 64비트 핸들 */
	using RHITextureHandle = uint64;

	/** @brief Bindless 리소스 인덱스 (Descriptor Heap / Set 내 바인딩 인덱스) */
	using RHIDescriptorIndex = uint32;

	/** @brief 유효하지 않은 Descriptor 인덱스 */
	constexpr RHIDescriptorIndex kInvalidDescriptorIndex = invalid_index::kUint32;

	/**
	 * @struct RHIVertex
	 * @brief 기본 3D 정점 (직접 그리기 예제용)
	 */
	struct RHIVertex
	{
		float32 position[3]; ///< 정점 위치 (X, Y, Z)
		float32 color[4];	 ///< 정점 색상 (R, G, B, A)
	};

	// ------------------------------------------------------------------------------
	// 2) 백엔드 · 포맷 — API 종류, ImGui Vulkan 핸들, 픽셀 포맷
	// ------------------------------------------------------------------------------
	/**
	 * @enum RHIBackend
	 * @brief 엔진이 지원하는 크로스 플랫폼 Graphics API
	 */
	ENUM()
	enum class RHIBackend : uint32
	{
		DirectX11 = 0, ///< Direct3D 11
		DirectX12 = 1, ///< Direct3D 12 (Bindless)
		Vulkan	  = 2, ///< Vulkan 1.3 (Bindless)
		OpenGL	  = 3, ///< OpenGL 4.5+
	};

	/**
	 * @enum RHICommandListMode
	 * @brief 소프트웨어 CommandList의 제출 시기 (Immediate/Deferred Context와 다름).
	 * @details Immediate: endCommandList에서 Immediate Context로 flush(replay).
	 *          Deferred: 기록만; executeCommandList → Immediate Context에 replay.
	 *          Context 슬롯 선택은 IRHIDevice::getImmediateContext / getDeferredCommandContext.
	 */
	ENUM()
	enum class RHICommandListMode : uint8
	{
		Deferred  = 0, ///< 기록 후 execute에서 Immediate Context로 replay
		Immediate = 1, ///< endCommandList에서 Immediate Context로 즉시 flush
	};

	/**
	 * @struct RHIVulkanImGuiNative
	 * @brief Vulkan ImGui init용 opaque 핸들 묶음 (Editor가 concrete VulkanRHIDevice에 의존하지 않도록)
	 */
	struct RHIVulkanImGuiNative
	{
		void*  _pInstance{ nullptr };
		void*  _pPhysicalDevice{ nullptr };
		void*  _pDevice{ nullptr };
		void*  _pGraphicsQueue{ nullptr };
		void*  _pRenderPass{ nullptr };
		uint32 _queueFamily{ 0 };
		uint32 _minImageCount = 2;
		uint32 _imageCount	  = 2;
	};

	/**
	 * @enum RHIFormat
	 * @brief 텍스처·렌더 타깃·픽셀 데이터 포맷
	 */
	ENUM()
	enum class RHIFormat : uint32
	{
		R8G8B8A8_UNORM	   = 0, ///< 8비트 RGBA 정규화
		B8G8R8A8_UNORM	   = 1, ///< 8비트 BGRA 정규화 (DirectX 기본)
		R16G16B16A16_FLOAT = 2, ///< 16비트 부동소수점 RGBA (HDR)
		D24_UNORM_S8_UINT  = 3, ///< 24비트 깊이 + 8비트 스텐실
		R32G32B32_FLOAT	   = 4, ///< 32비트 부동소수점 RGB (위치/노멀)
		R32G32_FLOAT	   = 5, ///< 32비트 부동소수점 RG (UV)
		R32_FLOAT		   = 6, ///< 32비트 단일 부동소수점
	};

	/**
	 * @struct RHIInputElement
	 * @brief 정점 레이아웃(Input Layout) 엘리먼트
	 */
	struct RHIInputElement
	{
		string	  _semanticName;						///< 시맨틱 이름 (POSITION, COLOR 등)
		uint32	  _semanticIndex{ 0 };					///< 시맨틱 인덱스
		RHIFormat _format = RHIFormat::R32G32B32_FLOAT; ///< 엘리먼트 데이터 포맷
		uint32	  _alignedByteOffset{ 0 };				///< 버텍스 구조체 내 바이트 오프셋
		uint32	  _inputSlot{ 0 };						///< 버텍스 버퍼 입력 슬롯
	};

	// ------------------------------------------------------------------------------
	// 3) 스왑체인 · 뷰포트 · 인디렉트 커맨드
	// ------------------------------------------------------------------------------
	/**
	 * @struct RHISwapChainDesc
	 * @brief 윈도우 스왑체인 생성 설정
	 */
	REFLECT()
	struct RHISwapChainDesc
	{
		PROPERTY()
		void* _pWindowHandle{ nullptr }; ///< OS 윈도우 핸들 (HWND, Window XID 등)

		PROPERTY()
		void* _pWindowDisplay{ nullptr }; ///< X11 Display 포인터 (리눅스 전용)

		PROPERTY()
		uint32 _width{ 1280 }; ///< 스왑체인 너비 (픽셀)

		PROPERTY()
		uint32 _height{ 720 }; ///< 스왑체인 높이 (픽셀)

		PROPERTY()
		uint32 _bufferCount{ 2 }; ///< 프레임버퍼 개수 (Double/Triple Buffering)

		PROPERTY()
		bool _bVSync{ true }; ///< 수직 동기화

		PROPERTY()
		bool _bFullscreen{ false }; ///< 전체 화면
	};

	/**
	 * @struct RHIViewport
	 * @brief 렌더링 뷰포트 영역 (DirectX 규약 — 전 백엔드 동일)
	 * @details 원점은 좌상단, 픽셀 +Y는 아래. 클립/NDC +Y는 위(Direct3D).
	 *          Vulkan은 내부에서 negative-height viewport로 변환하고,
	 *          OpenGL은 glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)로 맞춘다.
	 */
	REFLECT()
	struct RHIViewport
	{
		PROPERTY()
		float32 _x{ 0.0f }; ///< 뷰포트 좌상단 X

		PROPERTY()
		float32 _y{ 0.0f }; ///< 뷰포트 좌상단 Y

		PROPERTY()
		float32 _width{ 1280.0f }; ///< 뷰포트 너비

		PROPERTY()
		float32 _height{ 720.0f }; ///< 뷰포트 높이 (양수; Vulkan이 부호 반전)

		PROPERTY()
		float32 _minDepth{ 0.0f }; ///< 최소 깊이 (0.0~1.0)

		PROPERTY()
		float32 _maxDepth{ 1.0f }; ///< 최대 깊이 (0.0~1.0)
	};

	/**
	 * @struct RHIDrawIndirectCommand
	 * @brief 간접 드로우 파라미터
	 */
	REFLECT()
	struct RHIDrawIndirectCommand
	{
		PROPERTY()
		uint32 _vertexCount = 3; ///< 정점 개수

		PROPERTY()
		uint32 _instanceCount{ 1 }; ///< 인스턴스 개수

		PROPERTY()
		uint32 _startVertexLocation{ 0 }; ///< 시작 정점 위치

		PROPERTY()
		uint32 _startInstanceLocation{ 0 }; ///< 시작 인스턴스 위치
	};

	/**
	 * @struct RHIDispatchIndirectCommand
	 * @brief 간접 컴퓨트 디스패치 파라미터
	 */
	REFLECT()
	struct RHIDispatchIndirectCommand
	{
		PROPERTY()
		uint32 _threadGroupCountX{ 1 }; ///< X축 스레드 그룹 개수

		PROPERTY()
		uint32 _threadGroupCountY{ 1 }; ///< Y축 스레드 그룹 개수

		PROPERTY()
		uint32 _threadGroupCountZ{ 1 }; ///< Z축 스레드 그룹 개수
	};

	/** @brief 파이프라인 상태 객체(PSO) 64비트 핸들 */
	using RHIPipelineStateHandle = uint64;

	/** @brief 렌더 패스 객체 64비트 핸들 */
	using RHIRenderPassHandle = uint64;

	// ------------------------------------------------------------------------------
	// 4) 버퍼 — usage 플래그, 상태, 서술체, 인덱스 인디렉트
	// ------------------------------------------------------------------------------
	/**
	 * @enum RHIBufferUsage
	 * @brief 범용 GPU 버퍼 usage 플래그 (createBuffer)
	 */
	enum class RHIBufferUsage : uint32
	{
		None			= 0,
		Vertex			= 1u << 0,
		Index			= 1u << 1,
		Constant		= 1u << 2,
		Structured		= 1u << 3,
		UnorderedAccess = 1u << 4,
		ShaderResource	= 1u << 5,
		IndirectArgs	= 1u << 6,
		Raw				= 1u << 7, ///< 바이트 주소 / RAW UAV (DX11 DRAWINDIRECT_ARGS 호환)
	};

	/** @brief usage 플래그 OR. */
	constexpr RHIBufferUsage operator|( RHIBufferUsage a, RHIBufferUsage b ) noexcept { return static_cast<RHIBufferUsage>( static_cast<uint32>( a ) | static_cast<uint32>( b ) ); }
	/** @brief usage 플래그 AND. */
	constexpr RHIBufferUsage operator&( RHIBufferUsage a, RHIBufferUsage b ) noexcept { return static_cast<RHIBufferUsage>( static_cast<uint32>( a ) & static_cast<uint32>( b ) ); }
	/** @brief mask에 flag가 켜져 있는지. */
	constexpr bool hasFlag( RHIBufferUsage mask, RHIBufferUsage flag ) noexcept { return ( static_cast<uint32>( mask ) & static_cast<uint32>( flag ) ) != 0; }

	/**
	 * @enum RHIBufferState
	 * @brief transitionBuffer 배리어용 논리 버퍼 상태
	 */
	enum class RHIBufferState : uint32
	{
		Common			 = 0,
		ShaderResource	 = 1,
		UnorderedAccess	 = 2,
		IndirectArgument = 3,
		CopyDest		 = 4,
		VertexOrConstant = 5,
		Index			 = 6,
	};

	/**
	 * @struct RHIBufferDesc
	 * @brief 범용 버퍼 생성 서술체
	 */
	struct RHIBufferDesc
	{
		uint32		   _sizeBytes{ 0 };
		uint32		   _elementSize{ 0 }; ///< Structured stride. raw/바이트 버퍼는 0
		uint32		   _elementCount{ 0 };
		RHIBufferUsage _usage		 = RHIBufferUsage::None;
		const void*	   _pInitialData = nullptr;
	};

	/**
	 * @struct RHIDrawIndexedIndirectCommand
	 * @brief 인덱스 인디렉트 드로우 인자 (D3D12/Vulkan 레이아웃과 동일)
	 */
	struct RHIDrawIndexedIndirectCommand
	{
		uint32 _indexCountPerInstance{ 0 };
		uint32 _instanceCount{ 1 };
		uint32 _startIndexLocation{ 0 };
		int32  _baseVertexLocation{ 0 };
		uint32 _startInstanceLocation{ 0 };
	};

	// ------------------------------------------------------------------------------
	// 5) 파이프라인 상태 — 블렌드, 토폴로지, 컬링, 로드/스토어
	// ------------------------------------------------------------------------------
	/**
	 * @enum RHIBlendMode
	 * @brief 머티리얼/패스 블렌드 분류
	 */
	enum class RHIBlendMode : uint8
	{
		Opaque		= 0,
		Transparent = 1, ///< SrcAlpha / InvSrcAlpha
	};

	/**
	 * @enum RHIPrimitiveTopology
	 * @brief 도형 출력 위상
	 */
	enum class RHIPrimitiveTopology
	{
		TriangleList, ///< 삼각형 리스트
		LineList,	  ///< 선 리스트
		PointList	  ///< 점 리스트
	};

	/**
	 * @enum RHIFillMode
	 * @brief 래스터라이저 와이어프레임 / 솔리드
	 */
	enum class RHIFillMode
	{
		Solid,	  ///< 일반 채우기
		Wireframe ///< 와이어프레임
	};

	/**
	 * @enum RHICullMode
	 * @brief 페이스 컬링
	 */
	enum class RHICullMode
	{
		None,  ///< 컬링 없음
		Front, ///< 전면 컬링
		Back   ///< 후면 컬링
	};

	/**
	 * @enum RHIRenderPassLoadOp
	 * @brief 렌더 패스 시작 시 프레임버퍼 로드
	 */
	enum class RHIRenderPassLoadOp
	{
		Clear,	 ///< 기존 데이터 지우기
		Load,	 ///< 기존 데이터 유지
		DontCare ///< 이전 내용 무시 (최적화)
	};

	/**
	 * @enum RHIRenderPassStoreOp
	 * @brief 렌더 패스 완료 시 프레임버퍼 저장
	 */
	enum class RHIRenderPassStoreOp
	{
		Store,	 ///< 메모리에 최종 결과 저장
		DontCare ///< 결과 보존 안 함
	};

	/** @brief beginRenderPass / PSO에서 동시 컬러 RT 최대 개수 (MRT). */
	inline constexpr uint32 kMaxColorAttachments = 4;

	/**
	 * @struct RHIPipelineStateDesc
	 * @brief Graphics & Compute 파이프라인 상태 생성 서술체
	 */
	struct SW_API RHIPipelineStateDesc
	{
		string _vertexShaderPath;  ///< 버텍스 셰이더 소스 경로
		string _vertexEntryPoint;  ///< 버텍스 셰이더 진입점
		string _pixelShaderPath;   ///< 픽셀 셰이더 소스 경로
		string _pixelEntryPoint;   ///< 픽셀 셰이더 진입점
		string _computeShaderPath; ///< 컴퓨트 셰이더 소스 경로
		string _computeEntryPoint; ///< 컴퓨트 셰이더 진입점

		vector<string> _listShaderDefines; ///< 컴파일 매크로 ("NAME" 또는 "NAME=VALUE") — 셰이더 permutation

		RHIPrimitiveTopology   _topology;							 ///< 프리미티브 위상
		RHIFillMode			   _fillMode;							 ///< 채우기 모드
		RHICullMode			   _cullMode;							 ///< 컬링 모드
		uint32				   _numRenderTargets;					 ///< 컬러 RT 개수 (MRT)
		RHIFormat			   _arrRtvFormats[kMaxColorAttachments]; ///< RT별 포맷
		RHIFormat			   _depthStencilFormat;
		uint8				   _bEnableDepthTest  : 1; ///< 깊이 테스트
		uint8				   _bEnableDepthWrite : 1; ///< 깊이 쓰기 (Transparent=0)
		uint8				   _bEnableBlend	  : 1; ///< 알파 블렌딩 (SrcAlpha/InvSrcAlpha)
		[[maybe_unused]] uint8 _reservedFlags	  : 5;

		/** @brief 기본 토폴로지/컬링/깊이 플래그. */
		RHIPipelineStateDesc() noexcept;
	};

	/**
	 * @struct RHIRenderPassAttachment
	 * @brief 렌더 패스 색상/깊이 어태치먼트
	 */
	struct RHIRenderPassAttachment
	{
		RHIFormat			 _format		   = RHIFormat::R8G8B8A8_UNORM;	  ///< 어태치먼트 포맷
		RHIRenderPassLoadOp	 _loadOp		   = RHIRenderPassLoadOp::Clear;  ///< 로드 동작
		RHIRenderPassStoreOp _storeOp		   = RHIRenderPassStoreOp::Store; ///< 저장 동작
		float32				 _arrClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };  ///< 초기화 색상 (RGBA)
	};

	/**
	 * @struct RHIRenderPassDesc
	 * @brief 렌더 패스 생성 정보
	 */
	struct SW_API RHIRenderPassDesc
	{
		vector<RHIRenderPassAttachment> _listColorAttachments; ///< 색상 어태치먼트 목록
		float32							_clearDepth;		   ///< 깊이 초기화 값
		uint8							_clearStencil;		   ///< 스텐실 초기화 값
		uint8							_bHasDepthStencil : 1; ///< 깊이/스텐실 어태치먼트 포함
		[[maybe_unused]] uint8			_reservedFlags	  : 7;

		/** @brief 깊이 클리어 기본값. */
		RHIRenderPassDesc() noexcept;
	};

	/**
	 * @struct RHITextureDesc
	 * @brief 텍스처 생성 서술체
	 */
	struct SW_API RHITextureDesc
	{
		uint32	  _width;
		uint32	  _height;
		uint32	  _depth;
		uint32	  _mipLevels;
		RHIFormat _format;

		float32 _arrClearColor[4];
		float32 _clearDepth;
		uint8	_clearStencil;

		uint8				   _bIsRenderTarget	   : 1;
		uint8				   _bIsDepthStencil	   : 1;
		uint8				   _bIsShaderResource  : 1;
		uint8				   _bIsUnorderedAccess : 1;
		[[maybe_unused]] uint8 _reservedFlags	   : 4;

		/** @brief 기본 크기/포맷/클리어. */
		RHITextureDesc() noexcept;
	};

	/**
	 * @struct RHIRenderPassBeginInfo
	 * @brief 렌더 패스 바인딩 및 시작 인자
	 * @note MRT는 `_arrColorTargets` + `_colorTargetCount`를 우선합니다. `_colorTarget` / `_arrClearColor` /
	 *       `_loadOp`는 `_colorTargetCount == 0`일 때 RT0 별칭입니다.
	 */
	struct SW_API RHIRenderPassBeginInfo
	{
		RHIRenderPassHandle	   _renderPass;
		RHITextureHandle	   _colorTarget; ///< RT0 별칭. 바인딩 시 0 = 스왑체인
		RHITextureHandle	   _arrColorTargets[kMaxColorAttachments];
		uint32				   _colorTargetCount; ///< 0 → `_bBindColor`이면 단일 `_colorTarget`
		RHITextureHandle	   _depthTarget;	  ///< 0 = 깊이/스텐실 없음
		uint32				   _width;
		uint32				   _height;
		float32				   _arrClearColor[4]; ///< RT0 클리어 별칭
		float32				   _arrClearColors[kMaxColorAttachments][4];
		float32				   _clearDepth;
		RHIRenderPassLoadOp	   _loadOp; ///< RT0 로드 별칭
		RHIRenderPassLoadOp	   _arrLoadOps[kMaxColorAttachments];
		RHIRenderPassLoadOp	   _depthLoadOp;
		uint8				   _bBindColor	  : 1; ///< 0 = 깊이만 (ShadowMap 등)
		[[maybe_unused]] uint8 _reservedFlags : 7;

		/** @brief 스왑체인 RT0, Load 클리어 기본값. */
		RHIRenderPassBeginInfo() noexcept;
	};

	// ------------------------------------------------------------------------------
	// 6) VertexLayoutBuilder — 시맨틱 엘리먼트를 모아 Input Layout 구성
	// ------------------------------------------------------------------------------
	class SW_API VertexLayoutBuilder
	{
	public:
		/** @brief 빈 빌더. */
		VertexLayoutBuilder() = default;
		/** @brief 엘리먼트 목록을 복사합니다. */
		VertexLayoutBuilder( const VertexLayoutBuilder& ) = default;
		/** @brief 엘리먼트 목록을 이동합니다. */
		VertexLayoutBuilder( VertexLayoutBuilder&& ) = default;
		/** @brief 엘리먼트 목록을 복사 대입합니다. */
		VertexLayoutBuilder& operator=( const VertexLayoutBuilder& ) = default;
		/** @brief 엘리먼트 목록을 이동 대입합니다. */
		VertexLayoutBuilder& operator=( VertexLayoutBuilder&& ) = default;
		/** @brief 기본 소멸. */
		~VertexLayoutBuilder() = default;

		/** @brief 시맨틱 엘리먼트를 추가하고 *this를 반환합니다. */
		VertexLayoutBuilder& addElement( const utf8* pSemanticName, uint32 semanticIndex, RHIFormat format, uint32 offset, uint32 slot = 0 )
		{
			RHIInputElement elem{};
			elem._semanticName		= pSemanticName ? pSemanticName : "";
			elem._semanticIndex		= semanticIndex;
			elem._format			= format;
			elem._alignedByteOffset = offset;
			elem._inputSlot			= slot;
			_listElements.push_back( elem );
			return *this;
		}

		/** @brief 모은 엘리먼트 목록을 반환합니다. */
		const vector<RHIInputElement>& build() const { return _listElements; }

	private:
		vector<RHIInputElement> _listElements;
	};
} // namespace sw
