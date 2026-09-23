/**
 * @file TaskTypes.h
 * @brief 비동기 태스크 시스템이 쓰는 핸들 · 인자 · 델리게이트 · 열거형 타입입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    /**
     * @class TaskValue
     * @brief 여러 타입의 값을 런타임에 안전하게 보관하는 타입 소거 컨테이너입니다.
     * @details
     * - **SBO(Small Buffer Optimization, 32바이트)**:
     *   기본형(int32, float32, 포인터 등)과 32바이트 이하의 작은 구조체는 힙 할당(new/delete) 없이 인라인 버퍼(`_arrStorage`)에
     *   바로 저장합니다.
     * - **큰 객체는 힙에 할당**:
     *   32바이트를 넘는 객체는 자동으로 힙에 할당하고 포인터로 관리합니다.
     * - **소멸 · 복사 · 이동 VTable**:
     *   C++ 가상 함수 대신 함수 포인터 3개로 된 정적 VTable 로 수명을 가볍게 관리합니다.
     * - **타입 안전성**:
     *   값은 `getPtr<T>()` 로 읽으며, Debug 빌드에서는 저장 방식(인라인 여부)과 타입 크기를 비교해 잘못된 타입으로 읽으면 바로
     *   assert 합니다.
     */
    class TaskValue
    {
        /** @brief 인라인으로 보관할 수 있는 최대 크기(32바이트)입니다. */
        static constexpr size_t kInlineStorageSize = 32;

        /** @brief 타입 T 가 32바이트 인라인 버퍼에 들어가는지 컴파일 타임에 판별합니다. */
        template <typename T>
        static constexpr bool kIsInline = ( sizeof( T ) <= kInlineStorageSize && alignof( T ) <= alignof( std::max_align_t ) );

        /** @brief 타입별 소멸 · 복사 생성 · 이동 생성을 담당하는 함수 테이블입니다. */
        struct VTable
        {
            void ( *_pDestroy )( void* pStorage );             ///< 소멸자를 부르는 함수
            void ( *_pClone )( const void* pSrc, void* pDst ); ///< 복사 생성 함수
            void ( *_pMove )( void* pSrc, void* pDst );        ///< 이동 생성 함수
            size_t _typeSize{ 0 };
            bool   _bIsInline{ false };
        };

        template <typename T>
        static const VTable* getInlineVtable()
        {
            static constexpr VTable s_pVtable{
                []( void* pStorage )
            {
                reinterpret_cast<T*>( pStorage )->~T();
            },
                []( const void* pSrc, void* pDst )
            {
                sw_placement_new( pDst ) T( *reinterpret_cast<const T*>( pSrc ) );
            },
                []( void* pSrc, void* pDst )
            {
                sw_placement_new( pDst ) T( std::move( *reinterpret_cast<T*>( pSrc ) ) );
                reinterpret_cast<T*>( pSrc )->~T();
            },
                sizeof( T ),
                true };
            return &s_pVtable;
        }

        template <typename T>
        static const VTable* getHeapVtable()
        {
            static constexpr VTable s_pVtable{
                []( void* pStorage )
            {
                T* pPtr = *reinterpret_cast<T**>( pStorage );
                sw_delete( pPtr );
            },
                []( const void* pSrc, void* pDst )
            {
                const T*                                pSource = *reinterpret_cast<const T* const*>( pSrc );
                *reinterpret_cast<T**>( pDst )                  = sw_new T( *pSource );
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
        /** @brief 빈 값입니다(저장소 없음). */
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
                  typename         = std::enable_if_t<!std::is_same_v<Decayed, TaskValue>>>
        TaskValue( T&& value )
        {
            if constexpr ( kIsInline<Decayed> )
            {
                _pVtable = getInlineVtable<Decayed>();
                sw_placement_new( _arrStorage ) Decayed( std::forward<T>( value ) );
            }
            else
            {
                _pVtable                                    = getHeapVtable<Decayed>();
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

        /** @brief 저장된 값의 포인터를 반환합니다. 타입이 맞지 않으면 Debug 에서 assert 합니다. */
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
                return reinterpret_cast<const Stored*>( _arrStorage );
            else
                return *reinterpret_cast<const Stored* const*>( _arrStorage );
        }

        template <typename T>
        /** @brief 값을 복사해 반환합니다. 값이 없으면 defaultValue 입니다. */
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
     * @brief 위치 기반 태스크 인자 묶음입니다.
     * @details MakeTaskArgs<T0,T1,...>(...) 로 만들고 get<T0>(0), get<T1>(1), ... 로 읽습니다.
     */
    class TaskArgs
    {
    public:
        /** @brief 빈 인자 묶음입니다. */
        TaskArgs() = default;

        /** @brief 가변 인자로 값을 채웁니다. */
        template <typename... Args, typename = std::enable_if_t<( sizeof...( Args ) > 0 )>>
        explicit TaskArgs( Args&&... args )
        {
            _listValue.reserve( sizeof...( Args ) );
            ( _listValue.emplace_back( std::forward<Args>( args ) ), ... );
        }

        TaskArgs( std::initializer_list<TaskValue> listValue )
            : _listValue{ listValue.begin(), listValue.end() } {}

        template <typename T>
        /** @brief 값을 하나 추가합니다. */
        void add( T&& val )
        {
            _listValue.emplace_back( std::forward<T>( val ) );
        }

        /** @brief 인자 개수를 반환합니다. */
        uint32 getCount() const { return static_cast<uint32>( _listValue.size() ); }

        /** @brief index 번째 인자를 반환합니다. */
        const TaskValue& get( uint32 index ) const { return _listValue[index]; }

        template <typename T>
        const T* getPtr( uint32 index ) const
        {
            if ( index < _listValue.size() )
                return _listValue[index].getPtr<T>();
            return nullptr;
        }

        template <typename T>
        /** @brief index 번째 인자를 T 로 꺼내 반환합니다. 없으면 defaultVal 입니다. */
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

    /** @brief 타입 목록을 명시해 TaskArgs 를 만듭니다. */
    template <typename... Ts>
    TaskArgs MakeTaskArgs( Ts... values )
    {
        TaskArgs args;
        ( args.add( std::move( values ) ), ... );
        return args;
    }

    /** @brief 매개변수가 없는 기본 태스크 델리게이트입니다. */
    using TaskDelegate = Delegate<void()>;

    /** @brief 위치 기반 TaskArgs 를 받는 태스크 델리게이트입니다. */
    using TaskArgsDelegate = Delegate<void( const TaskArgs& args )>;

    /** @brief 인덱스(0 ~ count-1) 하나를 받는 병렬 태스크 델리게이트입니다. */
    using ParallelTaskDelegate = Delegate<void( uint32 index )>;

    /** @brief 범위 블록([start, end))을 받는 청크 단위 병렬 태스크 델리게이트입니다. */
    using ParallelBlockDelegate = Delegate<void( uint32 start, uint32 end )>;

    /**
     * @enum TaskPriority
     * @brief 태스크의 실행 우선순위입니다.
     */
    enum class TaskPriority : uint8
    {
        High   = 0, ///< 프레임에 결정적인 렌더링 · 물리 태스크(가장 먼저 처리)
        Normal = 1, ///< 일반 게임플레이 · 계산 태스크(기본값)
        Low    = 2  ///< 백그라운드 I/O · 에셋 파싱 · 통계 태스크
    };

    /**
     * @enum TaskThreadAffinity
     * @brief 태스크를 실행할 스레드를 정합니다.
     */
    enum class TaskThreadAffinity : uint8
    {
        Any,       ///< 워커 풀의 아무 유휴 스레드에서나 실행한다
        MainThread ///< 메인 스레드(렌더 · UI · 엔진 메인 루프)에서만 실행한다(dispatchMainThreadTasks 를 부를 때)
    };

    /**
     * @enum TaskType
     * @brief 태스크의 실행 유형입니다.
     */
    enum class TaskType : uint8
    {
        General,  ///< 일반 단일 함수 · 인자 태스크
        Parallel, ///< 여러 워커에 나눠 실행하는 N개의 병렬 하위 태스크
        Staged    ///< 특정 스테이지에 속한 그룹 태스크
    };

    /**
     * @enum TaskState
     * @brief 태스크 노드의 현재 수명 주기 상태입니다.
     */
    enum class TaskState : uint8
    {
        Pending,            ///< 만들어졌지만 부모 · 빌더 의존성이 남아 아직 준비되지 않은 상태
        Ready,              ///< 모든 선행 조건을 만족해 큐에 들어가기를 기다리는 상태
        Running,            ///< 워커 스레드에서 본문을 실행 중인 상태
        WaitingForChildren, ///< 자식 병렬 태스크가 모두 끝나기를 기다리는 상태
        Completed           ///< 실행과 후속 태스크 처리까지 모두 끝난 상태
    };

    struct StageNode;
    struct TaskNode;

    class TaskManager;

    /**
     * @struct TaskHandle
     * @brief 만든 태스크를 가리키는 핸들입니다. DAG 의존성 연결과 체이닝을 지원합니다.
     * @details
     * - 침입형 참조 계수를 써서, 복사 · 이동할 때 스마트 포인터 같은 별도 할당이 없습니다.
     * - `then()`, `precede()`, `succeed()` 로 작업 사이의 선후 관계를 선언적으로 조립할 수 있습니다.
     */
    struct SW_API TaskHandle
    {
        friend class TaskManager;

        /** @brief 빈 핸들입니다(노드 없음). */
        constexpr TaskHandle() = default;

        /** @brief TaskNode 포인터로 핸들을 만들고 참조 카운트를 올립니다. */
        explicit TaskHandle( TaskNode* pNode );
        TaskHandle( const TaskHandle& other );
        TaskHandle( TaskHandle&& other ) noexcept;
        ~TaskHandle();

        TaskHandle& operator=( const TaskHandle& other );
        TaskHandle& operator=( TaskHandle&& other ) noexcept;

        /** @brief 유효한 태스크 노드를 가리키는지 반환합니다. */
        bool isValid() const { return _pNode != nullptr; }

        /** @brief 내부 TaskNode 포인터를 반환합니다. */
        TaskNode* getNode() const { return _pNode; }

        /** @brief 태스크의 우선순위를 정합니다. */
        TaskHandle& setPriority( TaskPriority priority );

        /** @brief 태스크의 우선순위를 반환합니다. */
        TaskPriority getPriority() const;

        /**
         * @brief 이 태스크가 targetTask 보다 반드시 **먼저** 끝나도록 DAG 선후 의존성을 겁니다.
         * @param targetTask 이 태스크가 끝난 뒤 실행될 후속 태스크
         */
        TaskHandle& precede( const TaskHandle& targetTask );

        /**
         * @brief dependencyTask 가 반드시 **먼저** 끝난 뒤에 이 태스크가 실행되도록 DAG 선후 의존성을 겁니다.
         * @param dependencyTask 이 태스크보다 먼저 끝나야 하는 선행 태스크
         */
        TaskHandle& succeed( TaskHandle dependencyTask );

        /**
         * @brief 이 태스크가 끝나면 자동으로 실행될 후속 태스크(continuation)를 만들어 연결합니다.
         * @param nextTaskDelegate 이어서 실행할 델리게이트
         * @param affinity 후속 태스크를 실행할 스레드
         * @return 새로 만든 후속 태스크의 핸들
         */
        TaskHandle then( const TaskDelegate& nextTaskDelegate, TaskThreadAffinity affinity = TaskThreadAffinity::Any );

        /** @brief 태스크를 취소합니다. 아직 실행 중이 아니면 본문을 건너뜁니다. */
        bool cancel();

        /** @brief 태스크가 취소됐는지 반환합니다. */
        bool isCancelled() const;

        /** @brief 빌더 의존성을 풀고 스케줄러에 제출합니다. 선행 조건이 모두 만족되면 실행됩니다. */
        void submit();

    private:
        TaskNode* _pNode{ nullptr };
    };

    /**
     * @struct CancellationToken
     * @brief 비동기 태스크에 넘겨, 밖에서 취소 신호를 보내거나 확인할 수 있게 하는 토큰입니다.
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
     * @brief 여러 태스크를 하나의 논리적 단계(stage)로 묶어 관리하고 동기화하는 스테이지 핸들입니다.
     * @details 스테이지 안의 모든 태스크가 끝날 때까지 `waitStage()` 로 블로킹 대기할 수 있습니다.
     */
    struct SW_API TaskStageHandle
    {
        friend class TaskManager;

        constexpr TaskStageHandle() = default;
        /**
         * @brief 노드의 참조 하나를 **넘겨받아** 핸들을 만듭니다(매니저 전용).
         * @details 예전에는 `shared_ptr<StageNode>` 여서 스테이지마다 제어 블록 하나가 힙에 잡혔고, 렌더 그래프는 프레임마다 웨이브
         *          수만큼 스테이지를 만듭니다. 지금은 `TaskHandle` 처럼 침입형 참조 계수이고 노드는 매니저의 풀에서 옵니다. 그래서
         *          프레임이 안정된 상태에서는 스테이지를 디스패치해도 힙을 건드리지 않습니다.
         */
        explicit TaskStageHandle( StageNode* pNode ) noexcept
            : _pNode{ pNode } {}
        TaskStageHandle( const TaskStageHandle& other );
        TaskStageHandle( TaskStageHandle&& other ) noexcept;
        TaskStageHandle& operator=( const TaskStageHandle& other );
        TaskStageHandle& operator=( TaskStageHandle&& other ) noexcept;
        ~TaskStageHandle();

        bool isValid() const { return _pNode != nullptr; }

        TaskStageHandle& addTask( const TaskHandle& task );

    private:
        StageNode* _pNode{ nullptr };
    };
} // namespace sw
