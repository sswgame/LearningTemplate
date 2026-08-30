/**
 * @file Delegate.h
 * @brief 타입 안전 델리게이트·멀티캐스트·핸들
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/array.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) 핸들 ID — Core.dll 이 유일 발급. 멀티캐스트 remove 키
	// ------------------------------------------------------------------------------
	/** @brief 단일 바인딩 콜백 (함수·멤버·람다). */
	template <typename T>
	class Delegate;
	/** @brief 여러 Delegate 를 모아 broadcast 합니다. */
	template <typename T>
	class MulticastDelegate;

	// ------------------------------------------------------------------------------
	// 2) Delegate — create / operator() / isBound. 람다는 SBO 또는 힙
	// ------------------------------------------------------------------------------
	template <typename R, typename... Args>
	/** @brief 호출 가능한 대상을 하나 붙입니다. */
	class Delegate<R( Args... )>
	{
	public:
		/** @brief 람다 저장소 복사·이동·파괴 연산입니다. */
		enum class DelegateManagerOp
		{
			Copy,
			Move,
			Destroy
		};

		using manager_function					  = void* (*)( DelegateManagerOp, void*, const void* );
		static constexpr size_t kInlineBufferSize = 24;

		/** @brief 빈 델리게이트를 생성합니다. */
		Delegate() = default;

		/** @brief nullptr로 빈 델리게이트를 생성합니다. */
		Delegate( std::nullptr_t ) {}

		/** @brief 바인딩과 람다 저장소를 해제합니다. */
		~Delegate() { release(); }

		/** @brief 대상과 람다 저장소를 복제합니다. */
		Delegate( const Delegate& other ) { copyFrom( other ); }

		/** @brief 대상과 람다 저장소를 가져옵니다. */
		Delegate( Delegate&& other ) noexcept { moveFrom( std::move( other ) ); }

		/** @brief 복사 대입합니다. */
		Delegate& operator=( const Delegate& other )
		{
			if ( this != &other )
			{
				release();
				copyFrom( other );
			}
			return *this;
		}

		/** @brief 이동 대입합니다. */
		Delegate& operator=( Delegate&& other ) noexcept
		{
			if ( this != &other )
			{
				release();
				moveFrom( std::move( other ) );
			}
			return *this;
		}

		/** @brief 정적 함수 포인터로 델리게이트를 생성합니다. */
		Delegate( R ( *func )( Args... ) ) { *this = create( func ); }

		/**
		 * @brief 람다 함수로 델리게이트를 생성합니다.
		 * @note 빈번하게 호출되는 이벤트(예: 매 프레임 발생하는 Tick)에서는
		 *       람다 대신 가급적 멤버 함수 바인딩(create<Method>)을 사용하시기 바랍니다.
		 */
		template <typename Lambda, typename = std::enable_if_t<std::is_same_v<std::decay_t<Lambda>, Delegate> == false && std::is_invocable_r_v<R, Lambda, Args...>>>
		Delegate( const Lambda& lambdaFunc ) { *this = create( lambdaFunc ); }

		/**
		 * @brief 델리게이트가 동일한 대상을 가리키는지 비교합니다.
		 * @note 람다로 생성된 Delegate는 복사 시 서로 다른 인스턴스로 취급되어 항상 false를 반환합니다.
		 *       따라서 MulticastDelegate에서 람다를 제거할 때는 반드시 DelegateHandle을 사용해야 합니다.
		 */
		bool operator==( const Delegate& other ) const
		{
			if ( _stubFunc != other._stubFunc )
				return false;
			if ( _managerFunc != other._managerFunc )
				return false;
			return _pInstance == other._pInstance;
		}

		/** @brief 다른지 비교합니다. */
		bool operator!=( const Delegate& other ) const { return ( *this == other ) == false; }
		/** @brief 바인딩이 없으면 true입니다. */
		bool operator==( std::nullptr_t ) const { return _stubFunc == nullptr; }
		/** @brief 바인딩이 있으면 true입니다. */
		bool operator!=( std::nullptr_t ) const { return _stubFunc != nullptr; }

		/** @brief 델리게이트가 호출 가능한 상태(바인딩됨)인지 확인합니다. */
		bool isBound() const { return _stubFunc != nullptr; }

		template <typename... UArgs, typename = std::enable_if_t<std::is_invocable_v<R( Args... ), UArgs...>>>
		/** @brief 바인딩된 대상을 호출합니다. 비어 있으면 assert. */
		R operator()( UArgs&&... args ) const
		{
			SW_ASSERT( isBound() );
			return std::invoke( _stubFunc, _pInstance, std::forward<UArgs>( args )... );
		}

		template <auto Function, typename = std::enable_if_t<std::is_invocable_r_v<R, decltype( Function ), Args...>>>
		/** @brief 컴파일타임 함수 포인터로 바인딩합니다. */
		static Delegate create()
		{
			Delegate newDelegate{};
			newDelegate._pInstance = nullptr;
			newDelegate._stubFunc  = static_cast<stub_function>( []( const void*, Args... args ) -> R
			{ return std::invoke( Function, std::forward<Args>( args )... ); } );

			return newDelegate;
		}

		/** @brief 런타임 함수 포인터로 델리게이트를 생성합니다. */
		static Delegate create( R ( *pFunc )( Args... ) )
		{
			Delegate newDelegate{};
			newDelegate._pInstance = reinterpret_cast<const void*>( pFunc );
			newDelegate._stubFunc  = static_cast<stub_function>( []( const void* pPtr, Args... args ) -> R
			{
				auto pFn = reinterpret_cast<R ( * )( Args... )>( const_cast<void*>( pPtr ) );
				return pFn( std::forward<Args>( args )... );
			} );

			return newDelegate;
		}

		template <auto MemberFunction, typename Class, typename = std::enable_if_t<std::is_invocable_r_v<R, decltype( MemberFunction ), const Class*, Args...>>>
		/** @brief const 인스턴스의 멤버 함수를 바인딩합니다. */
		static Delegate create( const Class* pClassInstance )
		{
			Delegate newDelegate{};
			newDelegate._pInstance = pClassInstance;
			newDelegate._stubFunc  = static_cast<stub_function>( []( const void* pPtr, Args... args ) -> R
			{
				const Class* pInstance = static_cast<const Class*>( pPtr );
				return std::invoke( MemberFunction, pInstance, std::forward<Args>( args )... );
			} );

			return newDelegate;
		}

		template <auto MemberFunction, typename Class, typename = std::enable_if_t<std::is_invocable_r_v<R, decltype( MemberFunction ), Class*, Args...>>>
		/** @brief 인스턴스의 멤버 함수를 바인딩합니다. */
		static Delegate create( Class* pClassInstance )
		{
			Delegate newDelegate{};
			newDelegate._pInstance = pClassInstance;
			newDelegate._stubFunc  = static_cast<stub_function>( []( const void* pPtr, Args... args ) -> R
			{
				Class* pInstance = const_cast<Class*>( static_cast<const Class*>( pPtr ) );
				return std::invoke( MemberFunction, pInstance, std::forward<Args>( args )... );
			} );

			return newDelegate;
		}

		/**
		 * @brief 람다 함수나 Functor 객체로 델리게이트를 생성합니다.
		 * @note [가이드라인] 매 프레임 호출되는 Tick이나 빈도가 매우 높은 콜백에서는
		 *       람다 바인딩 대신 멤버 함수 바인딩(create<Method>) 사용을 강력히 권장합니다.
		 */
		template <typename Lambda>
		static Delegate create( const Lambda& lambdaFunc )
		{
			Delegate newDelegate{};
			newDelegate._managerFunc = &lambdaManager<Lambda>;
			newDelegate._stubFunc	 = static_cast<stub_function>( []( const void* pPtr, Args... args ) -> R
			{
				const Lambda* pInstance = static_cast<const Lambda*>( pPtr );
				return ( const_cast<Lambda*>( pInstance )->operator() )( std::forward<Args>( args )... );
			} );

			constexpr bool bIsSBO = sizeof( Lambda ) <= kInlineBufferSize && alignof( Lambda ) <= alignof( std::max_align_t ) && std::is_nothrow_move_constructible_v<Lambda>;
			if constexpr ( bIsSBO )
			{
				new ( &newDelegate._inlineBuffer ) Lambda( lambdaFunc );
				newDelegate._pInstance = &newDelegate._inlineBuffer;
			}
			else
			{
				Lambda* pHeapLambda										  = sw_new Lambda( lambdaFunc );
				*reinterpret_cast<Lambda**>( &newDelegate._inlineBuffer ) = pHeapLambda;
				newDelegate._pInstance									  = pHeapLambda;
			}

			return newDelegate;
		}

	private:
		using stub_function = R ( * )( const void*, Args... );

		/** @brief 바인딩을 해제합니다. */
		void release()
		{
			if ( _managerFunc )
				_managerFunc( DelegateManagerOp::Destroy, &_inlineBuffer, nullptr );
			_pInstance	 = nullptr;
			_stubFunc	 = nullptr;
			_managerFunc = nullptr;
		}

		/** @brief 다른 델리게이트를 복사합니다. */
		void copyFrom( const Delegate& other )
		{
			_stubFunc	 = other._stubFunc;
			_managerFunc = other._managerFunc;
			if ( _managerFunc )
				_pInstance = _managerFunc( DelegateManagerOp::Copy, &_inlineBuffer, &other._inlineBuffer );
			else
				_pInstance = other._pInstance;
		}

		/** @brief 다른 델리게이트를 이동합니다. */
		void moveFrom( Delegate&& other )
		{
			_stubFunc	 = other._stubFunc;
			_managerFunc = other._managerFunc;
			if ( _managerFunc )
				_pInstance = _managerFunc( DelegateManagerOp::Move, &_inlineBuffer, &other._inlineBuffer );
			else
				_pInstance = other._pInstance;
			other._pInstance   = nullptr;
			other._stubFunc	   = nullptr;
			other._managerFunc = nullptr;
		}

		/** @brief 람다 저장소를 관리합니다. */
		template <typename Lambda>
		static void* lambdaManager( DelegateManagerOp op, void* pDest, const void* pSrc )
		{
			constexpr bool bIsSBO = sizeof( Lambda ) <= kInlineBufferSize && alignof( Lambda ) <= alignof( std::max_align_t ) && std::is_nothrow_move_constructible_v<Lambda>;

			switch ( op )
			{
				case DelegateManagerOp::Copy:
					if constexpr ( bIsSBO )
					{
						new ( pDest ) Lambda( *static_cast<const Lambda*>( pSrc ) );
						return pDest;
					}
					else
					{
						Lambda* pNewHeap				= sw_new Lambda( **static_cast<Lambda* const*>( pSrc ) );
						*static_cast<Lambda**>( pDest ) = pNewHeap;
						return pNewHeap;
					}
				case DelegateManagerOp::Move:
					if constexpr ( bIsSBO )
					{
						new ( pDest ) Lambda( std::move( *static_cast<Lambda*>( const_cast<void*>( pSrc ) ) ) );
						return pDest;
					}
					else
					{
						Lambda* pHeap					= *static_cast<Lambda* const*>( pSrc );
						*static_cast<Lambda**>( pDest ) = pHeap;
						return pHeap;
					}
				case DelegateManagerOp::Destroy:
					if constexpr ( bIsSBO )
						static_cast<Lambda*>( pDest )->~Lambda();
					else
						sw_delete( *static_cast<Lambda**>( pDest ) );
					return nullptr;
				default:
					break;
			}
			return nullptr;
		}

		const void*		 _pInstance{ nullptr };
		stub_function	 _stubFunc{ nullptr };
		manager_function _managerFunc{ nullptr };
		alignas( std::max_align_t ) std::array<uint8, kInlineBufferSize> _inlineBuffer{};
	};
} // namespace sw

