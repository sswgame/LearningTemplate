#pragma once
#include "Core/Common/Common.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/String/hashed_string.h"

/**
 * @file RHITypes.h
 * @brief Render Hardware Interface (RHI) 공통 데이터 타입, 열거형 및 구조체 정의
 */

namespace sw
{
	/** @brief GPU 버퍼 리소스에 대한 64비트 정수 핸들 */
	using RHIBufferHandle								 = uint64;

	/** @brief GPU 텍스처 리소스에 대한 64비트 정수 핸들 */
	using RHITextureHandle								 = uint64;

	/** @brief Bindless 리소스 인덱스 타입 (Descriptor Heap / Set 내 바인딩 인덱스) */
	using RHIDescriptorIndex							 = uint32;

	/** @brief 유효하지 않은 Descriptor 인덱스 상수 */
	constexpr RHIDescriptorIndex kInvalidDescriptorIndex = invalid_index::kUint32;

	/**
	 * @struct RHIVertex
	 * @brief 기본적인 3D 정점 구조체 (직접 그리기 예제용)
	 */
	struct RHIVertex
	{
		float32 position[3]; ///< 정점 위치 (X, Y, Z)
		float32 color[4];	 ///< 정점 색상 (R, G, B, A)
	};

	/**
	 * @enum RHIBackend
	 * @brief 엔진이 지원하는 크로스 플랫폼 Graphics API 백엔드 열거형
	 */
	ENUM()
	enum class RHIBackend : uint32
	{
		DirectX11 = 0, ///< Direct3D 11 API 백엔드
		DirectX12 = 1, ///< Direct3D 12 API 백엔드 (Bindless 지원)
		Vulkan	  = 2, ///< Vulkan 1.3 API 백엔드 (Bindless 지원)
		OpenGL	  = 3, ///< OpenGL 4.5+ API 백엔드

		D3D11 = DirectX11,
		D3D12 = DirectX12
	};

	/**
	 * @enum RHIFormat
	 * @brief 텍스처 및 렌더 타깃, 픽셀 데이터 포맷
	 */
	ENUM()
	enum class RHIFormat : uint32
	{
		R8G8B8A8_UNORM	   = 0, ///< 8비트 RGBA 정규화 포맷
		B8G8R8A8_UNORM	   = 1, ///< 8비트 BGRA 정규화 포맷 (DirectX 기본)
		R16G16B16A16_FLOAT = 2, ///< 16비트 부동소수점 RGBA (HDR 타깃용)
		D24_UNORM_S8_UINT  = 3, ///< 24비트 깊이 + 8비트 스텐실 포맷
		R32G32B32_FLOAT	   = 4, ///< 32비트 부동소수점 RGB (위치/노멀)
		R32G32_FLOAT	   = 5, ///< 32비트 부동소수점 RG (UV 좌표)
		R32_FLOAT		   = 6, ///< 32비트 단일 부동소수점 (단일 채널 깊이/스칼라)
	};

	/**
	 * @struct RHIInputElement
	 * @brief 정점 레이아웃(Input Layout) 서술 엘리먼트
	 */
	struct RHIInputElement
	{
		std::string _semanticName;                             ///< 시맨틱 이름 (POSITION, COLOR 등)
		uint32		_semanticIndex	   = 0;                     ///< 시맨틱 인덱스
		RHIFormat	_format			   = RHIFormat::R32G32B32_FLOAT; ///< 엘리먼트 데이터 포맷
		uint32		_alignedByteOffset = 0;                     ///< 버텍스 구조체 내 바이트 오프셋
		uint32		_inputSlot		   = 0;                     ///< 버텍스 버퍼 입력 슬롯
	};

	/**
	 * @struct RHISwapChainDesc
	 * @brief 윈도우 스왑체인 생성을 위한 설정 구조체
	 */
	REFLECT()
	struct RHISwapChainDesc
	{
		PROPERTY()
		void* _windowHandle = nullptr; ///< OS 윈도우 핸들 (HWND, Window XID 등)

		PROPERTY()
		void* _windowDisplay = nullptr; ///< X11 Display 포인터 (리눅스 전용)

		PROPERTY()
		uint32 _width = 1280; ///< 스왑체인 너비 (픽셀)

		PROPERTY()
		uint32 _height = 720; ///< 스왑체인 높이 (픽셀)

		PROPERTY()
		uint32 _bufferCount = 2; ///< 프레임버퍼 갯수 (Double/Triple Buffering)

		PROPERTY()
		bool _vsync = true; ///< 수직 동기화 여부

