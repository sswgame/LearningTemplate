/**
 * @file EventDispatcher.h
 * @brief 발행-구독(Publish-Subscribe) 패턴을 구현하는 중앙 이벤트 디스패처
 * @details 채널(Channel)별로 이벤트를 브로드캐스트할 수 있으며, 동기식(Publish) 및 비동기식(Push) 이벤트 큐잉을 지원합니다.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Event/EventType.h"
#include "Core/Memory/LinearAllocator.h"
#include "Core/String/hashed_string.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) EventDispatcher — subscribe / publish(동기) / push(프레임 큐) / dispatchAll
	//    채널+타입별 멀티캐스트. 기본 채널은 빈 hashed_string
	// ------------------------------------------------------------------------------
	/**
	 * @class EventDispatcher
	 * @brief 채널 기반의 멀티캐스트 이벤트 전달자
	 */
	class SW_API EventDispatcher
	{
	public:
		/** @brief 버스·큐 락과 더블 버퍼 아레나를 준비합니다. */
		EventDispatcher();
		/** @brief 구독·대기 큐를 비웁니다. */
		~EventDispatcher();

		/** @brief 구독 해제에 쓰는 채널·타입·핸들 토큰입니다. */
		struct EventSubscription
		{
			hashed_string  _channel;
			EventTypeId	   _eventType;
			DelegateHandle _handle;
		};

		/** @brief 이벤트를 구독합니다. */
		template <typename T>
		EventSubscription subscribe( const Delegate<void( const T& )>& delegate ) { return subscribe<T>( getDefaultChannel(), delegate ); }

		/** @brief 이벤트를 구독합니다. */
		template <typename T>
		EventSubscription subscribe( hashed_string channel, const Delegate<void( const T& )>& delegate )
		{
			DelegateHandle handle = getOrCreateChannelDelegate<T>( channel )->add( delegate );
			return EventSubscription{ channel, T::kType, handle };
		}

		/** @brief 이벤트 구독을 해제합니다. */
		void unsubscribe( const EventSubscription& token )
		{
			std::scoped_lock<SpinLock>		 lock{ _busSpinLock };
			pair<hashed_string, EventTypeId> key( token._channel, token._eventType );
			auto							 iter = _mapChannelDelegate.find( key );
			if ( iter != _mapChannelDelegate.end() )
				std::static_pointer_cast<IMulticastDelegateBase>( iter->second )->remove( token._handle );
		}

		/** @brief 이벤트 구독을 해제합니다. */
		template <typename T>
		void unsubscribe( const Delegate<void( const T& )>& delegate ) { unsubscribe<T>( getDefaultChannel(), delegate ); }

		/** @brief 이벤트 구독을 해제합니다. */
		template <typename T>
		void unsubscribe( hashed_string channel, const Delegate<void( const T& )>& delegate ) { getOrCreateChannelDelegate<T>( channel )->remove( delegate ); }

		/** @brief 이벤트를 즉시 발행합니다. */
		template <typename T>
		void publish( const T& event ) { publish<T>( getDefaultChannel(), event ); }

		/** @brief 이벤트를 즉시 발행합니다. */
		template <typename T>
		void publish( hashed_string channel, const T& event )
		{
			shared_ptr<MulticastDelegate<void( const T& )>> mcast = getOrCreateChannelDelegate<T>( channel );
			mcast->broadcast( event );
		}

		/** @brief 기본 채널 큐에 넣고, dispatchAll 때 방송합니다. */
		template <typename T>
		void push( const T& event ) { enqueueEvent<T>( getDefaultChannel(), event ); }

		/** @brief 지정 채널 큐에 넣고, dispatchAll 때 방송합니다. */
		template <typename T>
		void push( hashed_string channel, const T& event ) { enqueueEvent<T>( channel, event ); }

		/** @brief 이벤트를 큐에 넣습니다. */
		template <typename T>
		void enqueueEvent( const T& event ) { enqueueEvent<T>( getDefaultChannel(), event ); }

		/** @brief 이벤트를 큐에 넣습니다. */
		template <typename T>
		void enqueueEvent( hashed_string channel, const T& event )
		{
			std::scoped_lock<SpinLock> lock{ _queueSpinLock };
			const int32				   allocIdx = _activeAllocatorIndex.load( std::memory_order_relaxed );

			void* pMem = _arrFrameAllocator[allocIdx].allocate( sizeof( T ), alignof( T ) );
			if ( pMem == nullptr )
			{
				pMem = Memory::allocMemory( sizeof( T ) );
				if ( pMem == nullptr )
					return;
				_arrListOverflowAllocation[allocIdx].push_back( pMem );
			}

			T* pQueuedEvent = new ( pMem ) T( event );

			auto iter = _mapChannelQueue.find( channel );
			if ( iter == _mapChannelQueue.end() )
			{
				auto [newIter, bInserted] = _mapChannelQueue.emplace( channel, make_unique<ChannelEventList>() );
				iter					  = newIter;
			}

			ChannelEventList* pList	   = iter->second.get();
			IEvent*			  pOldHead = pList->_pHead.load( std::memory_order_relaxed );
			pQueuedEvent->_next.store( pOldHead, std::memory_order_relaxed );
			pList->_pHead.store( pQueuedEvent, std::memory_order_release );
		}

		/** @brief 큐에 쌓인 이벤트를 모두 방송하고 프레임 아레나를 리셋합니다. */
		void processEvents();

		/** @brief 모든 구독과 큐를 초기화합니다. */
		void clear();

		/** @brief 기본 채널 이름을 반환합니다. */
		static const hashed_string& getDefaultChannel()
		{
			static const hashed_string s_defaultChannel{ "DefaultChannel" };
			return s_defaultChannel;
		}

		/** @brief 큐에 대기 중인 이벤트 수를 반환합니다. */
		size_t getPendingEventCount() const
		{
			std::scoped_lock<SpinLock> lock{ _queueSpinLock };
			size_t					   count = 0;
			for ( const auto& [channel, list] : _mapChannelQueue )
			{
				IEvent* pCurr = list->_pHead.load( std::memory_order_relaxed );
				while ( pCurr != nullptr )
				{
					count++;
					pCurr = pCurr->_next.load( std::memory_order_relaxed );
				}
			}
			return count;
		}

	private:
		/** @brief (채널, 이벤트 타입) 쌍을 해시합니다. */
		struct HashPair
		{
			/** @brief 채널 해시와 타입 ID를 섞습니다. */
			size_t operator()( const pair<hashed_string, EventTypeId>& pair ) const
			{
				size_t h1 = std::hash<hashed_string>{}( pair.first );
				size_t h2 = std::hash<uint32>{}( pair.second );
				return h1 ^ ( h2 << 1 );
			}
		};

		/** @brief 한 채널의 lock-free 이벤트 연결 리스트 헤드입니다. */
		struct ChannelEventList
		{
			atomic<IEvent*> _pHead{ nullptr };
		};

		/** @brief 타입 소거된 채널 방송. 람다 없이 함수 포인터 + 멀티캐스트입니다. */
		struct ChannelDispatchEntry
		{
			using BroadcastFn = void ( * )( void* pMulticast, const IEvent& eventRef );

			BroadcastFn		 _pfnBroadcast{ nullptr };
			shared_ptr<void> _pMulticast;

			/** @brief 엔트리가 호출 가능하면 true입니다. */
			bool isBound() const { return _pfnBroadcast != nullptr && _pMulticast != nullptr; }
			/** @brief 저장된 멀티캐스트로 이벤트를 방송합니다. */
			void invoke( const IEvent& eventRef ) const
			{
				if ( isBound() )
					_pfnBroadcast( _pMulticast.get(), eventRef );
			}
		};

		/** @brief typed 멀티캐스트에 IEvent를 캐스팅해 방송합니다. */
		template <typename T>
		static void broadcastTypedChannel( void* pMulticast, const IEvent& eventRef )
		{
			static_cast<MulticastDelegate<void( const T& )>*>( pMulticast )->broadcast( static_cast<const T&>( eventRef ) );
		}

		/** @brief 채널+타입 멀티캐스트를 찾거나 만듭니다. */
		template <typename T>
		shared_ptr<MulticastDelegate<void( const T& )>> getOrCreateChannelDelegate( hashed_string channel )
		{
			pair<hashed_string, EventTypeId> key( channel, T::kType );

			std::scoped_lock<SpinLock>																	lock{ _busSpinLock };
			unordered_map<pair<hashed_string, EventTypeId>, shared_ptr<void>, HashPair>::const_iterator iter = _mapChannelDelegate.find( key );
			if ( iter != _mapChannelDelegate.end() )
				return std::static_pointer_cast<MulticastDelegate<void( const T& )>>( iter->second );

			shared_ptr<MulticastDelegate<void( const T& )>> mcast = sw::make_shared<MulticastDelegate<void( const T& )>>();
			_mapChannelDelegate[key]							  = mcast;
			_mapChannelDispatchTable[key]						  = ChannelDispatchEntry{ &broadcastTypedChannel<T>, mcast };
			return mcast;
		}

	private:
		mutable SpinLock																_busSpinLock;
		mutable SpinLock																_queueSpinLock;
		unordered_map<pair<hashed_string, EventTypeId>, shared_ptr<void>, HashPair>		_mapChannelDelegate;
		unordered_map<pair<hashed_string, EventTypeId>, ChannelDispatchEntry, HashPair> _mapChannelDispatchTable;
		unordered_map<hashed_string, unique_ptr<ChannelEventList>>						_mapChannelQueue;

		LinearAllocator _arrFrameAllocator[2];
		vector<void*>	_arrListOverflowAllocation[2];
		atomic<int32>	_activeAllocatorIndex;
	};
} // namespace sw