namespace sw
{

	// ------------------------------------------------------------------------------
	// 3) DelegateHandle — add() 가 발급, remove(handle) 로 해제
	// ------------------------------------------------------------------------------
	/** @brief 멀티캐스트 항목을 가리키는 발급 ID입니다. 0 은 무효. */
	struct DelegateHandle
	{
		uint64 _id{ 0 };

		/** @brief 다음 멀티캐스트 델리게이트 핸들을 발급합니다. */
		SW_API static DelegateHandle allocate();

		/** @brief 발급된 ID가 있으면 true입니다. */
		bool isValid() const { return _id != 0; }
		/** @brief 같은지 비교합니다. */
		bool operator==( const DelegateHandle& rhs ) const { return _id == rhs._id; }
		/** @brief 다른지 비교합니다. */
		bool operator!=( const DelegateHandle& rhs ) const { return _id != rhs._id; }
	};

	/** @brief 타입 소거된 멀티캐스트. 핸들로만 제거합니다. */
	class SW_API IMulticastDelegateBase
	{
	public:
		/** @brief 파생 리스트를 비웁니다. */
		virtual ~IMulticastDelegateBase() = default;
		/** @brief 핸들과 일치하는 항목을 제거합니다. */
		virtual void remove( const DelegateHandle& handle ) = 0;
	};

