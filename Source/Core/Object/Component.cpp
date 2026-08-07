/**
 * @file Component.cpp
 * @brief Component 기반 클래스 구현
 */
#include "pch.h"
#include "Component.h"
#include "Core/Reflection/ReflectionCore.h"
#include "Core/Object/GameObject.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	uint64 Component::_s_nextComponentId = 1;

	namespace
	{
		uint64 makeCycleFingerprint( const std::vector<Component*>& nodes )
		{
			uint64 hash = 14695981039346656037ull;
			std::vector<uint64> ids;
			ids.reserve( nodes.size() );
			for ( Component* c : nodes )
			{
				if ( c != nullptr )
					ids.push_back( c->getComponentId() );
			}
			std::sort( ids.begin(), ids.end() );
			for ( uint64 id : ids )
			{
				hash ^= id;
				hash *= 1099511628211ull;
			}
			return hash;
		}

		void logTickCycleOnce( const std::vector<Component*>& groupNodes )
		{
			static std::unordered_set<uint64> s_loggedCycles;
			const uint64					  key = makeCycleFingerprint( groupNodes );
			if ( s_loggedCycles.insert( key ).second == false )
				return;

			SW_LOG_WARNING( "[TickOrder] Cyclic tick dependency detected among %# components in TickGroup %# — keeping stable order.",
							groupNodes.size(),
							groupNodes.empty() || groupNodes.front() == nullptr
								? 0u
								: static_cast<uint32>( groupNodes.front()->getTickGroup() ) );
		}

		/** @brief Kahn topo within one TickGroup. Returns false if a cycle was found. */
		bool topoSortGroup( std::vector<Component*>& group )
		{
			const size_t n = group.size();
			if ( n <= 1 )
				return true;

			std::unordered_map<Component*, size_t> indexOf;
			indexOf.reserve( n );
			for ( size_t i = 0; i < n; ++i )
				indexOf[group[i]] = i;

			std::vector<uint32>				 inDegree( n, 0 );
			std::vector<std::vector<size_t>> edges( n );

			for ( size_t i = 0; i < n; ++i )
			{
				Component* comp = group[i];
				if ( comp == nullptr )
					continue;

				for ( Component* dep : comp->getTickDependencies() )
				{
					if ( dep == nullptr || dep == comp )
						continue;
					auto it = indexOf.find( dep );
					if ( it == indexOf.end() )
						continue; // cross-group / external dep: TickGroup order already handles it

					// dep must tick before comp → edge dep -> comp
					edges[it->second].push_back( i );
					++inDegree[i];
				}
			}

			std::vector<size_t> queue;
			queue.reserve( n );
			for ( size_t i = 0; i < n; ++i )
			{
				if ( inDegree[i] == 0 )
					queue.push_back( i );
			}

			std::vector<Component*> sorted;
			sorted.reserve( n );
			size_t head = 0;
			while ( head < queue.size() )
			{
				const size_t u = queue[head++];
				sorted.push_back( group[u] );
				for ( size_t v : edges[u] )
				{
					if ( --inDegree[v] == 0 )
						queue.push_back( v );
				}
			}

			if ( sorted.size() != n )
			{
				logTickCycleOnce( group );
				return false; // keep original stable order
			}

			group.swap( sorted );
			return true;
		}

		void collectByTickGroup( const std::vector<Component*>& components,
								 std::array<std::vector<Component*>, 4>& outGroups )
		{
			for ( auto& g : outGroups )
				g.clear();

			for ( Component* comp : components )
			{
				if ( comp == nullptr )
					continue;
				const uint8 groupIdx = static_cast<uint8>( comp->getTickGroup() );
				if ( groupIdx < outGroups.size() )
					outGroups[groupIdx].push_back( comp );
			}
		}

		void buildWavesForGroup( const std::vector<Component*>& group, std::vector<std::vector<Component*>>& outWaves )
		{
			const size_t n = group.size();
			if ( n == 0 )
				return;
			if ( n == 1 )
			{
				outWaves.push_back( group );
				return;
			}

			std::unordered_map<Component*, size_t> indexOf;
			indexOf.reserve( n );
			for ( size_t i = 0; i < n; ++i )
				indexOf[group[i]] = i;

			std::vector<uint32>				 inDegree( n, 0 );
			std::vector<std::vector<size_t>> edges( n );

			for ( size_t i = 0; i < n; ++i )
			{
				Component* comp = group[i];
				if ( comp == nullptr )
					continue;
				for ( Component* dep : comp->getTickDependencies() )
				{
					if ( dep == nullptr || dep == comp )
						continue;
					auto it = indexOf.find( dep );
					if ( it == indexOf.end() )
						continue;
					edges[it->second].push_back( i );
					++inDegree[i];
				}
			}

			std::vector<size_t> currentWave;
			currentWave.reserve( n );
			std::vector<uint32> remaining = inDegree;
			std::vector<uint8>	placed( n, 0 );
			size_t				placedCount = 0;

			while ( placedCount < n )
			{
				currentWave.clear();
				for ( size_t i = 0; i < n; ++i )
				{
					if ( placed[i] == 0 && remaining[i] == 0 )
						currentWave.push_back( i );
				}

				if ( currentWave.empty() )
				{
					// Cycle: dump remaining as one serial wave (stable input order) and stop.
					logTickCycleOnce( group );
					std::vector<Component*> rest;
					rest.reserve( n - placedCount );
					for ( size_t i = 0; i < n; ++i )
					{
						if ( placed[i] == 0 )
							rest.push_back( group[i] );
					}
					outWaves.push_back( std::move( rest ) );
					return;
				}

				std::vector<Component*> waveComps;
				waveComps.reserve( currentWave.size() );
				for ( size_t i : currentWave )
				{
					waveComps.push_back( group[i] );
					placed[i] = 1;
					++placedCount;
				}
				outWaves.push_back( std::move( waveComps ) );

				for ( size_t i : currentWave )
				{
					for ( size_t v : edges[i] )
					{
						if ( remaining[v] > 0 )
							--remaining[v];
					}
				}
			}
		}
	} // namespace

	Component::Component()
		: _componentId{ _s_nextComponentId++ }
		, _tickGroup{ static_cast<uint8>( TickGroup::DuringPhysics ) }
		, _bActive{ 1 }
		, _reserved{ 0 }
	{
	}

	const TypeInfo* Component::getTypeInfo() const
	{
		return sw::getTypeRegistry().findType( hashed_string( "sw::Component" ) );
	}

	void Component::onBeginPlay()
	{
	}

	void Component::onTick( float32 deltaTime )
	{
		if ( _bActive != 0 && _onTickDelegate.isBound() )
		{
			_onTickDelegate( deltaTime );
		}
	}

	void Component::onDestroy()
	{
	}

	void Component::setActive( bool bActive )
	{
		_bActive = bActive ? 1 : 0;
		onPropertyChanged( hashed_string( "_bActive" ) );
	}

	void Component::onPropertyChanged( hashed_string propertyName )
	{

		(void)propertyName;
	}

	void Component::setTickGroup( TickGroup group )
	{
		_tickGroup = static_cast<uint8>( group );
		if ( _owner != nullptr )
		{
			_owner->markTickOrderDirty();
		}
	}

	void Component::addTickDependency( Component* targetComp )
	{
		if ( targetComp == nullptr || targetComp == this )
			return;

		for ( Component* dep : _tickDependencies )
		{
			if ( dep == targetComp )
				return;
		}
		_tickDependencies.push_back( targetComp );
		if ( _owner != nullptr )
			_owner->markTickOrderDirty();
	}

	void sortComponentsByTickOrder( std::vector<Component*>& components )
	{
		if ( components.size() <= 1 )
			return;

		// Preserve relative order for equal groups (stable), then topo within each group.
		std::stable_sort( components.begin(), components.end(), []( const Component* a, const Component* b )
		{
			if ( a == nullptr || b == nullptr )
				return b != nullptr && a == nullptr;
			return static_cast<uint8>( a->getTickGroup() ) < static_cast<uint8>( b->getTickGroup() );
		} );

		size_t begin = 0;
		while ( begin < components.size() )
		{
			size_t end = begin + 1;
			const TickGroup group = components[begin] != nullptr
										? components[begin]->getTickGroup()
										: TickGroup::DuringPhysics;
			while ( end < components.size() )
			{
				const TickGroup g = components[end] != nullptr
										? components[end]->getTickGroup()
										: TickGroup::DuringPhysics;
				if ( g != group )
					break;
				++end;
			}

			std::vector<Component*> slice( components.begin() + static_cast<std::ptrdiff_t>( begin ),
										   components.begin() + static_cast<std::ptrdiff_t>( end ) );
			if ( topoSortGroup( slice ) )
			{
				std::copy( slice.begin(), slice.end(), components.begin() + static_cast<std::ptrdiff_t>( begin ) );
			}
			begin = end;
		}
	}

	void buildComponentTickWaves( const std::vector<Component*>& components, std::vector<std::vector<Component*>>& outWaves )
	{
		outWaves.clear();
		std::array<std::vector<Component*>, 4> groups{};
		collectByTickGroup( components, groups );

		for ( std::vector<Component*>& group : groups )
		{
			if ( group.empty() )
				continue;
			// Preserve registration order inside the group before wave construction.
			buildWavesForGroup( group, outWaves );
		}
	}
} // namespace sw