		PROPERTY()
		bool _fullscreen = false; ///< 전체 화면 여부
	};

	/**
	 * @struct RHIViewport
	 * @brief 렌더링 뷰포트 영역 정의
	 */
	REFLECT()
	struct RHIViewport
	{
		PROPERTY()
		float32 _x = 0.0f; ///< 뷰포트 좌상단 X 좌표

		PROPERTY()
		float32 _y = 0.0f; ///< 뷰포트 좌상단 Y 좌표

		PROPERTY()
		float32 _width = 1280.0f; ///< 뷰포트 너비

		PROPERTY()
		float32 _height = 720.0f; ///< 뷰포트 높이

		PROPERTY()
		float32 _minDepth = 0.0f; ///< 최소 깊이 값 (0.0~1.0)

		PROPERTY()
		float32 _maxDepth = 1.0f; ///< 최대 깊이 값 (0.0~1.0)
	};

	/**
	 * @struct RHIDrawIndirectCommand
	 * @brief 간접 드로우(Indirect Draw) 파라미터 구조체
	 */
	REFLECT()
	struct RHIDrawIndirectCommand
	{
		PROPERTY()
		uint32 _vertexCount = 3; ///< 정점 개수

		PROPERTY()
		uint32 _instanceCount = 1; ///< 인스턴스 개수

		PROPERTY()
		uint32 _startVertexLocation = 0; ///< 시작 정점 위치

		PROPERTY()
		uint32 _startInstanceLocation = 0; ///< 시작 인스턴스 위치
	};

	/**
	 * @struct RHIDispatchIndirectCommand
	 * @brief 간접 컴퓨트 디스패치(Indirect Dispatch) 파라미터 구조체
	 */
	REFLECT()
	struct RHIDispatchIndirectCommand
	{
		PROPERTY()
		uint32 _threadGroupCountX = 1; ///< X 축 스레드 그룹 개수

		PROPERTY()
		uint32 _threadGroupCountY = 1; ///< Y 축 스레드 그룹 개수

		PROPERTY()
		uint32 _threadGroupCountZ = 1; ///< Z 축 스레드 그룹 개수
	};

	/** @brief 파이프라인 상태 객체(PSO) 64비트 핸들 */
	using RHIPipelineStateHandle = uint64;

	/** @brief 렌더 패스 객체 64비트 핸들 */
	using RHIRenderPassHandle	 = uint64;

	/** @brief 텍스처 객체 64비트 핸들 */
	using RHITextureHandle		 = uint64;

	/**
	 * @enum RHIPrimitiveTopology
	 * @brief 도형 출력 위상(Topology)
	 */
	enum class RHIPrimitiveTopology
	{
		TriangleList, ///< 삼각형 리스트
		LineList,     ///< 선 리스트
		PointList     ///< 점 리스트
	};

	/**
	 * @enum RHIFillMode
	 * @brief 래스터라이저 와이어프레임 / 솔리드 채우기 모드
	 */
	enum class RHIFillMode
	{
		Solid,     ///< 일반 채우기
		Wireframe  ///< 와이어프레임 표시
	};

	/**
	 * @enum RHICullMode
	 * @brief 페이스 컬링 방식
	 */
	enum class RHICullMode
	{
		None,  ///< 컬링 없음
		Front, ///< 전면 컬링
		Back   ///< 후면 컬링
	};

	/**
	 * @enum RHIRenderPassLoadOp
	 * @brief 렌더 패스 시작 시 프레임버퍼 데이터 로드 동작
	 */
	enum class RHIRenderPassLoadOp
	{
		Clear,   ///< 기존 데이터 지우기 (Clear Color)
		Load,    ///< 기존 데이터 유지하기
		DontCare ///< 이전 내용 무시 (최적화)
	};

	/**
	 * @enum RHIRenderPassStoreOp
	 * @brief 렌더 패스 완료 시 프레임버퍼 데이터 저장 동작
	 */
	enum class RHIRenderPassStoreOp
	{
		Store,   ///< 메모리에 최종 결과 저장
		DontCare ///< 결과 보존 안 함
	};

	/**
	 * @struct RHIPipelineStateDesc
	 * @brief Graphics & Compute 파이프라인 상태 생성 서술체
	 */
	struct RHIPipelineStateDesc
	{
		RHIPipelineStateDesc() noexcept;

