#pragma once
/**
 * @file Delegate.h
 * @brief 타입 안전 델리게이트·멀티캐스트·핸들
 */

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"

namespace sw
{
	template <typename T>
	class Delegate;
	template <typename T>
	class MulticastDelegate;

	template <typename R, typename... Args>
	class Delegate<R( Args... )>
	{
	public:
		Delegate()
			: _pInstance{ nullptr }
			, _stubFunc{ nullptr }
		{
		}
		Delegate( std::nullptr_t )
			: _pInstance{ nullptr }
			, _stubFunc{ nullptr }
		{
		}
		Delegate( const Delegate& other )			 = default;
		Delegate& operator=( const Delegate& other ) = default;

		Delegate( R ( *func )( Args... ) )
		{
			*this = create( func );
		}

		template <typename Lambda, typename = std::enable_if_t<std::is_same_v<std::decay_t<Lambda>, Delegate> == false && std::is_invocable_r_v<R, Lambda, Args...>>>
		Delegate( const Lambda& lambdaFunc )
		{
			*this = create( lambdaFunc );
		}

		bool operator==( const Delegate& other ) const { return other._stubFunc == _stubFunc && other._pInstance == _pInstance; }
		bool operator!=( const Delegate& other ) const { return ( *this == other ) == false; }
		bool operator==( std::nullptr_t ) const { return _stubFunc == nullptr; }
		bool operator!=( std::nullptr_t ) const { return _stubFunc != nullptr; }

		bool isBound() const { return _stubFunc != nullptr; }

		template <typename... UArgs, typename = std::enable_if_t<std::is_invocable_v<R( Args... ), UArgs...>>>
		R operator()( UArgs&&... args ) const
		{
			SW_ASSERT( isBound() );
			return std::invoke( _stubFunc, _pInstance, std::forward<UArgs>( args )... );
		}

		template <auto Function, typename = std::enable_if_t<std::is_invocable_r_v<R, decltype( Function ), Args...>>>
		static Delegate create()
		{
			Delegate newDelegate{};
			newDelegate._pInstance = nullptr;
			newDelegate._stubFunc  = static_cast<stub_function>( []( const void*, Args... args ) -> R
			 {
				 return std::invoke( Function, std::forward<Args>( args )... );
			 } );

			return newDelegate;
		}

		static Delegate create( R ( *func )( Args... ) )
		{
			Delegate newDelegate{};
			newDelegate._pInstance = reinterpret_cast<const void*>( func );
			newDelegate._stubFunc  = static_cast<stub_function>( []( const void* ptr, Args... args ) -> R
			 {
				 auto fn = reinterpret_cast<R ( * )( Args... )>( const_cast<void*>( ptr ) );
				 return fn( std::forward<Args>( args )... );
			 } );

			return newDelegate;
		}

		template <auto MemberFunction, typename Class, typename = std::enable_if_t<std::is_invocable_r_v<R, decltype( MemberFunction ), const Class*, Args...>>>
		static Delegate create( const Class* pClassInstance )
		{
			Delegate newDelegate{};
			newDelegate._pInstance = pClassInstance;
			newDelegate._stubFunc  = static_cast<stub_function>( []( const void* ptr, Args... args ) -> R
			 {
				 const Class* instance = static_cast<const Class*>( ptr );
				 return std::invoke( MemberFunction, instance, std::forward<Args>( args )... );
			 } );

			return newDelegate;
		}

		template <auto MemberFunction, typename Class, typename = std::enable_if_t<std::is_invocable_r_v<R, decltype( MemberFunction ), Class*, Args...>>>
		static Delegate create( Class* pClassInstance )
		{
			Delegate newDelegate{};
			newDelegate._pInstance = pClassInstance;
			newDelegate._stubFunc  = static_cast<stub_function>( []( const void* ptr, Args... args ) -> R
			 {
				 Class* instance = const_cast<Class*>( static_cast<const Class*>( ptr ) );
				 return std::invoke( MemberFunction, instance, std::forward<Args>( args )... );
			 } );

			return newDelegate;
		}

		template <typename Lambda>
		static Delegate create( const Lambda& lambdaFunc )
		{
			Delegate				newDelegate{};
			std::shared_ptr<Lambda> heapLambda = std::make_shared<Lambda>( lambdaFunc );
			newDelegate._pInstance			   = heapLambda.get();
			newDelegate._lifeKeeper			   = heapLambda;
			newDelegate._stubFunc			   = static_cast<stub_function>( []( const void* ptr, Args... args ) -> R
			 {
				 const Lambda* instance = static_cast<const Lambda*>( ptr );
				 return ( instance->operator() )( std::forward<Args>( args )... );
			 } );

			return newDelegate;
		}

