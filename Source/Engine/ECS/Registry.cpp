#include "pch.h"

#include "Engine/ECS/Registry.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
	static mutex& getGlobalFactoryHeadsMutex()
	{
		static mutex s_mutex;
		return s_mutex;
	}

	static unordered_map<string, ComponentFactoryRegistrar*>& getGlobalFactoryHeads()
	{
		static unordered_map<string, ComponentFactoryRegistrar*> s_mapFactoryHeads;
		return s_mapFactoryHeads;
	}

	static ComponentFactoryRegistrar* _s_engineHead{ nullptr };
	static bool						  _s_engineHeadSealed{ false };

	/**
	 * @brief Registry 생성자: 엔진 기본 및 동적 모듈의 컴포넌트 팩토리들을 수집 등록합니다.
	 */
	Registry::Registry()
	{
		if ( _s_engineHead == nullptr && _s_engineHeadSealed == false )
			_s_engineHead = ComponentFactoryRegistrar::getHead();

		if ( _s_engineHead != nullptr )
			registerPendingFactories( "Engine", _s_engineHead );
		else
			registerPendingFactories( "Engine", ComponentFactoryRegistrar::getHead() );

		vector<std::pair<string, ComponentFactoryRegistrar*>> headsCopy;
		{
			std::scoped_lock<mutex> lock{ getGlobalFactoryHeadsMutex() };
			for ( const auto& [modName, head] : getGlobalFactoryHeads() )
			{
				if ( head != nullptr && modName != "Engine" )
					headsCopy.emplace_back( modName, head );
			}
		}
		for ( const auto& [modName, head] : headsCopy )
		{
			registerPendingFactories( modName, head );
		}
	}

	Registry::~Registry() = default;

	namespace
	{
		/**
		 * @brief 동일한 엔티티에 속한 서로 다른 컴포넌트들이 동일한 병렬 작업에서 동시에 실행되어
		 *        엔티티 내부 상태 레이스 컨디션이 발생하는 것을 방지하기 위해 서브웨이브로 분할합니다.
		 */
		vector<vector<ComponentHandle>> splitWaveByEntity( const vector<ComponentHandle>& wave )
		{
			vector<vector<ComponentHandle>> subwaves;
			vector<unordered_set<Entity>>	occupied;
			for ( const ComponentHandle& handle : wave )
			{
				if ( handle.isValid() == false )
					continue;
				const Entity entity = handle.entity();
				size_t		 slot{ 0 };
				for ( ; slot < subwaves.size(); ++slot )
				{
					if ( occupied[slot].count( entity ) == 0 )
						break;
				}
				if ( slot == subwaves.size() )
				{
					subwaves.emplace_back();
					occupied.emplace_back();
				}
				subwaves[slot].push_back( handle );
				occupied[slot].insert( entity );
			}
			return subwaves;
		}
	} // namespace

	/**
	 * @brief 새로운 엔티티를 생성하고 세대 번호(Generation)를 결합한 고유 식별자를 발급합니다.
	 *
	 * 빈 슬롯(_listFreeIndices)이 있으면 인덱스를 재사용하고 이전 세대 번호를 유지/증가시킵니다.
	 */
	Entity Registry::create()
	{
		std::unique_lock<std::shared_mutex> lock{ _entityLock };
		uint32								index{ 0 };
		uint32								gen{ 1 };
		if ( _listFreeIndices.empty() == false )
		{
			index = _listFreeIndices.back();
			_listFreeIndices.pop_back();
			gen					 = _listGenerations[index];
			_listOccupied[index] = 1;
		}
		else
		{
			index = static_cast<uint32>( _listGenerations.size() );
			_listGenerations.push_back( 1 );
			_listOccupied.push_back( 1 );
			gen = 1;
		}
		const Entity entity = EntityHandle::make( index, gen );
		_listActiveEntities.push_back( entity );
		return entity;
	}

	/**
	 * @brief 대상 엔티티와 해당 엔티티에 부착된 모든 컴포넌트를 완전히 파괴하고 메모리를 회수합니다.
	 *
	 * [동작 세부 단계]:
	 * 1. 틱 중 파괴 방지: 현재 프레임 틱 진행 중(`_bTicking`)인 경우, 이터레이터 무효화를 방지하기 위해 `destroyDeferred` 커맨드 버퍼에 지연 등록합니다.
	 * 2. 엔티티 유효성 검사 및 서명 추출: `_mapEntitySignatures`에서 해당 엔티티가 소유한 컴포넌트 TypeId 목록을 추출하고 엔티티 활성 목록에서 제거합니다.
	 * 3. 세대 번호(Generation) 증가: 동일 슬롯 인덱스가 재사용될 때 이전 EntityHandle이 무효화되도록 Generation 번호를 1 증가시키고 `_listFreeIndices`에 반환합니다.
	 * 4. 컴포넌트 풀 삭제: 추출한 컴포넌트 풀들(`IPoolBase`)에서 엔티티 컴포넌트를 즉시 삭제하고 Tick Wave Dirty 플래그를 세웁니다.
	 */
	void Registry::destroy( Entity entity )
	{
		if ( entity == kNullEntity )
			return;
		if ( _bTicking.load( std::memory_order_acquire ) )
		{
			destroyDeferred( entity );
			return;
		}

		vector<IPoolBase*> listPoolsToRemoveFrom;
		{
			std::unique_lock<std::shared_mutex> lock{ _entityLock };
			if ( isValidUnlocked( entity ) == false )
				return;
			auto it = _mapEntitySignatures.find( entity );
			if ( it != _mapEntitySignatures.end() )
			{
				listPoolsToRemoveFrom.reserve( it->second.size() );
				for ( const auto& entry : it->second )
				{
					if ( entry._pPool != nullptr )
						listPoolsToRemoveFrom.push_back( entry._pPool );
				}
				_mapEntitySignatures.erase( it );
			}
			auto activeIt = std::find( _listActiveEntities.begin(), _listActiveEntities.end(), entity );
			if ( activeIt != _listActiveEntities.end() )
			{
				*activeIt = _listActiveEntities.back();
				_listActiveEntities.pop_back();
			}

			const uint32 index	 = entity.index();
			_listOccupied[index] = 0;
			uint32 nextGen		 = _listGenerations[index] + 1u;
			if ( nextGen == 0 )
				nextGen = 1;
			_listGenerations[index] = nextGen;
			_listFreeIndices.push_back( index );
		}

		for ( IPoolBase* pPool : listPoolsToRemoveFrom )
		{
			pPool->remove( entity );
		}
		if ( listPoolsToRemoveFrom.empty() == false )
			markTickWavesDirty();
	}

	void Registry::shrinkToFit()
	{
		{
			std::unique_lock<std::shared_mutex> lock{ _entityLock };
			_listActiveEntities.shrink_to_fit();
			_listFreeIndices.shrink_to_fit();
			_listGenerations.shrink_to_fit();
			_listOccupied.shrink_to_fit();
		}
		std::shared_lock<std::shared_mutex> lock{ _mapPoolsLock };
		for ( auto& [typeId, pPool] : _mapPools )
		{
			if ( pPool != nullptr )
				pPool->shrinkToFit();
		}
	}

	void Registry::clear()
	{
		{
			std::unique_lock<std::shared_mutex> lock{ _entityLock };
			_listActiveEntities.clear();
			_listFreeIndices.clear();
			_listGenerations.clear();
			_listOccupied.clear();
			_mapEntitySignatures.clear();
		}
		{
			std::shared_lock<std::shared_mutex> lock{ _mapPoolsLock };
			for ( auto& [typeId, pPool] : _mapPools )
			{
				if ( pPool != nullptr )
					pPool->clear();
			}
		}
		_listCachedTickWaves.clear();
		_bIsTickWavesDirty.store( true, std::memory_order_release );
	}

	void Registry::clearAndShrink()
	{
		clear();
		shrinkToFit();
	}

	/**
	 * @brief 엔티티 핸들이 현재 레지스트리에서 유효(생존 중)한지 스레드 안전하게 검사합니다.
	 */
	bool Registry::isValid( Entity entity ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _entityLock };
		return isValidUnlocked( entity );
	}

	/**
	 * @brief 원시 슬롯 인덱스로부터 현재 활성화된 세대 번호가 결합된 유효 Entity 핸들을 조회합니다.
	 */
	Entity Registry::handleFromIndex( uint32 index ) const
	{
		std::shared_lock<std::shared_mutex> lock{ _entityLock };
		return handleFromIndexUnlocked( index );
	}

	/**
	 * @brief 프레임 틱 시작을 선언합니다. 이 플래그가 활성화된 동안의 구조적 변경(생성/삭제)은 커맨드 버퍼에 지연됩니다.
	 */
	void Registry::beginTick()
	{
		_bTicking.store( true, std::memory_order_release );
	}

	/**
	 * @brief 프레임 틱 종료를 선언합니다.
	 */
	void Registry::finishTick()
	{
		_bTicking.store( false, std::memory_order_release );
	}

	/**
	 * @brief 틱 도중 커맨드 버퍼에 쌓인 지연 생성/삭제 요청들을 순차적으로 실행하여 레지스트리에 반영합니다.
	 */
	void Registry::flushCommands()
	{
		_commandBuffer.execute( *this );
	}

	/**
	 * @brief 틱을 종료하고 지연된 커맨드들을 즉시 플러시합니다.
	 */
	void Registry::endTick()
	{
		finishTick();
		flushCommands();
	}

	namespace
	{
		static void resolveAndTickVal( Registry* pRegistry, float32 deltaTime, const ComponentHandle& handle )
		{
			Component* pComp = pRegistry->resolve( handle );
			if ( pComp == nullptr || pComp->isActive() == false )
				return;
			GameObject* pOwner = pComp->getOwner();
			if ( pOwner == nullptr || pOwner->isActiveInHierarchy() == false || pOwner->isPendingKill() )
				return;
			pComp->onTick( deltaTime );
		}

		static void dispatchWaveVal( Registry* pRegistry, float32 deltaTime, const vector<ComponentHandle>& wave )
		{
			if ( wave.empty() )
				return;

			constexpr uint32 kParallelThreshold = 16;
			if ( wave.size() < kParallelThreshold || engine::areEngineServicesBound() == false )
			{
				for ( const ComponentHandle& handle : wave )
					resolveAndTickVal( pRegistry, deltaTime, handle );
				return;
			}

			const ComponentHandle* pRawHandles = wave.data();
			const uint32		   totalCount  = static_cast<uint32>( wave.size() );
			ParallelTaskDelegate   delegate	   = SW_DELEGATE_LAMBDA(
				ParallelTaskDelegate, [pRegistry, deltaTime, pRawHandles, totalCount]( uint32 index )
			{
				if ( index < totalCount )
					resolveAndTickVal( pRegistry, deltaTime, pRawHandles[index] );
			} );

			TaskStageHandle stage  = engine::getTaskManager().createAnonymousStage( "ComponentWave" );
			TaskHandle		handle = engine::getTaskManager().emplaceParallel( totalCount, delegate );
			stage.addTask( handle );
			handle.submit();
			engine::getTaskManager().waitStage( stage );
		}
	} // namespace

	/**
	 * @brief 매 프레임 모든 활성 컴포넌트들을 DAG 순서에 따라 병렬 틱(Update) 처리합니다.
	 *
	 * [병렬 처리 알고리즘 단계]:
	 * 1. Tick Wave 캐시 검사: 컴포넌트 추가/삭제 등으로 Dirty 플래그가 서 있다면 DAG 위상 정렬을 통해 Wave들을 재구성합니다.
	 * 2. Wave 순차 순회: Wave 간에는 종속성이 있으므로 Wave 0 -> Wave 1 -> Wave 2 순으로 실행합니다.
	 * 3. 동일 엔티티 격리: 단일 Wave 내에서 동일 엔티티의 컴포넌트가 병렬 실행되지 않도록 subwave로 분할합니다.
	 * 4. TaskManager 워커 스레드 병렬 실행: `emplaceParallel`을 통해 각 코어에 작업을 분산하고 동기화 대기합니다.
	 */
	void Registry::tickComponents( float32 deltaTime )
	{
		// exchange 로 읽고 지워야 웨이브를 만드는 동안 들어온 변경을 다음 프레임에 놓치지 않습니다.
		if ( _bIsTickWavesDirty.exchange( false, std::memory_order_acq_rel ) )
		{
			{
				std::shared_lock<std::shared_mutex> readLock{ _mapPoolsLock };
				vector<std::unique_lock<mutex>>		poolLocks;
				poolLocks.reserve( _mapPools.size() );
				for ( auto& [typeId, pool] : _mapPools )
				{
					poolLocks.emplace_back( pool->getMutex() );
				}

				array<vector<Component*>, 4> groupList;
				for ( auto& [typeId, pool] : _mapPools )
				{
					pool->forEachComponent( [&]( Component* pComp )
					{
						if ( pComp == nullptr )
							return;
						const uint8 groupIndex = static_cast<uint8>( pComp->getTickGroup() );
						if ( groupIndex < groupList.size() )
							groupList[groupIndex].push_back( pComp );
					} );
				}

				_listCachedTickWaves.clear();
				for ( const vector<Component*>& wave : groupList )
				{
					if ( wave.empty() )
						continue;
					vector<ComponentHandle> listTargets;
					listTargets.reserve( wave.size() );
					for ( Component* pComp : wave )
					{
						listTargets.push_back( pComp->getHandle() );
					}
					vector<vector<ComponentHandle>> subwaves = splitWaveByEntity( listTargets );
					for ( auto& subwave : subwaves )
					{
						if ( subwave.empty() == false )
							_listCachedTickWaves.push_back( std::move( subwave ) );
					}
				}
			}
		}

		for ( const vector<ComponentHandle>& wave : _listCachedTickWaves )
		{
			if ( wave.empty() )
				continue;

			dispatchWaveVal( this, deltaTime, wave );
		}
	}

	/**
	 * @brief 동적 로드된 DLL 모듈의 컴포넌트 팩토리 체인 헤드를 전역 사전에 등록합니다.
	 */
	void Registry::registerModuleFactoryHead( string_view moduleName, ComponentFactoryRegistrar* pHead )
	{
		_s_engineHeadSealed = true;
		SW_LOG_INFO( "[Registry] registerModuleFactoryHead module=%#, head=%#", moduleName.data(), pHead );
		std::scoped_lock<mutex> lock{ getGlobalFactoryHeadsMutex() };
		if ( pHead == nullptr )
			getGlobalFactoryHeads().erase( string( moduleName ) );
		else
			getGlobalFactoryHeads()[string( moduleName )] = pHead;
	}

	/**
	 * @brief 언로드되는 DLL 모듈의 컴포넌트 팩토리 체인 헤드를 전역 사전에서 제거합니다.
	 */
	void Registry::unregisterModuleFactoryHead( string_view moduleName )
	{
		std::scoped_lock<mutex> lock{ getGlobalFactoryHeadsMutex() };
		getGlobalFactoryHeads().erase( string( moduleName ) );
	}

	/**
	 * @brief 대기 중인 컴포넌트 팩토리 체인을 레지스트리 내부 사전에 등록합니다.
	 */
	void Registry::registerPendingFactories( string_view moduleName, ComponentFactoryRegistrar* pHead )
	{
		if ( pHead == nullptr )
			return;

		{
			std::scoped_lock<mutex> lock{ getGlobalFactoryHeadsMutex() };
			getGlobalFactoryHeads()[string( moduleName )] = pHead;
		}
		_activeModuleName					= hashed_string( moduleName.data(), static_cast<uint32>( moduleName.size() ) );
		ComponentFactoryRegistrar* pCurrent = pHead;
		while ( pCurrent != nullptr )
		{
			if ( pCurrent->_registerFunc != nullptr )
				pCurrent->_registerFunc( *this );
			pCurrent = pCurrent->_pNext;
		}
		_activeModuleName = hashed_string();
	}

	/**
	 * @brief 특정 모듈(DLL)에 의해 등록되었던 모든 컴포넌트 팩토리들을 레지스트리에서 제거합니다. (모듈 언로드 시 호출)
	 */
	void Registry::unregisterFactoriesByModule( string_view moduleName )
	{
		const hashed_string hashModule( moduleName.data(), static_cast<uint32>( moduleName.size() ) );
		for ( auto it = _mapFactoryModules.begin(); it != _mapFactoryModules.end(); )
		{
			if ( it->second == hashModule )
			{
				_mapFactories.erase( it->first );
				it = _mapFactoryModules.erase( it );
			}
			else
				++it;
		}
	}

	/**
	 * @brief 핫 리로드 직전 모든 활성 컴포넌트의 캐시된 TypeInfo 포인터를 nullptr로 초기화합니다.
	 */
	void Registry::clearAllCachedTypeInfo()
	{
		std::shared_lock<std::shared_mutex> readLock{ _mapPoolsLock };
		for ( auto& [typeId, pool] : _mapPools )
		{
			std::scoped_lock<mutex> lock{ pool->getMutex() };
			pool->forEachComponent( []( Component* pComp )
			{
				if ( pComp != nullptr )
					pComp->clearCachedTypeInfo();
			} );
		}
	}

	/**
	 * @brief 핫 리로드 완료 후 새로 갱신된 TypeRegistry로부터 모든 컴포넌트의 TypeInfo 포인터를 다시 바인딩합니다.
	 */
	void Registry::rebindAllCachedTypeInfo()
	{
		TypeRegistry&						typeRegistry = engine::getTypeRegistry();
		std::shared_lock<std::shared_mutex> readLock{ _mapPoolsLock };
		for ( auto& [typeId, pool] : _mapPools )
		{
			std::scoped_lock<mutex> lock{ pool->getMutex() };
			pool->forEachComponent( [&typeRegistry]( Component* pComp )
			{
				if ( pComp == nullptr )
					return;
				const hashed_string typeKey = pComp->getComponentName();
				if ( typeKey.empty() )
					return;
				pComp->setCachedTypeInfo( typeRegistry.findType( typeKey ) );
			} );
		}

		// 리로드 후 풀 구성과 tick 의존성이 바뀌었을 수 있으므로 웨이브를 다시 만듭니다.
		markTickWavesDirty();
	}

	/**
	 * @brief 컴포넌트 핸들(Entity + TypeId)로부터 실제 컴포넌트 원시 인스턴스 포인터를 O(1) 조회합니다.
	 *
	 * @param handle 조회할 컴포넌트 핸들
	 * @param bIncludePendingKill 삭제 대기 중인(PendingKill) 컴포넌트도 포함할지 여부
	 * @return 유효하지 않거나 삭제된 컴포넌트면 nullptr, 유효하면 Component 포인터
	 */
	Component* Registry::resolve( ComponentHandle handle, bool bIncludePendingKill )
	{
		if ( handle.isValid() == false )
			return nullptr;
		const Entity entity = handle.entity();
		if ( isValid( entity ) == false )
			return nullptr;

		Component* pComp{ nullptr };
		{
			std::shared_lock<std::shared_mutex> readLock{ _mapPoolsLock };
			auto								it = _mapPools.find( handle.typeId() );
			if ( it != _mapPools.end() )
				pComp = it->second->getComponent( entity );
			else
			{
				for ( auto& [typeId, pool] : _mapPools )
				{
					pComp = pool->getComponent( entity );
					if ( pComp != nullptr )
						break;
				}
			}
		}
		if ( pComp == nullptr || isValid( entity ) == false )
			return nullptr;
		if ( bIncludePendingKill == false && pComp->isPendingKill() )
			return nullptr;
		return pComp;
	}

	/**
	 * @brief 엔티티로부터 컴포넌트 인스턴스를 지정하여 제거합니다.
	 */
	bool Registry::removeComponent( Entity entity, Component* pComp )
	{
		if ( entity == kNullEntity || pComp == nullptr )
			return false;

		ComponentHandle handle{};
		{
			std::shared_lock<std::shared_mutex> lock{ _mapPoolsLock };
			for ( auto& [typeId, pool] : _mapPools )
			{
				if ( pool->getComponent( entity ) == pComp )
				{
					handle = ComponentHandle::make( entity, static_cast<uint32>( typeId ) );
					break;
				}
			}
		}

		if ( handle.isValid() == false )
			return false;
		return removeComponent( handle );
	}

	/**
	 * @brief 컴포넌트 핸들을 통해 해당 컴포넌트를 소유한 엔티티의 풀에서 컴포넌트를 완전히 삭제합니다.
	 */
	bool Registry::removeComponent( ComponentHandle handle )
	{
		if ( handle.isValid() == false )
			return false;
		if ( _bTicking.load( std::memory_order_acquire ) )
		{
			_commandBuffer.push( [handle]( Registry& reg )
			{ reg.removeComponent( handle ); } );
			return true;
		}

		const Entity entity		   = handle.entity();
		const uint32 matchedTypeId = handle.typeId();
		IPoolBase*	 pool{ nullptr };
		{
			std::shared_lock<std::shared_mutex> lock{ _mapPoolsLock };
			auto								it = _mapPools.find( matchedTypeId );
			if ( it != _mapPools.end() )
				pool = it->second.get();
		}
		if ( pool == nullptr || pool->getComponent( entity ) == nullptr )
			return false;

		pool->remove( entity );
		{
			std::unique_lock<std::shared_mutex> lock{ _entityLock };
			auto								it = _mapEntitySignatures.find( entity );
			if ( it != _mapEntitySignatures.end() )
			{
				auto& sig	= it->second;
				auto  sigIt = std::lower_bound( sig.begin(), sig.end(), matchedTypeId,
												[]( const EntitySignatureEntry& entry, uint32 val )
				{ return entry._typeId < val; } );
				if ( sigIt != sig.end() && sigIt->_typeId == matchedTypeId )
					sig.erase( sigIt );
			}
		}
		markTickWavesDirty();
		return true;
	}

	/**
	 * @brief 락이 이미 획득된 상태에서 내부적으로 엔티티의 세대 번호 유효성을 검사합니다.
	 */
	bool Registry::isValidUnlocked( Entity entity ) const
	{
		if ( entity.isValid() == false )
			return false;
		const uint32 index = entity.index();
		if ( index >= _listGenerations.size() || _listOccupied[index] == 0 )
			return false;
		return _listGenerations[index] == entity.generation();
	}

	/**
	 * @brief 락이 이미 획득된 상태에서 내부적으로 인덱스에 매칭되는 Entity 핸들을 생성합니다.
	 */
	Entity Registry::handleFromIndexUnlocked( uint32 index ) const
	{
		if ( index >= _listGenerations.size() || _listOccupied[index] == 0 )
			return kNullEntity;
		return EntityHandle::make( index, _listGenerations[index] );
	}

	/**
	 * @brief 전역 컴포넌트 팩토리 단일 연결 리스트의 헤드 포인터를 반환합니다.
	 */
	ComponentFactoryRegistrar*& ComponentFactoryRegistrar::getHead()
	{
		static ComponentFactoryRegistrar* s_pHead{ nullptr };
		return s_pHead;
	}

	ComponentFactoryRegistrar::ComponentFactoryRegistrar( void ( *registerFunc )( Registry& ) )
		: _registerFunc{ registerFunc }
		, _pNext{ getHead() }
	{
		getHead() = this;
		if ( _s_engineHeadSealed == false )
			_s_engineHead = this;
	}

	ComponentFactoryRegistrar::ComponentFactoryRegistrar( void ( *registerFunc )( Registry& ), ComponentFactoryRegistrar*& moduleHead )
		: _registerFunc{ registerFunc }
		, _pNext{ moduleHead }
	{
		moduleHead = this;
		if ( _s_engineHeadSealed == false && &moduleHead == &getHead() )
			_s_engineHead = this;
	}
} // namespace sw