		std::string _vertexShaderPath;                  ///< 버텍스 셰이더 소스 경로
		std::string _vertexEntryPoint = "VSMain";        ///< 버텍스 셰이더 진입점
		std::string _pixelShaderPath;                   ///< 픽셀 셰이더 소스 경로
		std::string _pixelEntryPoint = "PSMain";         ///< 픽셀 셰이더 진입점
		std::string _computeShaderPath;                 ///< 컴퓨트 셰이더 소스 경로
		std::string _computeEntryPoint = "CSMain";       ///< 컴퓨트 셰이더 진입점

		RHIPrimitiveTopology _topology = RHIPrimitiveTopology::TriangleList; ///< 프리미티브 위상
		RHIFillMode			 _fillMode = RHIFillMode::Solid;				 ///< 채우기 모드
		RHICullMode			 _cullMode = RHICullMode::None;					 ///< 컬링 모드
		uint8				 _bEnableDepthTest : 1;						 ///< 깊이 테스트 활성화 여부
		uint8				 _bEnableBlend	   : 1;						 ///< 알파 블렌딩 활성화 여부
		[[maybe_unused]] uint8				 _reservedFlags	   : 6;
	};

	/**
	 * @struct RHIRenderPassAttachment
	 * @brief 렌더 패스 내 색상/깊이 어태치먼트 서술 구조체
	 */
	struct RHIRenderPassAttachment
	{
		RHIFormat			 _format		= RHIFormat::R8G8B8A8_UNORM;       ///< 어태치먼트 포맷
		RHIRenderPassLoadOp	 _loadOp		= RHIRenderPassLoadOp::Clear;      ///< 로드 동작
		RHIRenderPassStoreOp _storeOp		= RHIRenderPassStoreOp::Store;     ///< 저장 동작
		float32				 _clearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };    ///< 초기화 색상 (RGBA)
	};

	/**
	 * @struct RHIRenderPassDesc
	 * @brief 렌더 패스 생성 정보
	 */
	struct RHIRenderPassDesc
	{
		RHIRenderPassDesc() noexcept;

		std::vector<RHIRenderPassAttachment> _colorAttachments; ///< 색상 어태치먼트 목록
		float32								 _clearDepth   = 1.0f; ///< 깊이 초기화 값
		uint8								 _clearStencil = 0;	   ///< 스텐실 초기화 값
		uint8								 _bHasDepthStencil : 1; ///< 깊이/스텐실 어태치먼트 포함 여부
		[[maybe_unused]] uint8								 _reservedFlags	   : 7;
	};

	/**
	 * @struct RHITextureDesc
	 * @brief 텍스처 생성 서술 구조체
	 */
	struct RHITextureDesc
	{
		RHITextureDesc() noexcept;

		uint32	  _width	 = 1;
		uint32	  _height	 = 1;
		uint32	  _depth	 = 1;
		uint32	  _mipLevels = 1;
		RHIFormat _format	 = RHIFormat::R8G8B8A8_UNORM;

		float32 _clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		float32 _clearDepth	   = 1.0f;
		uint8	_clearStencil  = 0;

		uint8 _bIsRenderTarget	  : 1;
		uint8 _bIsDepthStencil	  : 1;
		uint8 _bIsShaderResource  : 1;
		uint8 _bIsUnorderedAccess : 1;
		[[maybe_unused]] uint8 _reservedFlags	  : 4;
	};

	/**
	 * @struct RHIRenderPassBeginInfo
	 * @brief 렌더 패스 바인딩 및 수행 시작 인자
	 */
		struct RHIRenderPassBeginInfo
	{
		RHIRenderPassHandle _renderPass		= 0;
		RHITextureHandle	_colorTarget	= 0; ///< 0 = swapchain backbuffer
		uint32				_width			= 0;
		uint32				_height			= 0;
		float32				_clearColor[4]	= { 0.1f, 0.1f, 0.1f, 1.0f };
	};

	class VertexLayoutBuilder
	{
	public:
		VertexLayoutBuilder()  = default;
		~VertexLayoutBuilder() = default;

		VertexLayoutBuilder& addElement( const utf8* semanticName, uint32 semanticIndex, RHIFormat format, uint32 offset, uint32 slot = 0 )
		{
			RHIInputElement elem{};
			elem._semanticName		= semanticName ? semanticName : "";
			elem._semanticIndex		= semanticIndex;
			elem._format			= format;
			elem._alignedByteOffset = offset;
			elem._inputSlot			= slot;
			_elements.push_back( elem );
			return *this;
		}

		const std::vector<RHIInputElement>& build() const
		{
			return _elements;
		}

	private:
		std::vector<RHIInputElement> _elements;
	};
}