	private:
		using stub_function = R ( * )( const void*, Args... );

		const void*			  _pInstance = nullptr;
		stub_function		  _stubFunc	 = nullptr;
		std::shared_ptr<void> _lifeKeeper;
	};
}

namespace sw
{

	struct DelegateHandle
	{
		uint64 _id = 0;

		bool isValid() const { return _id != 0; }
		bool operator==( const DelegateHandle& rhs ) const { return _id == rhs._id; }
		bool operator!=( const DelegateHandle& rhs ) const { return _id != rhs._id; }
	};

	template <typename R, typename... Args>
	class MulticastDelegate<R( Args... )>
	{
		using delegate_type = Delegate<R( Args... )>;

		struct DelegateEntry
		{
			DelegateHandle _handle;
			delegate_type  _delegate;
		};

	public:
		explicit MulticastDelegate()								   = default;
		MulticastDelegate( const MulticastDelegate& other )			   = default;
		MulticastDelegate& operator=( const MulticastDelegate& other ) = default;

		bool operator==( const MulticastDelegate& other ) const
		{
			if ( _delegateList.size() != other._delegateList.size() )
				return false;

			const uint32 delegateCount = static_cast<uint32>( _delegateList.size() );
			for ( uint32 index = 0; index < delegateCount; ++index )
			{
				if ( _delegateList[index]._delegate != other._delegateList[index]._delegate )
					return false;
			}
			return true;
		}

		bool operator!=( const MulticastDelegate& other ) const { return ( *this == other ) == false; }

		bool isBound() const { return _delegateList.empty() == false; }

		void broadcast( Args... args )
		{

			const std::vector<DelegateEntry> snapshot = _delegateList;
			for ( const DelegateEntry& entry : snapshot )
			{
				if ( entry._delegate.isBound() == true )
				{
					entry._delegate( args... );
				}
			}
		}

		DelegateHandle add( const delegate_type& newDelegate )
		{
			static std::atomic<uint64> s_nextHandleId{ 1 };
			DelegateHandle			   handle{ s_nextHandleId.fetch_add( 1, std::memory_order_relaxed ) };
			_delegateList.push_back( DelegateEntry{ handle, newDelegate } );
			return handle;
		}

		void remove( const DelegateHandle& handle )
		{
			if ( handle.isValid() == false )
				return;

			const auto iter = std::find_if( _delegateList.begin(), _delegateList.end(), [&]( const DelegateEntry& entry )
			{
				return entry._handle == handle;
			} );

			if ( iter != _delegateList.end() )
				_delegateList.erase( iter );
		}

		void remove( const delegate_type& target )
		{
			const auto iter = std::find_if( _delegateList.begin(), _delegateList.end(), [&]( const DelegateEntry& entry )
			{
				return entry._delegate == target;
			} );

			if ( iter != _delegateList.end() )
				_delegateList.erase( iter );
		}

		void removeAll() { _delegateList.clear(); }

	private:
		std::vector<DelegateEntry> _delegateList;
	};
}

/** @brief SW_DECLARE_DELEGATE 매크로 정의입니다. */
#define SW_DECLARE_DELEGATE( ReturnType, DelegateName, ... )			using DelegateName = sw::Delegate<ReturnType( __VA_ARGS__ )>
/** @brief SW_DECLARE_MULTI_CAST_DELEGATE 매크로 정의입니다. */
#define SW_DECLARE_MULTI_CAST_DELEGATE( ReturnType, DelegateName, ... ) using DelegateName = sw::MulticastDelegate<ReturnType( __VA_ARGS__ )>
/** @brief SW_DELEGATE_FUNCTION 매크로 정의입니다. */
#define SW_DELEGATE_FUNCTION( DelegateName, Func )						DelegateName::create<Func>()
/** @brief SW_DELEGATE_METHOD 매크로 정의입니다. */
#define SW_DELEGATE_METHOD( DelegateName, Method, Instance )			DelegateName::create<Method>( Instance )
/** @brief SW_DELEGATE_LAMBDA 매크로 정의입니다. */
#define SW_DELEGATE_LAMBDA( DelegateName, ... )							DelegateName::create( __VA_ARGS__ )
