/**
 * @file TaskTypes.h
 * @brief 비동기 태스크 시스템에서 사용하는 핸들, 인자, 델리게이트 및 열거형 타입들을 정의합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/StringUtil.h"

namespace sw
{
	/**
	 * @class TaskValue
	 * @brief 이종(Heterogeneous) 데이터 타입을 런타임에 안전하게 보관하는 타입 소거(Type Erasure) 컨테이너입니다.
	 * @details
	 * - **SBO (Small Buffer Optimization, 32바이트)**:
	 *   기본형(int32, float32, 포인터 등) 및 32바이트 이하의 소형 구조체는 별도의 힙 메모리 할당(new/delete) 없이
	 *   인라인 스택 버퍼(`_storage`)에 즉시 저장하여 고성능 0-Alloc을 보장합니다.
	 * - **큰 객체 자동 힙 할당**:
	 *   32바이트를 초과하는 대형 객체는 자동으로 힙에 할당하고 포인터로 관리합니다.
	 * - **소멸/복사/이동 VTable**:
	 *   C++ 가상 함수 테이블 오버헤드 대신 함수 포인터 4개로 구성된 정적 VTable을 통해 가볍게 수명주기를 관리합니다.
	 * - **타입 안전성**:
	 *   `getPtr<T>()`로 값을 읽어오며, Debug 모드에서는 `typeid`를 비교하여 잘못된 타입 접근 시 즉시 어설션을 발생시킵니다.
	 */
	class TaskValue
	{
		/** @brief 인라인으로 보관 가능한 최대 바이트 크기 (32바이트) */
		static constexpr size_t kInlineStorageSize = 32;

		/** @brief 해당 타입 T가 32바이트 인라인 버퍼에 들어갈 수 있는지 컴파일 타임에 판별합니다. */
		template <typename T>
		static constexpr bool kIsInline = ( sizeof( T ) <= kInlineStorageSize && alignof( T ) <= alignof( std::max_align_t ) );

		/** @brief 타입별 소멸, 복사 생성, 이동 생성을 관리하는 가상 함수 테이블 구조체 */
		struct VTable
		{
			void ( *_pDestroy )( void* pStorage );			   ///< 객체 소멸자 호출 함수
			void ( *_pClone )( const void* pSrc, void* pDst ); ///< 복사 생성 함수
			void ( *_pMove )( void* pSrc, void* pDst );		   ///< 이동 생성 함수
			size_t _typeSize{ 0 };
			bool   _bIsInline{ false };
		};

		template <typename T>
		static const VTable* getInlineVTable()
		{
			static constexpr VTable s_pVtable{
				[]( void* pStorage )
			{
				reinterpret_cast<T*>( pStorage )->~T();
			},
				[]( const void* pSrc, void* pDst )
			{
				new ( pDst ) T( *reinterpret_cast<const T*>( pSrc ) );
			},
				[]( void* pSrc, void* pDst )
			{
				new ( pDst ) T( std::move( *reinterpret_cast<T*>( pSrc ) ) );
				reinterpret_cast<T*>( pSrc )->~T();
			},
				sizeof( T ),
				true };
			return &s_pVtable;
		}

		template <typename T>
		static const VTable* getHeapVTable()
		{
			static constexpr VTable s_pVtable{
				[]( void* pStorage )
			{
				T* pPtr = *reinterpret_cast<T**>( pStorage );
				sw_delete( pPtr );
			},
				[]( const void* pSrc, void* pDst )
			{
				const T*								pSource = *reinterpret_cast<const T* const*>( pSrc );
				*reinterpret_cast<T**>( pDst )					= sw_new T( *pSource );
			},
				[]( void* pSrc, void* pDst )
			{
				*reinterpret_cast<T**>( pDst ) = *reinterpret_cast<T**>( pSrc );
				*reinterpret_cast<T**>( pSrc ) = nullptr;
			},
				sizeof( T ),
				false };
			return &s_pVtable;
		}

	public:
		/** @brief 빈 값 (저장소 없음). */
		TaskValue() = default;

		~TaskValue()
		{
			reset();
		}

		TaskValue( const TaskValue& other )
		{
			if ( other._pVtable != nullptr )
			{
				_pVtable = other._pVtable;
				_pVtable->_pClone( other._arrStorage, _arrStorage );
			}
		}

		TaskValue( TaskValue&& other ) noexcept
		{
			if ( other._pVtable != nullptr )
			{
				_pVtable = other._pVtable;
				_pVtable->_pMove( other._arrStorage, _arrStorage );
				other._pVtable = nullptr;
			}
		}

		TaskValue& operator=( const TaskValue& other )
		{
			if ( this != &other )
			{
				reset();
				if ( other._pVtable != nullptr )
				{
					_pVtable = other._pVtable;
					_pVtable->_pClone( other._arrStorage, _arrStorage );
				}
			}
			return *this;
		}

		TaskValue& operator=( TaskValue&& other ) noexcept
		{
			if ( this != &other )
			{
				reset();
				if ( other._pVtable != nullptr )
				{
					_pVtable = other._pVtable;
					_pVtable->_pMove( other._arrStorage, _arrStorage );
					other._pVtable = nullptr;
				}
			}
			return *this;
		}

		template <typename T,
				  typename Decayed = std::decay_t<T>,
				  typename		   = std::enable_if_t<!std::is_same_v<Decayed, TaskValue>>>
		TaskValue( T&& value )
		{
			if constexpr ( kIsInline<Decayed> )
			{
				_pVtable = getInlineVTable<Decayed>();
				new ( _arrStorage ) Decayed( std::forward<T>( value ) );
			}
			else
			{
				_pVtable									= getHeapVTable<Decayed>();
				*reinterpret_cast<Decayed**>( _arrStorage ) = sw_new Decayed( std::forward<T>( value ) );
			}
		}

		void reset()
		{
			if ( _pVtable != nullptr )
			{
				_pVtable->_pDestroy( _arrStorage );
				_pVtable = nullptr;
			}
		}

		bool hasValue() const { return _pVtable != nullptr; }

		/** @brief 저장 값 포인터. 타입 불일치는 Debug assert. */
		template <typename T>
		const T* getPtr() const
		{
			using Stored = std::decay_t<T>;
			if ( _pVtable == nullptr )
				return nullptr;
#if defined( SW_DEBUG )
			SW_ASSERT( _pVtable->_pDestroy != nullptr );
			SW_ASSERT( _pVtable->_bIsInline == kIsInline<Stored> );
			SW_ASSERT( _pVtable->_typeSize == sizeof( Stored ) );
#endif
			if constexpr ( kIsInline<Stored> )
			{
				return reinterpret_cast<const Stored*>( _arrStorage );
			}
			else
			{
				return *reinterpret_cast<const Stored* const*>( _arrStorage );
			}
		}

		template <typename T>
		/** @brief 값을 복사해 반환합니다. 없으면 defaultValue. */
		T getValue( const T& defaultValue = T{} ) const
		{
			const T* pVal = getPtr<T>();
			if ( pVal != nullptr )
				return *pVal;
			return defaultValue;
		}

	private:
		const VTable* _pVtable{ nullptr };
		alignas( std::max_align_t ) std::byte _arrStorage[kInlineStorageSize]{};
	};

	/**
	 * @class TaskArgs
	 * @brief 위치 기반 태스크 인자 가방.
	 * @details MakeTaskArgs<T0,T1,...>(...) → get<T0>(0), get<T1>(1), ...
	 */
	class TaskArgs
	{
	public:
		/** @brief 빈 인자 가방. */
		TaskArgs() = default;

		/** @brief 가변 인자로 값 가방을 채웁니다. */
		template <typename... Args, typename = std::enable_if_t<( sizeof...( Args ) > 0 )>>
		explicit TaskArgs( Args&&... args )
		{
			_listValue.reserve( sizeof...( Args ) );
			( _listValue.emplace_back( std::forward<Args>( args ) ), ... );
		}

		TaskArgs( std::initializer_list<TaskValue> listValues )
			: _listValue{ listValues.begin(), listValues.end() } {}

		template <typename T>
		/** @brief 추가합니다. */
		void add( T&& val )
		{
			_listValue.emplace_back( std::forward<T>( val ) );
		}

		/** @brief 크기를 반환합니다. */
		uint32 getCount() const { return static_cast<uint32>( _listValue.size() ); }

		/** @brief 인덱스 인자를 반환합니다. */
		const TaskValue& get( uint32 index ) const { return _listValue[index]; }

		template <typename T>
		const T* getPtr( uint32 index ) const
		{
			if ( index < _listValue.size() )
				return _listValue[index].getPtr<T>();
			return nullptr;
		}

		template <typename T>
		/** @brief 반환합니다. */
		T get( uint32 index, const T& defaultVal = T{} ) const
		{
			const T* pVal = getPtr<T>( index );
			if ( pVal != nullptr )
				return *pVal;
			return defaultVal;
		}

	private:
		vector<TaskValue> _listValue;
	};

	/** @brief 명시 타입 목록으로 TaskArgs 생성. */
	template <typename... Ts>
	TaskArgs MakeTaskArgs( Ts... values )
	{
		TaskArgs args;
		( args.add( std::move( values ) ), ... );
		return args;
	}

	/** @brief 매개변수가 없는 기본 태스크 델리게이트 */
	using TaskDelegate = Delegate<void()>;

	/** @brief 위치 기반 TaskArgs 인자를 전달받는 태스크 델리게이트 */
	using TaskArgsDelegate = Delegate<void( const TaskArgs& args )>;

	/** @brief 인덱스(0 ~ count-1)를 전달받는 단일 병렬 태스크 델리게이트 */
	using ParallelTaskDelegate = Delegate<void( uint32 index )>;

	/** @brief 범위 블록([start, end))을 전달받는 청크 분할 병렬 태스크 델리게이트 */
	using ParallelBlockDelegate = Delegate<void( uint32 start, uint32 end )>;

	/**
	 * @enum TaskPriority
	 * @brief 태스크의 실행 우선순위를 나타냅니다.
	 */
	enum class TaskPriority : uint8
	{
		High   = 0, ///< 프레임 크리티컬/렌더링/물리 태스크 (최우선 처리)
		Normal = 1, ///< 일반 게임플레이/계산 태스크 (기본값)
		Low	   = 2	///< 백그라운드 I/O, 에셋 파싱, 통계 태스크
	};

	/**
	 * @enum TaskThreadAffinity
	 * @brief 태스크가 실행될 스레드 선호도(지정 대상)를 결정합니다.
	 */
	enum class TaskThreadAffinity : uint8
	{
		Any,	   ///< 워커 풀의 유휴 스레드 아무 곳에서나 실행 가능
		MainThread ///< 오직 메인 스레드(렌더/UI/엔진 메인 루프)에서만 실행 (dispatchMainThreadTasks 호출 시)
	};

	/**
	 * @enum TaskType
	 * @brief 태스크의 실행 유형을 나타냅니다.
	 */
	enum class TaskType : uint8
	{
		General,  ///< 일반 단일 함수/인자 태스크
		Parallel, ///< 여러 워커에 분산 실행되는 N개 병렬 하위 태스크
		Staged	  ///< 특정 스테이지에 소속된 그룹 태스크
	};

	/**
	 * @enum TaskState
	 * @brief 태스크 노드의 현재 수명주기 상태를 나타냅니다.
	 */
	enum class TaskState : uint8
	{
		Pending,			///< 생성되었으나 아직 부모/빌더 의존성이 남아있어 준비되지 않은 상태
		Ready,				///< 모든 선행 조건이 충족되어 큐에 진입 대기 중인 상태
		Running,			///< 워커 스레드에서 본문이 실행 중인 상태
		WaitingForChildren, ///< 자식 병렬 태스크들이 모두 완료되기를 기다리는 상태
		Completed			///< 실행 및 후속 트리거 처리가 완전히 완료된 상태
	};

	class TaskManager;
	struct TaskNode;
	struct StageNode;

	/**
	 * @struct TaskHandle
	 * @brief 생성된 태스크를 가리키는 고유 핸들이며, DAG 의존성 연결 및 플루언트(Fluent) 체이닝을 지원합니다.
	 * @details
	 * - 침입형 참조 카운팅(Intrusive Reference Counting)을 사용하여 복사/이동 시 스마트 포인터 할당 오버헤드가 없습니다.
	 * - `then()`, `precede()`, `succeed()` 메서드를 통해 작업 간의 선후 관계를 선언적으로 조립할 수 있습니다.
	 */
	struct SW_API TaskHandle
	{
		friend class TaskManager;

		/** @brief 빈 핸들 (노드 없음). */
		constexpr TaskHandle() = default;

		/** @brief TaskNode 포인터로부터 핸들을 생성하며 참조 카운트를 증가시킵니다. */
		explicit TaskHandle( TaskNode* pNode );
		TaskHandle( const TaskHandle& other );
		TaskHandle( TaskHandle&& other ) noexcept;
		~TaskHandle();

		TaskHandle& operator=( const TaskHandle& other );
		TaskHandle& operator=( TaskHandle&& other ) noexcept;

		/** @brief 유효한 태스크 노드를 가리키고 있는지 여부를 반환합니다. */
		bool isValid() const { return _pNode != nullptr; }

		/** @brief 내부 TaskNode 원시 포인터를 반환합니다. */
		TaskNode* getNode() const { return _pNode; }

		/** @brief 태스크의 우선순위를 설정합니다. */
		TaskHandle& setPriority( TaskPriority priority );

		/** @brief 태스크의 우선순위를 반환합니다. */
		TaskPriority getPriority() const;

		/**
		 * @brief 이 태스크가 targetTask보다 반드시 '먼저' 실행 완료되도록 DAG 선후 의존성을 겁니다.
		 * @param targetTask 이 태스크 완료 후 실행될 후속 태스크
		 */
		TaskHandle& precede( TaskHandle targetTask );

		/**
		 * @brief dependencyTask가 반드시 '먼저' 끝난 뒤에 이 태스크가 실행되도록 DAG 선후 의존성을 겁니다.
		 * @param dependencyTask 이 태스크 전에 먼저 완료되어야 하는 선행 태스크
		 */
		TaskHandle& succeed( TaskHandle dependencyTask );

		/**
		 * @brief 이 태스크가 완료된 후 자동으로 실행될 후속 연속(Continuation) 태스크를 생성하여 체이닝합니다.
		 * @param nextTaskDelegate 후속 실행될 델리게이트
		 * @param affinity 후속 태스크가 실행될 스레드 친화도
		 * @return 새롭게 생성된 후속 태스크 핸들
		 */
		TaskHandle then( TaskDelegate nextTaskDelegate, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

		/** @brief 태스크를 취소합니다. 이미 실행 중이지 않은 경우 본문 실행이 생략됩니다. */
		bool cancel();

		/** @brief 태스크가 취소되었는지 여부를 반환합니다. */
		bool isCancelled() const;

		/** @brief 태스크의 빌더 의존성을 해제하고 스케줄러에 즉시 제출하여 선행 조건 충족 시 실행되도록 합니다. */
		void submit();

	private:
		TaskNode* _pNode{ nullptr };
	};

	/**
	 * @struct CancellationToken
	 * @brief 비동기 태스크에 전달하여 외부에서 취소 신호를 보내거나 확인할 수 있는 토큰
	 */
	struct SW_API CancellationToken
	{
		shared_ptr<atomic<bool>> _pCancelled;

		CancellationToken()
			: _pCancelled{ sw::make_shared<atomic<bool>>( false ) } {}

		void cancel()
		{
			if ( _pCancelled != nullptr )
				_pCancelled->store( true, std::memory_order_release );
		}

		bool isCancelled() const
		{
			return _pCancelled != nullptr && _pCancelled->load( std::memory_order_acquire );
		}
	};

	/**
	 * @struct TaskStageHandle
	 * @brief 여러 태스크를 하나의 논리적 단계(Stage)로 묶어 관리하고 동기화할 수 있는 스테이지 핸들입니다.
	 * @details 스테이지 내의 모든 태스크가 완료될 때까지 `waitStage()`로 블로킹 대기할 수 있습니다.
	 */
	struct SW_API TaskStageHandle
	{
		friend class TaskManager;

		/** @brief 빈 스테이지 핸들. */
		constexpr TaskStageHandle() = default;

		explicit TaskStageHandle( shared_ptr<StageNode> node )
			: _node{ std::move( node ) } {}

		/** @brief 스테이지 노드가 유효한지 여부를 반환합니다. */
		bool isValid() const { return _node != nullptr; }

		/** @brief 내부 StageNode 스마트 포인터를 반환합니다. */
		shared_ptr<StageNode> getStageNode() const { return _node; }

		/**
		 * @brief 특정 태스크를 이 스테이지에 추가합니다.
		 * @param task 스테이지에 소속시킬 태스크 핸들
		 */
		TaskStageHandle& addTask( TaskHandle task );

	private:
		shared_ptr<StageNode> _node;
	};
} // namespace sw