	/** @brief 시그니처별 멀티캐스트 특수화입니다. */
	template <typename Signature>
	class MulticastDelegate;

	// ------------------------------------------------------------------------------
	// 4) MulticastDelegate — add / remove / broadcast. 방송 중 remove 는 지연
	// ------------------------------------------------------------------------------
	/**
	 * @class MulticastDelegate
	 * @brief 다수의 델리게이트를 관리하고 브로드캐스트할 수 있는 컨테이너
	 */
	template <typename R, typename... Args>
	class MulticastDelegate<R( Args... )> : public IMulticastDelegateBase
	{
		using delegate_type = Delegate<R( Args... )>;

		/** @brief 핸들과 바인딩 한 쌍입니다. */
		struct DelegateEntry
		{
			DelegateHandle _handle;
			delegate_type  _delegate;
		};

	public:
		/** @brief 빈 구독 리스트로 둡니다. */
		MulticastDelegate() noexcept
			: _listDelegate{}
			, _listPendingRemove{}
			, _broadcastDepth{ 0 }
		{
		}

		/** @brief 구독 리스트를 복제합니다. */
		MulticastDelegate( const MulticastDelegate& other ) = default;
		/** @brief 복사 대입합니다. */
		MulticastDelegate& operator=( const MulticastDelegate& other ) = default;

