/**
 * @file FrameRenderer.h
 * @brief RenderPipeline XML을 로드하고 RenderGraph를 만든 뒤 Shadow/Forward/Deferred/Post를 실행합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/RenderPass/GpuScene.h"
#include "Engine/Graphics/RenderPass/RenderGraph.h"
#include "Engine/Graphics/RenderPass/RenderPipelineResource.h"

namespace sw
{
	class IRHIDevice;
	class IRHICommandList;
	class Material;
	class MaterialInstance;
	class Scene;
	class CameraComponent;
	class TaskManager;
	class TaskArgs;
	struct RenderFramePacket;

	/** @brief FrameRenderer 초기화/파이프라인 상태. */
	enum class FrameRendererStatus : uint8
	{
		Uninitialized = 0, ///< initialize 미호출
		Ready,			   ///< 파이프라인 로드·그래프 준비 완료
		Failed			   ///< initialize/loadPipeline 실패 (getStatusMessage)
	};

	/**
	 * @class FrameRenderer
	 * @brief 프레임 경로: RenderPipeline XML → RenderGraph → GpuScene / MeshComponents → IRHIDevice
	 */
	class SW_API FrameRenderer
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 수명 — 디바이스·파이프라인 XML, 핫패스 서비스
		// ------------------------------------------------------------------------------
		/** @brief 빈 렌더러. initialize 전에 사용하지 마세요. */
		FrameRenderer();
		/** @brief GPU 자원을 해제합니다. */
		~FrameRenderer();

		/** @brief 복사를 금지합니다. */
		FrameRenderer( const FrameRenderer& ) = delete;
		/** @brief 대입을 금지합니다. */
		FrameRenderer& operator=( const FrameRenderer& ) = delete;

		/** @brief 디바이스와 파이프라인 XML로 초기화합니다. */
		bool initialize( IRHIDevice* pDevice, string_view pipelineXmlPath = {} );
		/** @brief 디바이스·TaskManager·파이프라인 XML로 초기화합니다. */
		bool initialize( IRHIDevice* pDevice, TaskManager* pTaskManager,
						 string_view pipelineXmlPath = {} );
		/** @brief 핫패스 서비스(TaskManager)를 연결합니다. */
		void bindServices( TaskManager* pTaskManager );
		/** @brief GPU 자원을 해제하고 종료합니다. */
		void shutdown();

		// ------------------------------------------------------------------------------
		// 2) 파이프라인 · 실행 — XML 로드, execute / executePacket
		// ------------------------------------------------------------------------------
		/** @brief RenderPipeline XML에서 그래프를 다시 만듭니다 (동기 로드). 패스 콜백은 한 번 바인딩합니다. */
		bool loadPipeline( string_view pipelineXmlPath );
		/** @brief 컴파일된 그래프를 실행합니다. scene이 있으면 GpuScene을 구축합니다. */
		bool execute( IRHIDevice* pDevice, Material* pMaterial = nullptr, Scene* pScene = nullptr );
		/** @brief 렌더 스레드 경로: 미리 만든 packet.GpuScene을 씁니다 (Scene 미접근). */
		bool executePacket( IRHIDevice* pDevice, RenderFramePacket& packet );

		// ------------------------------------------------------------------------------
		// 3) 조회
		// ------------------------------------------------------------------------------
		/** @brief Ready 상태면 true. */
		bool isReady() const { return _status == FrameRendererStatus::Ready; }
		/** @brief 초기화/파이프라인 상태를 반환합니다. */
		FrameRendererStatus getStatus() const { return _status; }
		/** @brief Failed일 때 원인 메시지 (그 외 empty). */
		const string& getStatusMessage() const { return _statusMessage; }
		/** @brief 렌더 그래프를 반환합니다. */
		const RenderGraph& getGraph() const { return _graph; }
		/** @brief GpuScene을 반환합니다. */
		const GpuScene& getGpuScene() const { return _gpuScene; }
		GpuScene&		getGpuScene() { return _gpuScene; }

	private:
		/** @brief GPU 상수 버퍼 레이아웃. 필드 순서가 와이어 포맷이므로 함부로 바꾸지 마세요. */
		struct PassConstants
		{
			float32 _lightViewProj[16]{};
			float32 _viewProj[16]{};
			float32 _world[16]{};
			float32 _cascadeViewProj[4][16]{};
			float32 _cascadeSplits[4]{ 10.0f, 25.0f, 60.0f, 150.0f };
			float32 _keyLightDirIntensity[4]{ -0.35f, -0.85f, -0.25f, 1.35f };
			float32 _keyLightColor[4]{ 1.0f, 0.82f, 0.62f, 0.28f };
			float32 _shadowParams[4]{ 0.02f, 0.45f, 0.0f, 0.0f };
			float32 _bloomParams[4]{ 0.55f, 0.65f, 0.25f, 0.0f };
			float32 _outlineColor[4]{ 0.08f, 0.05f, 0.12f, 0.85f };
			float32 _outlineParams[4]{ 0.02f, 0.001f, 0.001f, 0.0f };
			uint32	_cascadeCount{ 4 };
			uint32	_texShadow		= 0xFFFFFFFFu;
			uint32	_texAlbedo		= 0xFFFFFFFFu;
			uint32	_texNormal		= 0xFFFFFFFFu;
			uint32	_texDepth		= 0xFFFFFFFFu;
			uint32	_texSource		= 0xFFFFFFFFu;
			uint32	_texSourceDepth = 0xFFFFFFFFu;
			uint32	_flags{ 0 };
		};

		// ------------------------------------------------------------------------------
		// 4) 패스 자원 · 콜백 · 드로우
		// ------------------------------------------------------------------------------
		/** @brief 패스용 상주 GPU 자원을 확보합니다. */
		void ensurePassResources();
		/** @brief 패스용 상주 GPU 자원을 해제합니다. */
		void releasePassResources();
		/** @brief 일시 텍스처를 확보합니다. */
		void ensureTransientResources();
		/** @brief 일시 텍스처를 해제합니다. */
		void releaseTransientResources();
		/** @brief 그래프 패스 콜백을 한 번 바인딩합니다. */
		void bindPassCallbacks();
		/** @brief RenderGraph 패스 실행 콜백. */
		void onGraphPassExecute( const RenderGraphPassContext& ctx );
		/** @brief 패스 타입에 맞는 실행을 수행합니다. */
		void executePass( string_view passType, string_view passName, Material* pMaterial );
		/** @brief 패스 상수 버퍼를 갱신합니다. */
		void updatePassConstants();
		/** @brief 카메라에서 뷰/투영을 적용합니다. */
		void applyViewFromCamera( CameraComponent* pCamera );
		/** @brief 키라이트 뷰-투영 행렬을 만듭니다. */
		void buildLightViewProj( float32 outMat[16] ) const;
		/** @brief 캐스케이드 섀도우 맵 뷰-투영 행렬 및 분할 거리를 계산합니다. */
		void buildCascadeShadowMatrices( float32 outCascadeMats[4][16], float32 outSplits[4] ) const;
		/** @brief 카메라 뷰-투영 행렬을 만듭니다. */
		void buildViewProj( float32 outMat[16] ) const;
		/** @brief 월드 행렬을 항등으로 둡니다. */
		void setIdentityWorld();
		/** @brief 씬 메시를 직접 그립니다. */
		void drawSceneMeshes( RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass );
		/** @brief GpuScene CPU 스냅샷을 월드 행렬 + draw()로 그립니다 (GPU-driven 꺼짐). */
		void drawGpuSceneMeshes( RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass );
		/** @brief GpuScene 배치를 간접 드로우로 그립니다. */
		void drawGpuBatches( RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass );
		/** @brief 풀스크린 삼각형을 그립니다. */
		void drawFullscreen( RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex );
		/** @brief 일시 텍스처를 할당합니다. */
		void allocTransient( string_view name, RHIFormat format, bool bDepth, const float32 arrClearColor[4] );
		/** @brief 컬러(+깊이) 패스를 시작합니다. */
		void beginColorPass( string_view colorName, string_view depthName, const float32 arrClearColor[4],
							 RHIRenderPassLoadOp colorLoad, RHIRenderPassLoadOp depthLoad );
		/** @brief MRT 컬러 패스를 시작합니다. */
		void beginColorPassMRT( const string* pColorNames, const float32 arrClearColors[][4],
								const RHIRenderPassLoadOp* pColorLoads, uint32 colorCount, string_view depthName,
								RHIRenderPassLoadOp depthLoad );
		/** @brief 깊이 전용 패스를 시작합니다. */
		void beginDepthOnlyPass( string_view depthName, float32 clearDepth, RHIRenderPassLoadOp depthLoad );
		/** @brief 패스 텍스처 인덱스를 설정합니다. */
		void setPassTexture( uint32& outIndex, string_view name );
		/** @brief 바인들리스 텍스처 바인딩을 커밋합니다. */
		void commitBindlessTextureBindings();
		/** @brief 패스 텍스처 인덱스를 지웁니다. */
		void clearPassTextureIndices();

		// ------------------------------------------------------------------------------
		// 5) 커맨드 리스트 · 그래프 제출
		// ------------------------------------------------------------------------------
		/**
		 * @brief execute() / executePacket() 공통: commandList를 준비하고 device에 연결합니다.
		 * @param pCallerName 오류 로그 식별용 호출자 이름
		 * @return 성공 시 true; false면 호출자가 즉시 반환해야 합니다.
		 */
		bool prepareCommandList( IRHIDevice* pDevice, const utf8* pCallerName );

		/**
		 * @brief execute() / executePacket() 공통: graph를 실행하고 commandList를 제출합니다.
		 * @return graph.execute() 결과
		 */
		bool submitGraph( IRHIDevice* pDevice );

		// ------------------------------------------------------------------------------
		// 6) 어태치먼트 · PSO
		// ------------------------------------------------------------------------------
		/** @brief 어태치먼트 클리어 색을 찾습니다. */
		bool tryGetAttachmentClearColor( string_view attachmentName, float32 outClearColor[4] ) const;
		/** @brief 일시 텍스처 핸들을 찾습니다. */
		RHITextureHandle findTransient( string_view name ) const;
		/** @brief 일시 텍스처 SRV를 찾습니다. */
		RHIDescriptorIndex findTransientSrv( string_view name ) const;
		/** @brief 포맷 이름을 RHIFormat으로 해석합니다. */
		RHIFormat parseAttachmentFormat( string_view formatName ) const;
		/** @brief Present 소스 어태치먼트 이름을 결정합니다. */
		string resolvePresentSource() const;
		/** @brief 패스 타입으로 파이프라인 패스 서술을 찾습니다. */
		const RenderGraphPassDesc* findPassDescByType( string_view passType ) const;

		/** @brief 엔진 기본 PSO를 만듭니다. */
		RHIPipelineStateHandle createEnginePso( string_view shaderPath, bool bDepthTest, uint32 numRenderTargets = 1,
												const RHIFormat* pRtvFormats = nullptr, bool bBlend = false,
												bool bDepthWrite = true );
		/** @brief 파이프라인 XML 패스 레시피로 PSO를 만들고, 없으면 타입 기본값을 씁니다. */
		RHIPipelineStateHandle createPsoForPassType( string_view passType, string_view defaultShader,
													 bool bDepthTest, uint32 numRenderTargets = 1,
													 const RHIFormat* pRtvFormats = nullptr, bool bDefaultBlend = false,
													 bool				   bDefaultDepthWrite = true,
													 const vector<string>* pExtraDefines	  = nullptr );
		/**
		 * @brief 패스 레시피 PSO에 Material/MaterialInstance permutation define을 합칩니다 (캐시).
		 */
		RHIPipelineStateHandle getOrCreateMaterialPassPso( string_view passType, string_view defaultShader,
														   bool bDepthTest, Material* pMaterial,
														   MaterialInstance* pMaterialInstance = nullptr,
														   uint32 numRenderTargets = 1, const RHIFormat* pRtvFormats = nullptr,
														   bool bDefaultBlend = false, bool bDefaultDepthWrite = true );
		/** @brief passType 키로 엔진 내장 PSO를 조회합니다. 없으면 0 반환. */
		RHIPipelineStateHandle getEnginePso( string_view passType ) const;

	private:
		/** @brief TaskArgs: passType, defaultShader, depth, numRT, rtvFormats, blend, depthWrite, defines, cacheKey. */
		void compileMaterialPsoTask( const TaskArgs& args );

	private:
		IRHIDevice*									  _pDevice;
		IRHIDevice*									  _pCmdOwnerDevice;
		unique_ptr<IRHICommandList>					  _frameCmd;
		IRHICommandList*							  _pCmd;
		Scene*										  _pScene;
		TaskManager*								  _pTaskManager;
		GpuScene									  _gpuScene;
		RenderPipelineResource						  _pipelineResource;
		RenderGraph									  _graph;
		string										  _pipelinePath;
		float32										  _arrClearColor[4];
		unordered_map<string, RHITextureHandle>		  _mapTransients;
		unordered_map<string, RHIDescriptorIndex>	  _mapTransientSrvs;
		vector<hashed_string>						  _listClearedThisFrame;
		Material*									  _pBoundMaterial;
		PassConstants								  _passConstants;
		RHIBufferHandle								  _passCb;
		RHIDescriptorIndex							  _passCbIndex;
		RHIBufferHandle								  _gpuCullCb;
		RHIDescriptorIndex							  _gpuCullCbIndex;
		unordered_map<string, RHIPipelineStateHandle> _mapEnginePsos;
		unordered_map<uint64, RHIPipelineStateHandle> _mapMaterialPassPsos;
		mutex										  _psoMutex;
		uint32										  _transientWidth;
		uint32										  _transientHeight;
		RHITextureHandle							  _outputRenderTarget;
		RHITextureHandle							  _taaHistory;	  ///< TAA resolve history (ping copy of last TaaColor)
		RHIDescriptorIndex							  _taaHistorySrv; ///< `_taaHistory` bindless SRV (프레임마다 재등록하지 않음)
		FrameRendererStatus							  _status;
		string										  _statusMessage;
		uint8										  _bCallbacksBound			: 1;
		uint8										  _bPassResourcesReady		: 1;
		uint8										  _bSceneTransformsFlushed	: 1; ///< CPU 드로우 경로: execute당 한 번 flush
		uint8										  _bHasExecutedDepthPrepass : 1;
		[[maybe_unused]] uint8						  _reservedFlags			: 4;
		bool										  _bUseGpuDriven;
		RenderGraphExecutionContext					  _graphContext;
	};
} // namespace sw
