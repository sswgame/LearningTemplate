#pragma once
/**
 * @file TaskManager.h
 * @brief 멀티스레드 기반의 작업(Task) 큐를 관리하고 스케줄링하는 비동기 프레임워크입니다.
 * @details 방향성 비순환 그래프(DAG) 형태의 의존성 작업 실행을 지원합니다.
 */
#include "Core/CoreMinimal.h"
#include "Core/Utility/Task/TaskTypes.h"

namespace sw
{
	struct TaskNode;
	struct StageNode;

	/**
	 * @class TaskManager
	 * @brief 스레드 풀(Worker Threads)을 생성 및 관리하고, 대기열(Queue)에 등록된 태스크를 분배하여 비동기 실행을 처리합니다.
	 */
	class SW_API TaskManager
	{
	public:
		TaskManager()  = default;
		~TaskManager() = default;

		/**
		 * @brief 초기화합니다
		 */
		bool initialize( uint32 threadCount = 0 );

		/**
		 * @brief 활성화된 모든 워커 스레드의 실행을 중지하고 풀을 종료합니다.
		 */
		void shutdown();

		uint32 getWorkerCount() const { return static_cast<uint32>( _workers.size() ); }

	public:
		/**
		 * @brief 작업을 등록합니다
		 */
		TaskHandle emplaceTask( const TaskDelegate& delegate );

		/**
		 * @brief 디버깅 및 식별용 이름을 가진 기본 태스크를 추가합니다.
		 * @param name 태스크 이름
		 * @param delegate 실행할 델리게이트 함수
		 * @return 태스크 핸들
		 */
		TaskHandle emplaceTask( const std::string_view name, const TaskDelegate& delegate );

		/**
		 * @brief 임의의 매개변수를 받는 태스크를 추가합니다.
		 * @param delegate 매개변수를 인자로 받는 델리게이트 함수
		 * @param args 작업에 전달될 복합 인자들(TaskArgs)
		 * @return 태스크 핸들
		 */
		TaskHandle emplaceTask( const TaskArgsDelegate& delegate, const TaskArgs& args );

		/**
		 * @brief 작업을 등록합니다
		 */
		TaskHandle emplaceTask( const std::string_view name, const TaskArgsDelegate& delegate, const TaskArgs& args );

		// ============================================================================
		// [Parallel Task API]
		// ============================================================================

		/**
		 * @brief 주어진 횟수(count)만큼 분할되어 여러 스레드에서 병렬 처리되는 태스크 그룹을 추가합니다.
		 * @param count 작업을 나눌 단위 개수 (인덱스 0 ~ count-1)
		 * @param delegate 각 분할 작업별로 실행될 델리게이트 함수
		 * @return 루트(부모) 병렬 태스크 핸들
		 */
		TaskHandle emplaceParallel( uint32 count, const ParallelTaskDelegate& delegate );

		/**
		 * @brief 병렬 작업을 등록합니다
		 */
		TaskHandle emplaceParallel( const std::string_view name, uint32 count, const ParallelTaskDelegate& delegate );

		/**
		 * @brief emplaceParallelB내부 뮤텍스를 잠급니다
		 */
		TaskHandle emplaceParallelBlock( uint32 start, uint32 end, const ParallelBlockDelegate& delegate );

		/**
		 * @brief 작업 스테이지를 생성합니다
		 */
		TaskStageHandle createStage( const std::string_view stageName );

		/**
		 * @brief 스테이지 완료를 기다립니다
		 */
		void waitStage( TaskStageHandle stage );

		/**
		 * @brief 스테이지 완료 여부를 반환합니다
		 */
		bool isStageComplete( TaskStageHandle stage );

		/**
		 * @brief 대기 중인 작업을 디스패치합니다
		 */
		void dispatch();

		/**
		 * @brief 모든 작업 완료를 기다립니다
		 */
		void waitAll();

		/**
		 * @brief 내부 상태를 비웁니다
		 */
		void clear();

		/**
		 * @brief 모두 완료 시 연결합니다
		 */
		TaskHandle whenAll( const std::vector<TaskHandle>& tasks, const TaskDelegate& continuation );

		/**
		 * @brief 하나라도 완료 시 연결합니다
		 */
		TaskHandle whenAny( const std::vector<TaskHandle>& tasks, const TaskDelegate& continuation );

	private:
		/**
		 * @brief 워커 루프를 실행합니다
		 */
		void workerLoop( uint32 workerId );
		/**
		 * @brief 준비된 작업을 스케줄합니다
		 */
		void scheduleReadyTask( const std::shared_ptr<TaskNode>& node );
		/**
		 * @brief 작업 완료 콜백을 호출합니다
		 */
		void onTaskFinished( const std::shared_ptr<TaskNode>& node );
		/**
		 * @brief 작업 훔치기를 시도합니다
		 */
		std::shared_ptr<TaskNode> tryStealWorkNode();

	private:
		bool								  _bInitialized = false;
		std::atomic<bool>					  _bStop{ false };
		std::vector<std::thread>			  _workers;
		std::queue<std::shared_ptr<TaskNode>> _readyQueue;
		mutable std::mutex					  _queueMutex;
		std::condition_variable				  _cvWorker;
		std::condition_variable				  _cvWaitAll;

		std::vector<std::shared_ptr<TaskNode>>	_allNodes;
		std::vector<std::shared_ptr<StageNode>> _allStages;
		std::atomic<uint32>						_activeTaskCount{ 0 };
	};
} // namespace sw