		/** @brief 두 멀티캐스트 델리게이트가 동일한 대상 리스트를 가지고 있는지 비교합니다. */
		bool operator==( const MulticastDelegate& other ) const
		{
			if ( _listDelegate.size() != other._listDelegate.size() )
				return false;

			const uint32 delegateCount = static_cast<uint32>( _listDelegate.size() );
			for ( uint32 delegateIndex = 0; delegateIndex < delegateCount; ++delegateIndex )
			{
				if ( _listDelegate[delegateIndex]._delegate != other._listDelegate[delegateIndex]._delegate )
					return false;
			}
			return true;
		}

		/** @brief 다른지 비교합니다. */
		bool operator!=( const MulticastDelegate& other ) const { return ( *this == other ) == false; }

		/** @brief 하나라도 등록된 델리게이트가 있는지 확인합니다. */
		bool isBound() const { return _listDelegate.empty() == false; }

		/**
		 * @brief 등록된 모든 델리게이트에게 이벤트를 브로드캐스트(발송)합니다.
		 * @details broadcast 중 remove()가 호출되면 완료 후 일괄 처리합니다.
		 *          매 호출마다 벡터 전체를 복사하지 않으므로 고빈도 이벤트에 적합합니다.
		 */
		void broadcast( Args... args )
		{
			// 콜백이 다시 broadcast 를 부를 수 있으므로 깊이로 셉니다. 가장 바깥 호출만 지연 제거를 처리합니다.
			++_broadcastDepth;
			const size_t numDelegates = _listDelegate.size();
			for ( size_t entryIndex = 0; entryIndex < numDelegates; ++entryIndex )
			{
				// 콜백이 add() 를 부르면 벡터가 재할당되므로 원소 참조를 들고 호출하지 않습니다.
				delegate_type callee = _listDelegate[entryIndex]._delegate;
				if ( callee.isBound() )
					callee( args... );
			}
			--_broadcastDepth;

			if ( _broadcastDepth > 0 )
				return;

			// broadcast 중 요청된 remove를 일괄 처리
			for ( const DelegateHandle& removeHandle : _listPendingRemove )
			{
				removeNow( removeHandle );
			}
			_listPendingRemove.clear();
		}

