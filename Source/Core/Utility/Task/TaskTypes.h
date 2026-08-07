#pragma once
/**
 * @file TaskTypes.h
 * @brief 비동기 태스크 시스템에서 사용하는 핸들, 인자, 델리게이트 및 열거형 타입들을 정의합니다.
 */
#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"
#include "Core/Utility/Delegate/Delegate.h"

namespace sw
{
	/**
	 * @class TaskValue
	 * @brief std::any를 래핑하여 모든 형태의 인자를 런타임에 안전하게 저장하고 타입 캐스팅할 수 있는 클래스
	 */
	class TaskValue
	{
	public:
		TaskValue() = default;

		template <typename T, typename = std::enable_if_t<std::is_same_v<std::decay_t<T>, TaskValue> == false>>
		TaskValue( T&& value )
			: _value{ std::forward<T>( value ) }
		{
		}

		/** @brief 내부에 실제 값이 저장되어 있는지 여부를 확인합니다. */
		bool hasValue() const { return _value.has_value(); }

		/** @brief 저장된 값의 런타임 타입 정보를 반환합니다. */
		const std::type_info& type() const { return _value.type(); }

		/**
		 * @brief 지정된 타입 T에 대한 포인터를 반환합니다. 타입이 불일치하면 nullptr을 반환합니다.
		 */
		template <typename T>
		const T* getPtr() const
		{
			return std::any_cast<T>( &_value );
		}

		template <typename T>
		T getValue( const T& defaultValue = T{} ) const
		{
			if ( auto* p = std::any_cast<T>( &_value ) )
				return *p;
			return defaultValue;
		}

	private:
		std::any _value;
	};

	/**
	 * @class TaskArgs
	 * @brief 하나 이상의 TaskValue를 배열 형태로 유지하여, 태스크 실행 시 런타임에 매개변수를 전달하기 위한 컨테이너 클래스
	 */
	class TaskArgs
	{
	public:
		TaskArgs() = default;
		TaskArgs( std::initializer_list<TaskValue> values )
			: _values{ values.begin(), values.end() }
		{
		}

		template <typename... Args, typename = std::enable_if_t<( sizeof...( Args ) > 0 )>>
		explicit TaskArgs( Args&&... args )
		{
			( _values.emplace_back( std::forward<Args>( args ) ), ... );
		}

		/**
		 * @brief 새로운 인자 값을 리스트 끝에 추가합니다.
		 */
		template <typename T>
		void add( T&& val )
		{
			_values.emplace_back( std::forward<T>( val ) );
		}

		/** @brief 저장된 인자의 총 개수를 반환합니다. */
		uint32 getCount() const { return static_cast<uint32>( _values.size() ); }

		/** @brief 인덱스에 해당하는 인자 값을 반환합니다. (범위 초과 시 예외 발생 가능) */
		const TaskValue& get( uint32 index ) const { return _values[index]; }

		/**
		 * @brief 인덱스에 해당하는 인자를 지정된 타입 포인터로 획득합니다.
		 */
		template <typename T>
		const T* getPtr( uint32 index ) const
		{
			if ( index < _values.size() )
				return _values[index].getPtr<T>();
			return nullptr;
		}

		template <typename T>
		T get( uint32 index, const T& defaultVal = T{} ) const
		{
			if ( index < _values.size() )
				return _values[index].getValue<T>( defaultVal );
			return defaultVal;
		}

	private:
		std::vector<TaskValue> _values;
	};

	using TaskDelegate = Delegate<void()>;

	using TaskArgsDelegate = Delegate<void( const TaskArgs& args )>;

	using ParallelTaskDelegate = Delegate<void( uint32 index )>;

	using ParallelBlockDelegate = Delegate<void( uint32 start, uint32 end )>;

	enum class TaskType : uint8
	{
		General,
		Parallel,
		Staged
	};

	enum class TaskState : uint8
	{
		Pending,
		Ready,
		Running,
		Completed
	};

	class TaskManager;
	struct TaskNode;
	struct StageNode;

	struct TaskHandle
	{
		friend class TaskManager;

	public:
		constexpr TaskHandle() = default;
		explicit TaskHandle( std::shared_ptr<TaskNode> node )
			: _node{ std::move( node ) }
		{
		}

		bool isValid() const { return _node != nullptr; }

		std::shared_ptr<TaskNode> getNode() const { return _node; }

		/**
		 * @brief 선행 의존성을 추가합니다
		 */
		TaskHandle& precede( TaskHandle targetTask );

		/**
		 * @brief 후행 의존성을 추가합니다
		 */
		TaskHandle& succeed( TaskHandle dependencyTask );

		/**
		 * @brief 완료 후 연결 작업을 추가합니다
		 */
		TaskHandle then( TaskDelegate nextTaskDelegate );

	private:
		std::shared_ptr<TaskNode> _node;
	};

	struct TaskStageHandle
	{
		friend class TaskManager;

	public:
		constexpr TaskStageHandle() = default;
		explicit TaskStageHandle( std::shared_ptr<StageNode> node )
			: _node{ std::move( node ) }
		{
		}

		bool isValid() const { return _node != nullptr; }

		std::shared_ptr<StageNode> getStageNode() const { return _node; }

		/**
		 * @brief 스테이지에 작업을 추가합니다
		 */
		TaskStageHandle& addTask( TaskHandle task );

	private:
		std::shared_ptr<StageNode> _node;
	};
} // namespace sw
