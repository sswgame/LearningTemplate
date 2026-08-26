/**
 * @file Registry.h
 * @brief ECS 레지스트리 — 엔티티, 컴포넌트 풀, 팩토리, 지연 명령
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"

#include "Engine/ECS/ComponentHandle.h"
#include "Engine/ECS/Entity.h"
#include "Engine/EngineMinimal.h"
#include "Engine/Object/Component/Component.h"

#include <atomic>
#include <shared_mutex>
#include <tuple>
#include <utility>

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) 타입 ID — REFLECT_BODY() StaticType()._typeId
	// ------------------------------------------------------------------------------
	template <typename, typename = std::void_t<>>
	struct HasStaticTypeInfo : std::false_type
	{
	};

	template <typename T>
	struct HasStaticTypeInfo<T, std::void_t<decltype( T::StaticType() )>> : std::true_type
	{
	};

	template <typename T>
	/** @brief T::StaticType()->_typeId를 반환합니다. */
	uint32 getEcsTypeId()
	{
		static_assert( HasStaticTypeInfo<T>::value, "Component must unconditionally use REFLECTION (e.g., REFLECT_BODY())" );
		const TypeInfo* pTypeInfo = nullptr;
		if constexpr ( HasStaticType_v<T> )
			pTypeInfo = T::StaticType();
		else if constexpr ( HasReflectStaticType_v<T> )
			pTypeInfo = ReflectTypeTraits<T>::StaticType();
		if ( pTypeInfo == nullptr )
		{
			SW_LOG_ERROR( "[Registry] getEcsTypeId: StaticType() is null for component type!" );
			return 0;
		}
		return pTypeInfo->_typeId;
	}

	/**
	 * @brief ECS 레지스트리 컴포넌트 풀의 타입 소거 베이스
	 */
	class IPoolBase
	{
	public:
		/** @brief 가상 소멸. */
		virtual ~IPoolBase() = default;
		/** @brief 엔티티의 이 타입 컴포넌트를 제거합니다. */
		virtual void remove( Entity entity ) = 0;
		/** @brief 풀을 비웁니다. */
		virtual void clear() = 0;
		/** @brief 내부 컨테이너의 미사용 메모리를 반환합니다. */
		virtual void shrinkToFit() = 0;
		/** @brief 엔티티의 Component 포인터를 반환합니다. 없으면 nullptr. */
		virtual Component* getComponent( Entity entity ) = 0;
		/** @brief 풀의 모든 Component 포인터를 순회합니다. 호출자가 getMutex()를 잠가야 합니다. */
		virtual void forEachComponent( Delegate<void( Component* )> callback ) = 0;
		/** @brief 이 풀의 구조 변경을 직렬화하는 뮤텍스. */
		virtual mutex& getMutex() = 0;
	};

	/**
	 * @brief 엔티티 서명 항목 (타입 ID + 풀 포인터 직접 캐싱)
	 */
	struct EntitySignatureEntry
	{
		uint32	   _typeId{ 0 };
		IPoolBase* _pPool{ nullptr };
	};

	/**
	 * @brief 레지스트리 sparse_set의 타입별 래퍼
	 */
	template <typename T>
	class PoolWrapper : public IPoolBase
	{
	public:
		/** @brief 엔티티의 이 타입 컴포넌트를 제거합니다. */
		void remove( Entity entity ) override
		{
			std::scoped_lock<mutex> lock{ _mutex };
			_uniqueSparseSet.erase( entity.index() );
		}

		/** @brief 풀을 비웁니다. */
		void clear() override
		{
			std::scoped_lock<mutex> lock{ _mutex };
			_uniqueSparseSet.clear();
		}

		/** @brief 내부 컨테이너의 미사용 메모리를 반환합니다. */
		void shrinkToFit() override
		{
			std::scoped_lock<mutex> lock{ _mutex };
			_uniqueSparseSet.shrink_to_fit();
		}

		/** @brief 엔티티의 Component 포인터를 반환합니다. 없으면 nullptr. */
		Component* getComponent( Entity entity ) override
		{
			if constexpr ( std::is_base_of_v<Component, T> )
			{
				std::scoped_lock<mutex> lock{ _mutex };
				const auto&				set = _uniqueSparseSet;
				if ( set.contains( entity.index() ) )
					return const_cast<Component*>( static_cast<const Component*>( &set[entity.index()] ) );
			}
			return nullptr;
		}

		/** @brief 풀의 모든 Component 포인터를 순회합니다. 호출자가 getMutex()를 잠가야 합니다. */
		void forEachComponent( Delegate<void( Component* )> callback ) override
		{
			if constexpr ( std::is_base_of_v<Component, T> )
			{
				for ( auto tuple : _uniqueSparseSet )
				{
					auto& comp = std::get<1>( tuple );
					callback( static_cast<Component*>( &comp ) );
				}
			}
		}

		/** @brief 이 풀의 구조 변경을 직렬화하는 뮤텍스. */
		mutex& getMutex() override { return _mutex; }

		/** @brief 내부 sparse_set을 반환합니다. 호출자가 getMutex()를 잠가야 합니다. */
		sparse_set<T>& getSet() { return _uniqueSparseSet; }
		/** @brief 내부 sparse_set을 반환합니다. */
		const sparse_set<T>& getSet() const { return _uniqueSparseSet; }

	private:
		mutex		  _mutex;
		sparse_set<T> _uniqueSparseSet;
	};

	class Registry;

	// ------------------------------------------------------------------------------
	// 2) CommandBuffer — 워커에서 쌓고 메인에서 execute
	// ------------------------------------------------------------------------------
	class CommandBuffer
	{
	public:
		using Command = Delegate<void( Registry& )>;

		/** @brief 명령을 큐에 넣습니다. */
		void push( Command cmd )
		{
			std::scoped_lock<mutex> lock{ _mutex };
			_listCommands.push_back( std::move( cmd ) );
		}

		/** @brief 쌓인 명령을 레지스트리에 실행합니다. */
		void execute( Registry& registry )
		{
			vector<Command> listCmds;
			{
				std::scoped_lock<mutex> lock{ _mutex };
				listCmds.swap( _listCommands );
			}
			for ( auto& cmd : listCmds )
			{
				cmd( registry );
			}
		}

	private:
		mutex			_mutex;
		vector<Command> _listCommands;
	};

	/**
	 * @brief 엔티티와 컴포넌트 풀을 관리하는 ECS 레지스트리
	 * @details 멀티스레드 시스템이 컴포넌트를 동시에 읽고 쓸 수 있습니다.
	 */
	class SW_API Registry
	{
	public:
		// ------------------------------------------------------------------------------
		// 3) 수명 · 엔티티 생성/파괴
		// ------------------------------------------------------------------------------
		/** @brief 빈 레지스트리를 만듭니다. */
		Registry();
		/** @brief 풀과 엔티티를 정리합니다. */
		~Registry();

		/** @brief 복사를 금지합니다. */
		Registry( const Registry& ) = delete;
		/** @brief 대입을 금지합니다. */
		Registry& operator=( const Registry& ) = delete;

		/** @brief 엔티티를 만듭니다. */
		Entity create();
		/** @brief 엔티티와 컴포넌트를 파괴합니다. */
		void destroy( Entity entity );

		/** @brief 핸들의 세대가 현재 슬롯과 같고 점유 중이면 true. */
		bool isValid( Entity entity ) const;
		/** @brief 점유 중인 인덱스의 현재 핸들을 반환합니다. 없으면 kNullEntity. */
		Entity handleFromIndex( uint32 index ) const;

		/** @brief 현재 활성화된(생존 중인) 엔티티 개수를 반환합니다. */
		size_t getActiveEntityCount() const
		{
			std::shared_lock<std::shared_mutex> lock{ _entityLock };
			return _listActiveEntities.size();
		}

		/** @brief 구조 동결 구간이면 true. 스크립트 tick을 포함합니다. 구조 변경은 CommandBuffer로 미루세요. */
		bool isTicking() const { return _bTicking.load( std::memory_order_acquire ); }

		/** @brief 구조 변경을 지연하는 틱 구간을 시작합니다. */
		void beginTick();
		/** @brief 구조 동결만 해제합니다. CommandBuffer는 실행하지 않습니다. */
		void finishTick();
		/** @brief 쌓인 CommandBuffer를 실행합니다. */
		void flushCommands();
		/** @brief finishTick + flushCommands. */
		void endTick();

		/** @brief 레지스트리 내부 풀 및 엔티티 관리 컨테이너들의 미사용 메모리를 회수합니다. */
		void shrinkToFit();

		/** @brief 모든 엔티티와 컴포넌트 풀을 비우고 상태를 초기화합니다. */
		void clear();

		/** @brief 모든 엔티티와 컴포넌트 풀을 비운 뒤 컨테이너 메모리까지 OS에 반환합니다. */
		void clearAndShrink();

		// ------------------------------------------------------------------------------
		// 4) 컴포넌트 — emplace / remove / has / get
		// ------------------------------------------------------------------------------
		template <typename T, typename... Args>
		/**
		 * @brief 엔티티에 T를 생성하거나 교체합니다.
		 * @details beginTick 중이면 CommandBuffer에 쌓고 나중에 생성되도록 핸들을 반환합니다.
		 * @return 컴포넌트 핸들 (TComponentHandle<T>). 틱 중이거나 생성 전이면 get()이 nullptr을 반환하며,
		 *         flushCommands 이후 컴포넌트가 생성되면 동일 핸들에서 유효한 인스턴스로 자동 resolve됩니다.
		 */
		TComponentHandle<T> emplace( Entity entity, Args&&... args )
		{
			if ( _bTicking.load( std::memory_order_acquire ) )
			{
				if constexpr ( sizeof...( Args ) == 0 )
				{
					_commandBuffer.push( [entity]( Registry& reg )
					{ reg.emplace<T>( entity ); } );
				}
				else
				{
					auto packedArgs = std::make_tuple( std::decay_t<Args>( std::forward<Args>( args ) )... );
					_commandBuffer.push( [entity, packedArgs = std::move( packedArgs )]( Registry& reg ) mutable
					{
						std::apply( [&reg, entity]( auto&&... forwarded )
						{ reg.emplace<T>( entity, std::forward<decltype( forwarded )>( forwarded )... ); },
									std::move( packedArgs ) );
					} );
				}
				return TComponentHandle<T>( entity, this );
			}
			if ( isValid( entity ) == false )
			{
				SW_LOG_ERROR( "[Registry] emplace on invalid entity" );
				SW_ASSERT( false && "Registry::emplace on invalid entity" );
				return TComponentHandle<T>( Entity{}, this );
			}
			auto&  pool	  = getOrCreatePool<T>();
			uint32 typeId = getEcsTypeId<T>();
			{
				std::unique_lock<std::shared_mutex> lock{ _entityLock };
				auto&								sig = _mapEntitySignatures[entity];
				auto								it	= std::lower_bound( sig.begin(), sig.end(), typeId,
																			[]( const EntitySignatureEntry& entry, uint32 val )
												{ return entry._typeId < val; } );
				if ( it == sig.end() || it->_typeId != typeId )
					sig.insert( it, EntitySignatureEntry{ typeId, &pool } );
			}
			std::scoped_lock<mutex> setLock{ pool.getMutex() };
			pool.getSet().emplace( entity.index(), std::forward<Args>( args )... );
			markTickWavesDirty();
			return TComponentHandle<T>( entity, this );
		}

		template <typename T>
		/** @brief 엔티티에서 T를 제거합니다. beginTick 중이면 CommandBuffer로 미룹니다. */
		void remove( Entity entity )
		{
			if ( isValid( entity ) == false )
				return;
			if ( _bTicking.load( std::memory_order_acquire ) )
			{
				_commandBuffer.push( [entity]( Registry& reg )
				{ reg.remove<T>( entity ); } );
				return;
			}
			auto* pool = getPool<T>();
			if ( pool != nullptr )
			{
				{
					std::scoped_lock<mutex> setLock{ pool->getMutex() };
					pool->getSet().erase( entity.index() );
				}
				markTickWavesDirty();
				uint32								typeId = getEcsTypeId<T>();
				std::unique_lock<std::shared_mutex> lock{ _entityLock };
				auto								it = _mapEntitySignatures.find( entity );
				if ( it != _mapEntitySignatures.end() )
				{
					auto& sig	= it->second;
					auto  sigIt = std::lower_bound( sig.begin(), sig.end(), typeId,
													[]( const EntitySignatureEntry& entry, uint32 val )
					 { return entry._typeId < val; } );
					if ( sigIt != sig.end() && sigIt->_typeId == typeId )
						sig.erase( sigIt );
				}
			}
		}

		template <typename T>
		/** @brief 엔티티에 T가 있으면 true. */
		bool has( Entity entity ) const
		{
			if ( isValid( entity ) == false )
				return false;
			auto* pool = getPool<T>();
			if ( pool != nullptr )
			{
				if ( _bTicking.load( std::memory_order_acquire ) )
					return pool->getSet().contains( entity.index() );

				std::scoped_lock<mutex> setLock{ pool->getMutex() };
				const auto&				set = pool->getSet();
				return set.contains( entity.index() );
			}
			return false;
		}

		template <typename T>
		/** @brief 엔티티의 T 참조를 반환합니다. 풀이나 컴포넌트가 없으면 UB이므로 has/getPtr로 먼저 확인하세요. */
		T& get( Entity entity )
		{
			SW_ASSERT( isValid( entity ) );
			auto* pool = getPool<T>();
			SW_ASSERT( pool != nullptr );
			if ( _bTicking.load( std::memory_order_acquire ) )
				return const_cast<T&>( pool->getSet()[entity.index()] );

			std::scoped_lock<mutex> setLock{ pool->getMutex() };
			const auto&				set = pool->getSet();
			return const_cast<T&>( set[entity.index()] );
		}

		template <typename T>
		/** @brief 엔티티의 T const 참조를 반환합니다. 풀이나 컴포넌트가 없으면 UB이므로 has/getPtr로 먼저 확인하세요. */
		const T& get( Entity entity ) const
		{
			SW_ASSERT( isValid( entity ) );
			auto* pool = getPool<T>();
			SW_ASSERT( pool != nullptr );
			if ( _bTicking.load( std::memory_order_acquire ) )
				return pool->getSet()[entity.index()];

			std::scoped_lock<mutex> setLock{ pool->getMutex() };
			const auto&				set = pool->getSet();
			return set[entity.index()];
		}

		template <typename T>
		/** @brief 엔티티의 T 포인터를 반환합니다. 없으면 nullptr.
		 *  @details packed sparse_set 주소입니다. 같은 풀의 삽입·삭제 이후에는 유효하지 않습니다.
		 *           같은 틱에서 얻은 포인터는 웨이브가 끝날 때까지 유효합니다.
		 *           조회 직후 세대를 다시 확인해 인덱스 재사용을 걸러냅니다. 프레임을 넘기려면 핸들을 저장하십시오. */
		T* getPtr( Entity entity )
		{
			if ( isValid( entity ) == false )
				return nullptr;
			auto* pool = getPool<T>();
			if ( pool )
			{
				T* ptr{ nullptr };
				if ( _bTicking.load( std::memory_order_acquire ) )
				{
					const auto& set = pool->getSet();
					if ( set.contains( entity.index() ) )
						ptr = const_cast<T*>( &set[entity.index()] );
				}
				else
				{
					std::scoped_lock<mutex> setLock{ pool->getMutex() };
					const auto&				set = pool->getSet();
					if ( set.contains( entity.index() ) )
						ptr = const_cast<T*>( &set[entity.index()] );
				}
				if ( ptr == nullptr || isValid( entity ) == false )
					return nullptr;
				return ptr;
			}
			return nullptr;
		}

		template <typename T>
		/** @brief 엔티티의 T const 포인터를 반환합니다. 없으면 nullptr. */
		const T* getPtr( Entity entity ) const
		{
			if ( isValid( entity ) == false )
				return nullptr;
			auto* pool = getPool<T>();
			if ( pool )
			{
				const T* ptr = nullptr;
				if ( _bTicking.load( std::memory_order_acquire ) )
				{
					const auto& set = pool->getSet();
					if ( set.contains( entity.index() ) )
						ptr = &set[entity.index()];
				}
				else
				{
					std::scoped_lock<mutex> setLock{ pool->getMutex() };
					const auto&				set = pool->getSet();
					if ( set.contains( entity.index() ) )
						ptr = &set[entity.index()];
				}
				if ( ptr == nullptr || isValid( entity ) == false )
					return nullptr;
				return ptr;
			}
			return nullptr;
		}

		template <typename T, typename Func>
		/** @brief 풀 락을 유지한 상태에서 안전하게 컴포넌 참조에 콜백을 실행합니다. 컴포넌트가 없거나 엔티티가 무효하면 false를 반환합니다. */
		bool withComponent( Entity entity, Func&& func )
		{
			if ( isValid( entity ) == false )
				return false;
			auto* pool = getPool<T>();
			if ( pool == nullptr )
				return false;

			if ( _bTicking.load( std::memory_order_acquire ) )
			{
				auto& set = pool->getSet();
				if ( set.contains( entity.index() ) && isValid( entity ) )
				{
					func( set[entity.index()] );
					return true;
				}
			}
			else
			{
				std::scoped_lock<mutex> setLock{ pool->getMutex() };
				auto&					set = pool->getSet();
				if ( set.contains( entity.index() ) && isValid( entity ) )
				{
					func( set[entity.index()] );
					return true;
				}
			}
			return false;
		}

		template <typename T, typename Func>
		/** @brief 풀 락을 유지한 상태에서 안전하게 const 컴포넌트 참조에 콜백을 실행합니다. */
		bool withComponentConst( Entity entity, Func&& func ) const
		{
			if ( isValid( entity ) == false )
				return false;
			auto* pool = getPool<T>();
			if ( pool == nullptr )
				return false;

			if ( _bTicking.load( std::memory_order_acquire ) )
			{
				const auto& set = pool->getSet();
				if ( set.contains( entity.index() ) && isValid( entity ) )
				{
					func( set[entity.index()] );
					return true;
				}
			}
			else
			{
				std::scoped_lock<mutex> setLock{ pool->getMutex() };
				const auto&				set = pool->getSet();
				if ( set.contains( entity.index() ) && isValid( entity ) )
				{
					func( set[entity.index()] );
					return true;
				}
			}
			return false;
		}

		// ------------------------------------------------------------------------------
		// 5) 조회 — sparse_set, 엔티티의 모든 Component
		// ------------------------------------------------------------------------------
		template <typename T>
		/** @brief View/고급 사용용 sparse_set 포인터. 없으면 nullptr. */
		sparse_set<T>* getSparseSet() const
		{
			auto* pool = getPool<T>();
			return pool ? &pool->getSet() : nullptr;
		}

		/**
		 * @brief 엔티티의 모든 컴포넌트를 스택 메모리를 활용해 안전하게 순회합니다. (SBO 기반 0 힙 할당)
		 * @details 순회 직전 락을 해제하므로 콜백 안에서 addComponent 호출이 가능합니다.
		 */
		template <typename Func>
		void forEachComponent( Entity entity, Func&& func )
		{
			if ( isValid( entity ) == false )
				return;

			small_vector<IPoolBase*, 32> pools;
			{
				std::shared_lock<std::shared_mutex> readLock{ _entityLock };
				auto								it = _mapEntitySignatures.find( entity );
				if ( it == _mapEntitySignatures.end() )
					return;

				const auto& entries = it->second;
				for ( const EntitySignatureEntry& entry : entries )
				{
					if ( entry._pPool != nullptr )
						pools.push_back( entry._pPool );
				}
			}

			small_vector<Component*, 32> comps;
			for ( IPoolBase* pPool : pools )
			{
				Component* pComp = pPool->getComponent( entity );
				if ( pComp != nullptr )
					comps.push_back( pComp );
			}

			for ( Component* pComp : comps )
			{
				func( pComp );
			}
		}

		/**
		 * @brief 엔티티의 모든 Component 포인터를 모읍니다.
		 * @details GameObject::getAllComponents와 달리, ECS의 순수 메모리 상태를 반환하므로
		 *          삭제 대기(pending-kill) 상태인 컴포넌트도 포함됩니다. (완전 소멸 전까지)
		 */
		vector<Component*> getAllComponents( Entity entity )
		{
			vector<Component*> listResult;
			forEachComponent( entity, [&]( Component* pComp )
			{
				listResult.push_back( pComp );
			} );
			return listResult;
		}

		// ------------------------------------------------------------------------------
		// 6) Tick — 웨이브 캐시
		// ------------------------------------------------------------------------------
		/** @brief 캐시된 웨이브 순으로 활성 컴포넌트를 tick합니다. beginTick 이후에 호출하세요. */
		void tickComponents( float32 deltaTime );

		/** @brief Tick 웨이브 캐시를 다음 beginTick에서 다시 만듭니다. */
		void markTickWavesDirty() { _bIsTickWavesDirty.store( true, std::memory_order_release ); }

		// ------------------------------------------------------------------------------
		// 7) 팩토리 — 직렬화/이름 추가, 모듈 일괄 등록
		// ------------------------------------------------------------------------------
		using ComponentFactoryDelegate = Delegate<Component*( Registry&, Entity )>;

		template <typename T>
		/** @brief 타입 이름과 모듈 이름으로 T 팩토리를 등록합니다. */
		void registerComponentType( hashed_string typeName, hashed_string moduleName = hashed_string() )
		{
			static_assert( HasStaticTypeInfo<T>::value, "Component must unconditionally use REFLECTION (e.g., REFLECT_BODY())" );
			_mapFactories[typeName] = []( Registry& reg, Entity entity ) -> Component*
			{
				reg.emplace<T>( entity );
				if constexpr ( std::is_base_of_v<Component, T> )
				{
					Component* pComp = static_cast<Component*>( reg.getPtr<T>( entity ) );
					if ( pComp != nullptr )
					{
						const TypeInfo* pTypeInfo = nullptr;
						if constexpr ( HasStaticType_v<T> )
							pTypeInfo = T::StaticType();
						else if constexpr ( HasReflectStaticType_v<T> )
							pTypeInfo = ReflectTypeTraits<T>::StaticType();
						pComp->setCachedTypeInfo( pTypeInfo );
					}
					return pComp;
				}
				else
					return nullptr;
			};

			if ( _mapFactoryModules.find( typeName ) == _mapFactoryModules.end() )
			{
				if ( moduleName.getHash() != 0 )
					_mapFactoryModules[typeName] = moduleName;
				else if ( _activeModuleName.getHash() != 0 )
					_mapFactoryModules[typeName] = _activeModuleName;
				else
					_mapFactoryModules[typeName] = hashed_string( "Engine" );
			}
		}

		/** @brief 등록된 이름으로 컴포넌트를 추가합니다. */
		Component* addComponentByName( Entity entity, hashed_string typeName, bool bLogWarning = true )
		{
			auto it = _mapFactories.find( typeName );
			if ( it != _mapFactories.end() )
				return it->second( *this, entity );
			if ( bLogWarning )
				SW_LOG_WARNING( "[Registry] Component factory for type '%#' not found (%# factories registered)", typeName.c_str(), static_cast<uint32>( _mapFactories.size() ) );
			return nullptr;
		}

		/** @brief 전역 모듈 팩토리 헤드를 등록합니다. (이후 생성되는 모든 Registry가 상속) */
		static void registerModuleFactoryHead( string_view moduleName, struct ComponentFactoryRegistrar* pHead );
		/** @brief 전역 모듈 팩토리 헤드를 해제합니다. */
		static void unregisterModuleFactoryHead( string_view moduleName );
		/** @brief 모듈의 pending 팩토리 체인을 등록합니다. */
		void registerPendingFactories( string_view moduleName, struct ComponentFactoryRegistrar* pHead );
		/** @brief 해당 모듈이 등록한 팩토리를 모두 해제합니다. */
		void unregisterFactoriesByModule( string_view moduleName );
		/** @brief 모든 컴포넌트의 TypeInfo 캐시를 비웁니다. */
		void clearAllCachedTypeInfo();
		/** @brief 모든 컴포넌트의 TypeInfo 캐시를 다시 붙입니다. */
		void rebindAllCachedTypeInfo();

		/** @brief 등록된 이름→팩토리 맵을 반환합니다. */
		const unordered_map<hashed_string, ComponentFactoryDelegate>& getRegisteredFactories() const { return _mapFactories; }

		/** @brief 엔티티의 T 핸들을 만듭니다. 컴포넌트 유무는 handle.get()에서 확인합니다. */
		template <typename T>
		TComponentHandle<T> handleFor( Entity entity ) { return TComponentHandle<T>( entity, this ); }

		/** @brief 핸들이 가리키는 Component를 찾습니다. 없으면 nullptr.
		 *  @param bIncludePendingKill true면 삭제 예정 인스턴스도 반환합니다. */
		Component* resolve( ComponentHandle handle, bool bIncludePendingKill = false );

		/** @brief 지연 명령 버퍼를 반환합니다. */
		CommandBuffer& getCommandBuffer() { return _commandBuffer; }

		/** @brief entity에서 컴포넌트 인스턴스를 풀에서 제거합니다. 호출자가 onDestroy를 먼저 호출해야 합니다. */
		bool removeComponent( Entity entity, Component* pComp );
		/** @brief 핸들의 엔티티+타입을 풀에서 제거합니다. tick 중이면 CommandBuffer로 미룹니다. */
		bool removeComponent( ComponentHandle handle );

		/** @brief 커맨드 버퍼에 엔티티 파괴를 예약합니다. */
		void destroyDeferred( Entity entity )
		{
			_commandBuffer.push( [entity]( Registry& reg )
			{ reg.destroy( entity ); } );
		}

	private:
		template <typename T>
		/** @brief T 풀을 찾거나 새로 만듭니다. */
		PoolWrapper<T>& getOrCreatePool()
		{
			uint32 typeId = getEcsTypeId<T>();

			{
				std::shared_lock<std::shared_mutex> readLock{ _mapPoolsLock };
				auto								it = _mapPools.find( typeId );
				if ( it != _mapPools.end() )
					return *static_cast<PoolWrapper<T>*>( it->second.get() );
			}

			std::unique_lock<std::shared_mutex> writeLock{ _mapPoolsLock };
			auto								it = _mapPools.find( typeId );
			if ( it != _mapPools.end() )
				return *static_cast<PoolWrapper<T>*>( it->second.get() );

			unique_ptr<PoolWrapper<T>> newPool = make_unique<PoolWrapper<T>>();
			auto*					   ptr	   = newPool.get();
			_mapPools[typeId]				   = std::move( newPool );
			return *ptr;
		}

		template <typename T>
		/** @brief T 풀을 찾습니다. 없으면 nullptr. */
		PoolWrapper<T>* getPool() const
		{
			uint32								typeId = getEcsTypeId<T>();
			std::shared_lock<std::shared_mutex> readLock{ _mapPoolsLock };
			auto								it = _mapPools.find( typeId );
			if ( it != _mapPools.end() )
				return static_cast<PoolWrapper<T>*>( it->second.get() );
			return nullptr;
		}

		bool   isValidUnlocked( Entity entity ) const;
		Entity handleFromIndexUnlocked( uint32 index ) const;

		mutable std::shared_mutex							_entityLock;
		vector<uint32>										_listGenerations;
		vector<uint8>										_listOccupied;
		vector<uint32>										_listFreeIndices;
		vector<Entity>										_listActiveEntities;
		unordered_map<Entity, vector<EntitySignatureEntry>> _mapEntitySignatures;

		CommandBuffer _commandBuffer;

		// emplace/remove 는 워커 스레드에서도 불리므로 풀 뮤텍스 + _bTicking 가드.
		std::atomic<bool>				_bIsTickWavesDirty{ true };
		std::atomic<bool>				_bTicking{ false };
		vector<vector<ComponentHandle>> _listCachedTickWaves;
		TaskStageHandle					_tickStage;

		mutable std::shared_mutex							   _mapPoolsLock;
		unordered_map<size_t, unique_ptr<IPoolBase>>		   _mapPools;
		unordered_map<hashed_string, ComponentFactoryDelegate> _mapFactories;
		unordered_map<hashed_string, hashed_string>			   _mapFactoryModules;
		hashed_string										   _activeModuleName;
	};

	template <typename T>
	ComponentHandle TComponentHandle<T>::untyped() const { return ComponentHandle::make( _entity, getEcsTypeId<T>() ); }

	template <typename T>
	T* TComponentHandle<T>::get() const
	{
		if ( _pRegistry == nullptr )
			return nullptr;
		T* ptr = _pRegistry->getPtr<T>( _entity );
		if constexpr ( std::is_base_of_v<Component, T> )
		{
			if ( ptr != nullptr && ptr->isPendingKill() )
				return nullptr;
		}
		return ptr;
	}

	// ------------------------------------------------------------------------------
	// 8) ComponentFactoryRegistrar — 정적 초기화로 팩토리를 체인에 연결
	// ------------------------------------------------------------------------------
	struct SW_API ComponentFactoryRegistrar
	{
		void ( *_registerFunc )( Registry& ); ///< Registry에 팩토리를 넣는 함수
		ComponentFactoryRegistrar* _pNext;	  ///< 같은 헤드의 다음 registrar

		/** @brief Core.dll 전용 팩토리 리스트 헤드. */
		static ComponentFactoryRegistrar*& getHead();

		/** @brief Core::getHead()에 연결합니다 (Core TU 전용). */
		ComponentFactoryRegistrar( void ( *registerFunc )( Registry& ) );
		/** @brief 모듈 로컬 헤드에 연결합니다 (핫리로드 안전). */
		ComponentFactoryRegistrar( void ( *registerFunc )( Registry& ), ComponentFactoryRegistrar*& moduleHead );
	};

#ifndef SW_COMPONENT_FACTORY_MODULE_HEAD
	#define SW_COMPONENT_FACTORY_MODULE_HEAD() ( ::sw::ComponentFactoryRegistrar::getHead() )
#endif

} // namespace sw