		/** @brief 새로운 델리게이트를 등록하고 핸들을 반환합니다. */
		DelegateHandle add( const delegate_type& newDelegate )
		{
			DelegateHandle handle = DelegateHandle::allocate();
			_listDelegate.push_back( DelegateEntry{ handle, newDelegate } );
			return handle;
		}

		/** @brief 발급받았던 핸들을 사용하여 델리게이트를 등록 해제합니다. */
		void remove( const DelegateHandle& handle ) override
		{
			if ( handle.isValid() == false )
				return;

			if ( _broadcastDepth > 0 )
			{
				// broadcast 완료 후 일괄 처리 (이터레이터 무효화 방지)
				_listPendingRemove.push_back( handle );
				return;
			}
			removeNow( handle );
		}

		/** @brief 등록된 대상 델리게이트와 일치하는 항목을 찾아 삭제합니다. */
		void remove( const delegate_type& target )
		{
			const auto iter = std::find_if( _listDelegate.begin(), _listDelegate.end(), [&]( const DelegateEntry& entry )
			{ return entry._delegate == target; } );

			if ( iter == _listDelegate.end() )
				return;

			// 핸들 경로와 같은 지연 제거를 타야 broadcast 중 이터레이터가 깨지지 않습니다.
			remove( iter->_handle );
		}

		/** @brief 등록된 모든 델리게이트를 해제(제거)합니다. */
		void removeAll()
		{
			if ( _broadcastDepth > 0 )
			{
				for ( const DelegateEntry& entry : _listDelegate )
				{
					_listPendingRemove.push_back( entry._handle );
				}
				return;
			}
			_listDelegate.clear();
		}

		/** @brief 등록된 모든 델리게이트를 해제합니다 (removeAll 별칭). */
		void clear() { removeAll(); }

	private:
		/** @brief 핸들로 즉시 항목을 제거합니다. broadcast 외부에서만 호출하세요. */
		void removeNow( const DelegateHandle& handle )
		{
			const auto iter = std::find_if( _listDelegate.begin(), _listDelegate.end(), [&]( const DelegateEntry& entry )
			{ return entry._handle == handle; } );
			if ( iter != _listDelegate.end() )
				_listDelegate.erase( iter );
		}

		vector<DelegateEntry>  _listDelegate;
		vector<DelegateHandle> _listPendingRemove; ///< broadcast 중 지연된 remove 목록
		uint32				   _broadcastDepth;	   ///< 중첩 broadcast 깊이. 0 이 될 때만 지연 제거를 반영
	};
} // namespace sw

/** @brief Delegate 별칭을 선언합니다. */
#define SW_DECLARE_DELEGATE( ReturnType, DelegateName, ... ) using DelegateName = sw::Delegate<ReturnType( __VA_ARGS__ )>
/** @brief MulticastDelegate 별칭을 선언합니다. */
#define SW_DECLARE_MULTI_CAST_DELEGATE( ReturnType, DelegateName, ... ) using DelegateName = sw::MulticastDelegate<ReturnType( __VA_ARGS__ )>
/** @brief 자유 함수를 델리게이트에 붙입니다. */
#define SW_DELEGATE_FUNCTION( DelegateName, Func ) DelegateName::create<Func>()
/** @brief 멤버 함수를 델리게이트에 붙입니다. */
#define SW_DELEGATE_METHOD( DelegateName, Method, Instance ) DelegateName::create<Method>( Instance )
/** @brief 람다/펑터를 델리게이트에 붙입니다. */
#define SW_DELEGATE_LAMBDA( DelegateName, ... ) DelegateName::create( __VA_ARGS__ )
