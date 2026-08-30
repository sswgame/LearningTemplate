/**
 * @file GpuScene.h
 * @brief GPU 컬·간접 드로우용 MeshComponent CPU 스냅샷.
 */
#pragma once
#include "Core/Container/unordered_set.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
	class Scene;
	class IRHIDevice;
	class Mesh;
	class Material;
	class MaterialInstance;
	class TaskManager;
	class MeshComponent;

	/// @brief GPU 인스턴스 (월드 행렬 + 메시/머티리얼 인덱스)
	struct GpuInstance
	{
		float4x4 _world{};
		float3	 _boundsCenter{};
		float32	 _boundsRadius{ 1.0f };
		uint32	 _meshBatchIndex{ 0 };
		uint32	 _materialIndex{ 0 };
		uint32	 _blendMode{ 0 }; ///< RHIBlendMode
		uint32	 _pad{ 0 };
	};

	/// @brief 같은 메시/머티리얼의 인스턴스 배치
	struct GpuMeshBatch
	{
		Mesh*			   _pMesh{ nullptr };
		RHIBufferHandle	   _vertexBuffer{ 0 };
		uint32			   _vertexCount{ 0 };
		uint32			   _instanceBase{ 0 };
		uint32			   _instanceCount{ 0 };
		uint32			   _materialIndex{ 0 };
		RHIBlendMode	   _blendMode  = RHIBlendMode::Opaque;
		RHIDescriptorIndex _materialCb = kInvalidDescriptorIndex;
		/** @brief RT가 draw 직전에 applyToGpu. 수명은 GpuScene pin/retire 큐로 관리. */
		MaterialInstance* _pMaterialInstance{ nullptr };
	};

	/**
	 * @class GpuMaterialRetireQueue
	 * @brief GT→RT 교차 MaterialInstance 수명 정책.
	 * @details build 배치에 실린 인스턴스는 pin. 배치에서 빠지면 retire(프레임 지연).
	 *          GT는 isPinned이면 파괴하지 말고, flushAfterGpu 이후에 파괴합니다.
	 */
	class SW_API GpuMaterialRetireQueue
	{
	public:
		static constexpr uint32 kRetireFrameDelay = 2;

		/** @brief 현재 배치에 실린 인스턴스를 pin하고, 빠진 것은 retire 큐로 옮깁니다. */
		void syncFromBatches( const vector<GpuMeshBatch>& listOpaque, const vector<GpuMeshBatch>& listTransparent );
		/** @brief RT 프레임 종료 시 호출 — retire 카운트를 줄입니다 (waitIdle 없음). */
		void advanceFrame();
		/** @brief device.waitIdle() 후 pin/retire를 모두 비웁니다. */
		void flushAfterGpu( IRHIDevice* pDevice );
		/** @brief GPU 경로가 아직 참조 중이면 true. */
		bool isPinned( const MaterialInstance* pInstance ) const;
		/** @brief pin·retire를 즉시 비웁니다 (GPU sync 없음). */
		void clear();

	private:
		struct RetireEntry
		{
			MaterialInstance* _pInstance{ nullptr };
			uint32			  _framesLeft{ 0 };
		};

		unordered_set<MaterialInstance*> _uniquePinned;
		vector<RetireEntry>				 _listRetiring;
	};

	struct GpuSceneDrawCandidate
	{
		float4x4		  _world{};
		float3			  _boundsCenter{};
		float32			  _boundsRadius{ 1.0f };
		Mesh*			  _pMesh{ nullptr };
		Material*		  _pMaterial{ nullptr };
		MaterialInstance* _pInstance{ nullptr };
		uint32			  _blendMode{ 0 };
	};

	struct GpuSceneSortKey
	{
		Mesh*			  _pMesh{ nullptr };
		Material*		  _pMaterial{ nullptr };
		MaterialInstance* _pInstance{ nullptr };
		/** @brief 메시·머티리얼·인스턴스가 같은지 비교합니다. */
		bool operator==( const GpuSceneSortKey& o ) const { return _pMesh == o._pMesh && _pMaterial == o._pMaterial && _pInstance == o._pInstance; }
	};

	struct GpuSceneSortEntry
	{
		GpuSceneSortKey _key;
		uint32			_srcIdx{ 0 };
	};

	/**
	 * @class GpuScene
	 * @brief 게임 스레드에서 구축(선택적 TaskManager)하고 렌더 스레드에서 소비합니다.
	 */
	class SW_API GpuScene
	{
	public:
		GpuScene() noexcept = default;
		~GpuScene()			= default;

		GpuScene( GpuScene&& other ) noexcept			 = default;
		GpuScene& operator=( GpuScene&& other ) noexcept = default;

		GpuScene( const GpuScene& )			   = delete;
		GpuScene& operator=( const GpuScene& ) = delete;

		/** @brief CPU/GPU 스냅샷을 비웁니다. */
		void clear();
		/**
		 * @brief MeshComponent를 수집합니다. 개수가 많으면 TaskManager 병렬 샤드를 씁니다.
		 * @param pTaskManager 선택적 병렬 구축. null이면 단일 스레드 수집
		 * @details 내용·카메라가 이전과 같으면 재구축을 건너뜁니다. 카메라만 바뀌면
		 *          transparent 재정렬 + 배치 재구성만 합니다.
		 */
		void buildFromScene( Scene* pScene, const float3& cameraPos,
							 TaskManager* pTaskManager = nullptr );
		/** @brief 인스턴스 SRV와 배치별 간접 인자를 업로드합니다 (RT/디바이스 스레드). */
		bool upload( IRHIDevice* pDevice );
		/** @brief GPU 버퍼를 해제합니다. */
		void releaseGpu( IRHIDevice* pDevice );

		/** @brief 인스턴스 목록을 반환합니다. */
		const vector<GpuInstance>& getInstances() const { return _listInstance; }
		/** @brief 불투명 배치를 반환합니다. */
		const vector<GpuMeshBatch>& getOpaqueBatches() const { return _listOpaqueBatch; }
		/** @brief 투명 배치를 반환합니다. */
		const vector<GpuMeshBatch>& getTransparentBatches() const { return _listTransparentBatch; }
		/** @brief 인스턴스 버퍼 핸들을 반환합니다. */
		RHIBufferHandle getInstanceBuffer() const { return _instanceBuffer; }
		/** @brief 인스턴스 SRV 인덱스를 반환합니다. */
		RHIDescriptorIndex getInstanceSrv() const { return _instanceSrv; }
		/** @brief 간접 인자 버퍼 핸들을 반환합니다. */
		RHIBufferHandle getIndirectArgsBuffer() const { return _indirectArgsBuffer; }
		/** @brief 간접 인자 UAV 인덱스를 반환합니다. */
		RHIDescriptorIndex getIndirectArgsUav() const { return _indirectArgsUav; }
		/** @brief 간접 커맨드 개수를 반환합니다. */
		uint32 getIndirectCommandCount() const { return _indirectCommandCount; }
		/** @brief GPU에 올라갔는지 반환합니다. */
		bool isUploaded() const { return _instanceBuffer != 0; }
		/** @brief 마지막 buildFromScene이 CPU 스냅샷을 바꿨으면 true. */
		bool isCpuSnapshotDirty() const { return _bCpuDirty != 0; }

		/** @brief 배치 MaterialInstance pin/retire 정책. */
		GpuMaterialRetireQueue& getMaterialRetireQueue() { return _materialRetire; }
		/** @brief 배치 MaterialInstance pin/retire 정책. */
		const GpuMaterialRetireQueue& getMaterialRetireQueue() const { return _materialRetire; }
		/** @brief 현재 배치로 pin을 맞추고, RT 프레임 끝에서 advanceFrame을 호출하세요. */
		void syncMaterialPins();
		/** @brief RT 프레임 종료 — retire 지연 카운트. */
		void advanceMaterialRetireFrame() { _materialRetire.advanceFrame(); }
		/** @brief waitIdle 후 pin/retire 전부 해제 (핫스왑·셧다운). */
		void flushMaterialRetire( IRHIDevice* pDevice ) { _materialRetire.flushAfterGpu( pDevice ); }

	private:
		/** @brief 후보를 GpuInstance scratch로 채웁니다. ParallelBlockDelegate 시그니처입니다. */
		void fillScratchRange( uint32 start, uint32 end );
		/** @brief 수집된 인스턴스를 배치로 묶습니다. */
		void buildBatches();
		/** @brief 투명 인덱스를 카메라 거리순(먼→가까운)으로 정렬합니다. */
		void sortTransparent( const float32* pCameraPos );
		/** @brief opaque/transparent 인덱스 테이블을 후보에서 다시 만듭니다. */
		void rebuildPartitionTables();
		/** @brief 후보 내용 핑거프린트. */
		uint64 hashCandidates() const;
		/** @brief 캐시 무효화. */
		void invalidateBuildCache();

		vector<GpuInstance>	 _listInstance;
		vector<GpuMeshBatch> _listOpaqueBatch;
		vector<GpuMeshBatch> _listTransparentBatch;
		vector<GpuMeshBatch> _listAllBatch; ///< 불투명 다음 투명. 간접 슬롯과 일치

		/// buildFromScene에서 재사용해 프레임당 힙 할당을 줄입니다.
		struct DrawCandidate
		{
			float4x4		  _world{};
			float3			  _boundsCenter{};
			float32			  _boundsRadius{ 1.0f };
			Mesh*			  _pMesh{ nullptr };
			Material*		  _pMaterial{ nullptr };
			MaterialInstance* _pInstance{ nullptr };
			uint32			  _blendMode{ 0 };
		};

		vector<DrawCandidate> _listScratchCandidate;
		vector<GpuInstance>	  _listScratchRaw;

		struct SortKey
		{
			Mesh*			  _pMesh{ nullptr };
			Material*		  _pMaterial{ nullptr };
			MaterialInstance* _pInstance{ nullptr };
			/** @brief 메시·머티리얼·인스턴스가 같은지 비교합니다. */
			bool operator==( const SortKey& o ) const { return _pMesh == o._pMesh && _pMaterial == o._pMaterial && _pInstance == o._pInstance; }
		};

		struct SortEntry
		{
			SortKey _key;
			uint32	_srcIdx{ 0 };
		};

		vector<SortEntry> _listScratchOpaqueEntry;
		vector<uint32>	  _listScratchTransparentIdx;

		GpuMaterialRetireQueue _materialRetire;
		TaskStageHandle		   _snapshotStage;
		RHIBufferHandle		   _instanceBuffer{ 0 };
		RHIBufferHandle		   _indirectArgsBuffer{ 0 };
		uint64				   _lastContentHash{ 0 };
		float3				   _lastCameraPos{};
		RHIDescriptorIndex	   _instanceSrv		= kInvalidDescriptorIndex;
		RHIDescriptorIndex	   _indirectArgsUav = kInvalidDescriptorIndex;
		uint32				   _indirectCommandCount{ 0 };
		uint32				   _instanceCapacity{ 0 };
		uint32				   _argsCapacity{ 0 };
		uint8				   _bHasBuildCache{ 0 };
		uint8				   _bCpuDirty{ 1 };
	};
} // namespace sw
