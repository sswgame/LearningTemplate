#pragma once
/**
 * @file EventDispatcher.h
 * @brief 발행-구독(Publish-Subscribe) 패턴을 구현하는 중앙 이벤트 디스패처
 * @details 채널(Channel)별로 이벤트를 브로드캐스트할 수 있으며, 동기식(Publish) 및 비동기식(Push) 이벤트 큐잉을 지원합니다.
 */#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

#include "Core/Utility/Delegate/Delegate.h"
#include "EventType.h"

namespace sw
{
	/**
	 * @class EventDispatcher
	 * @brief 채널 기반의 멀티캐스트 이벤트 전달자
	 */	class SW_API EventDispatcher
	{
	public:
		SW_INLINE static hashed_string getDefaultChannel()
		{
			static const hashed_string kDefaultChannel{ "" };
			return kDefaultChannel;
		}

	public:
		EventDispatcher()  = default;
		~EventDispatcher() = default;

		template <typename T>
		void subscribe( const Delegate<void( const T& )>& delegate )
		{
			subscribe<T>( getDefaultChannel(), delegate );
		}

		template <typename T>
		void subscribe( hashed_string channel, const Delegate<void( const T& )>& delegate )
		{
			getOrCreateChannelDelegate<T>( channel )->add( delegate );
		}

		template <typename T>
		void unsubscribe( const Delegate<void( const T& )>& delegate )
		{
			unsubscribe<T>( getDefaultChannel(), delegate );
		}

		template <typename T>
		void unsubscribe( hashed_string channel, const Delegate<void( const T& )>& delegate )
		{
			getOrCreateChannelDelegate<T>( channel )->remove( delegate );
		}

		template <typename T>
		void publish( const T& event )
		{
			publish<T>( getDefaultChannel(), event );
		}

		template <typename T>
		void publish( hashed_string channel, const T& event )
		{
			std::shared_ptr<MulticastDelegate<void( const T& )>> mcast = getOrCreateChannelDelegate<T>( channel );
			mcast->broadcast( event );
		}

		template <typename T>
		void push( const T& event )
		{
			enqueueEvent<T>( getDefaultChannel(), event );
		}

		template <typename T>
		void push( hashed_string channel, const T& event )
		{
			enqueueEvent<T>( channel, event );
		}

		template <typename T>
		void enqueueEvent( const T& event )
		{
			enqueueEvent<T>( getDefaultChannel(), event );
		}

		template <typename T>
		void enqueueEvent( hashed_string channel, const T& event )
		{
			/**
			 * @brief 내부 뮤텍스를 잠급니다
			 */
			std::lock_guard<std::mutex> lock( _queueMutex );
			auto [iter, inserted] = _channelQueue.try_emplace( channel );
			iter->second.push_back( std::make_unique<T>( event ) );
		}

		void dispatchAll()
		{
			processEvents();
		}

		/**
		 * @brief 이벤트를 처리합니다
		 */
		void processEvents();

		SW_INLINE uint32 getPendingEventCount( hashed_string channel = getDefaultChannel() ) const
		{
			/**
			 * @brief 내부 뮤텍스를 잠급니다
			 */
			std::lock_guard<std::mutex>																lock( _queueMutex );
			std::unordered_map<hashed_string, std::vector<std::unique_ptr<IEvent>>>::const_iterator iter = _channelQueue.find( channel );
			if ( iter != _channelQueue.end() )
			{
				return static_cast<uint32>( iter->second.size() );
			}
			return 0;
		}

		/**
		 * @brief 내부 상태를 비웁니다
		 */
		void clear();

	private:
		template <typename T>
		std::shared_ptr<MulticastDelegate<void( const T& )>> getOrCreateChannelDelegate( hashed_string channel )
		{
			std::pair<hashed_string, EventType> key( channel, T::kType );

			/**
			 * @brief 내부 뮤텍스를 잠급니다
			 */
			std::lock_guard<std::mutex>																				 lock( _busMutex );
			std::unordered_map<std::pair<hashed_string, EventType>, std::shared_ptr<void>, HashPair>::const_iterator iter = _channelDelegates.find( key );
			if ( iter != _channelDelegates.end() )
			{
				return std::static_pointer_cast<MulticastDelegate<void( const T& )>>( iter->second );
			}

			std::shared_ptr<MulticastDelegate<void( const T& )>> mcast = std::make_shared<MulticastDelegate<void( const T& )>>();
			_channelDelegates[key]									   = mcast;

			_channelDispatchTable[key] = SW_DELEGATE_LAMBDA( ChannelEventCallback, [mcast]( const IEvent& e )
			{
				mcast->broadcast( static_cast<const T&>( e ) );
			} );

			return mcast;
		}

	private:
		struct HashPair
		{
			std::size_t operator()( const std::pair<hashed_string, EventType>& p ) const
			{
				std::size_t h1 = std::hash<hashed_string>{}( p.first );
				std::size_t h2 = std::hash<uint32>{}( static_cast<uint32>( p.second ) );
				return h1 ^ ( h2 << 1 );
			}
		};

		SW_DECLARE_DELEGATE( void, ChannelEventCallback, const IEvent& );

		mutable std::mutex																		 _busMutex;
		std::unordered_map<std::pair<hashed_string, EventType>, std::shared_ptr<void>, HashPair> _channelDelegates;
		std::unordered_map<std::pair<hashed_string, EventType>, ChannelEventCallback, HashPair>	 _channelDispatchTable;

		mutable std::mutex														_queueMutex;
		std::unordered_map<hashed_string, std::vector<std::unique_ptr<IEvent>>> _channelQueue;
		std::unordered_map<hashed_string, std::vector<std::unique_ptr<IEvent>>> _swapBatchMap;
	};
}
