/**
 * @file View.h
 * @brief 여러 컴포넌트 타입을 동시에 소유한 엔티티들을 초고속으로 필터링 순회하는 ECS View
 *
 * [동작 원리 및 최적화]:
 * 1. Smallest SparseSet Driver: 요청된 컴포넌트 풀 중 가장 요소 수가 적은 SparseSet을 기준으로 순회하여 불필요한 전체 검사를 방지합니다.
 * 2. O(1) Membership Check: 기준 엔티티가 나머지 컴포넌트 풀들에 포함되어 있는지 O(1) SparseSet 룩업으로 고속 판정합니다.
 * 3. Lazy Iterator: 유효한 엔티티 조합이 발견될 때까지 건너뛰며(AdvanceToValid) 튜플을 생성합니다.
 */
#pragma once
#include "Engine/ECS/Registry.h"

#include <tuple>
#include <utility>

namespace sw
{
	/**
	 * @brief 지정된 컴포넌트들을 모두 가진 엔티티와 컴포넌트 참조들을 묶어 순회하는 뷰 클래스
	 */
	template <typename... Components>
	class View
	{
	public:
		/**
		 * @brief 레지스트리로부터 컴포넌트 풀들을 튜플로 묶어 가장 크기가 작은 풀을 드라이버로 선정합니다.
		 */
		View( Registry& registry )
			: _registry{ registry }
			, _sets{ std::make_tuple( registry.getSparseSet<Components>()... ) }
		{
			_findSmallestSet( _minSize, _smallestIndex, std::index_sequence_for<Components...>{} );
		}

		class Iterator
		{
		public:
			/** @brief 가장 작은 풀의 index부터 유효 엔티티로 맞춥니다. */
			Iterator( View* pView, size_t index )
				: _pView{ pView }
				, _index{ index }
			{
				_advanceToValid();
			}

			/** @brief 다음 유효 엔티티로 진행합니다. */
			Iterator& operator++()
			{
				++_index;
				_advanceToValid();
				return *this;
			}

			/** @brief 다르면 true를 반환합니다. */
			bool operator!=( const Iterator& other ) const { return _index != other._index; }
			bool operator==( const Iterator& other ) const { return _index == other._index; }

			auto operator*() const { return _pView->_getTuple( _currentEntity, std::index_sequence_for<Components...>{} ); }

		private:
			/** @brief 모든 풀에 있는 엔티티가 나올 때까지 index를 올립니다. */
			void _advanceToValid()
			{
				while ( _index < _pView->_minSize )
				{
					Entity e = _pView->_getEntityAtSmallestSet( _index );
					if ( _pView->_allSetsHave( e, std::index_sequence_for<Components...>{} ) )
					{
						_currentEntity = e;
						return;
					}
					++_index;
				}
				_index = _pView->_minSize; // End
			}

			View*  _pView;
			size_t _index;
			Entity _currentEntity = kNullEntity;
		};

		/** @brief 첫 유효 엔티티 이터레이터. */
		Iterator begin() { return Iterator( this, 0 ); }
		/** @brief 끝 이터레이터. */
		Iterator end() { return Iterator( this, _minSize ); }

		template <typename T>
		/** @brief 반환합니다. */
		T& get( Entity entity ) const
		{
			return *_registry.getPtr<T>( entity );
		}

		template <typename Func>
		/** @brief 매칭 엔티티마다 func(entity, comps...)를 호출합니다. */
		void each( Func func )
		{
			for ( auto tuple : *this )
			{
				std::apply( func, tuple );
			}
		}

	private:
		Registry&							   _registry;
		std::tuple<sparse_set<Components>*...> _sets;
		size_t								   _minSize		  = static_cast<size_t>( -1 );
		int32								   _smallestIndex = -1;

		template <size_t... Is>
		/** @brief 모든 컴포넌트 풀에 e가 있으면 true. */
		bool _allSetsHave( Entity e, std::index_sequence<Is...> ) const
		{
			bool valid = ( ( std::get<Is>( _sets ) != nullptr ) && ... );
			if ( valid == false || e.isValid() == false )
				return false;
			return ( ( std::get<Is>( _sets )->contains( e.index() ) ) && ... );
		}

		template <size_t... Is>
		/** @brief 가장 작은 sparse_set을 찾아 이터레이션 드라이버로 씁니다. */
		void _findSmallestSet( size_t& minSize, int32& smallestIndex, std::index_sequence<Is...> ) const
		{
			bool bAnyNull = false;
			( ..., [&]()
			{
				auto* set = std::get<Is>( _sets );
				if ( set == nullptr )
				{
					bAnyNull = true;
					return;
				}
				if ( set->size() < minSize )
				{
					minSize		  = set->size();
					smallestIndex = static_cast<int32>( Is );
				}
			}() );
			if ( bAnyNull || minSize == static_cast<size_t>( -1 ) )
			{
				minSize		  = 0;
				smallestIndex = -1;
			}
		}

		/** @brief 드라이버 풀의 index번째 엔티티. */
		Entity _getEntityAtSmallestSet( size_t index ) const
		{
			Entity result = kNullEntity;
			_extractEntity( index, result, std::index_sequence_for<Components...>{} );
			return result;
		}

		template <size_t... Is>
		/** @brief 드라이버 풀 dense 키에서 엔티티를 꺼냅니다. */
		void _extractEntity( size_t index, Entity& outEntity, std::index_sequence<Is...> ) const
		{
			auto extractOne = [this, index, &outEntity]( size_t targetIndex, auto* pSet )
			{
				if ( _smallestIndex == static_cast<int32>( targetIndex ) && pSet != nullptr && index < pSet->getDenseKeys().size() )
					outEntity = _registry.handleFromIndex( pSet->getDenseKeys()[index] );
			};
			( extractOne( Is, std::get<Is>( _sets ) ), ... );
		}

		template <size_t... Is>
		/** @brief 엔티티와 각 컴포넌트 참조 튜플. */
		std::tuple<Entity, Components&...> _getTuple( Entity e, std::index_sequence<Is...> ) const
		{
			return std::tuple<Entity, Components&...>( e, ( *std::get<Is>( _sets ) )[e.index()]... );
		}
	};

	/**
	 * @brief ECS View에서 특정 컴포넌트를 가진 엔티티를 제외하기 위한 태그 구조체
	 */
	template <typename... ExcludeComponents>
	struct Exclude
	{
	};

	/**
	 * @brief Include 컴포넌트들을 모두 가지고, Exclude 컴포넌트들은 하나도 가지지 않는 엔티티들을 순회하는 뷰 클래스
	 */
	template <typename IncludeTypeList, typename ExcludeTypeList>
	class FilteredView;

	template <typename... Includes, typename... Excludes>
	class FilteredView<std::tuple<Includes...>, std::tuple<Excludes...>>
	{
	public:
		FilteredView( Registry& registry )
			: _registry{ registry }
			, _includeSets{ std::make_tuple( registry.getSparseSet<Includes>()... ) }
			, _excludeSets{ std::make_tuple( registry.getSparseSet<Excludes>()... ) }
		{
			_findSmallestSet( _minSize, _smallestIndex, std::index_sequence_for<Includes...>{} );
		}

		class Iterator
		{
		public:
			Iterator( FilteredView* pView, size_t index )
				: _pView{ pView }
				, _index{ index }
			{
				_advanceToValid();
			}

			Iterator& operator++()
			{
				++_index;
				_advanceToValid();
				return *this;
			}

			bool operator!=( const Iterator& other ) const { return _index != other._index; }
			bool operator==( const Iterator& other ) const { return _index == other._index; }

			auto operator*() const { return _pView->_getTuple( _currentEntity, std::index_sequence_for<Includes...>{} ); }

		private:
			void _advanceToValid()
			{
				while ( _index < _pView->_minSize )
				{
					Entity e = _pView->_getEntityAtSmallestSet( _index );
					if ( _pView->_allIncludeSetsHave( e, std::index_sequence_for<Includes...>{} ) &&
						 _pView->_noneExcludeSetsHave( e, std::index_sequence_for<Excludes...>{} ) )
					{
						_currentEntity = e;
						return;
					}
					++_index;
				}
				_index = _pView->_minSize;
			}

			FilteredView* _pView;
			size_t		  _index;
			Entity		  _currentEntity = kNullEntity;
		};

		Iterator begin() { return Iterator( this, 0 ); }
		Iterator end() { return Iterator( this, _minSize ); }

		template <typename T>
		T& get( Entity entity ) const
		{
			return *_registry.getPtr<T>( entity );
		}

		bool contains( Entity entity ) const
		{
			return _allIncludeSetsHave( entity, std::index_sequence_for<Includes...>{} ) &&
				   _noneExcludeSetsHave( entity, std::index_sequence_for<Excludes...>{} );
		}

		template <typename Func>
		void each( Func func )
		{
			for ( auto tuple : *this )
			{
				std::apply( func, tuple );
			}
		}

	private:
		Registry&							 _registry;
		std::tuple<sparse_set<Includes>*...> _includeSets;
		std::tuple<sparse_set<Excludes>*...> _excludeSets;
		size_t								 _minSize		= static_cast<size_t>( -1 );
		int32								 _smallestIndex = -1;

		template <size_t... Is>
		bool _allIncludeSetsHave( Entity e, std::index_sequence<Is...> ) const
		{
			bool valid = ( ( std::get<Is>( _includeSets ) != nullptr ) && ... );
			if ( valid == false || e.isValid() == false )
				return false;
			return ( ( std::get<Is>( _includeSets )->contains( e.index() ) ) && ... );
		}

		template <size_t... Is>
		bool _noneExcludeSetsHave( Entity e, std::index_sequence<Is...> ) const
		{
			if ( e.isValid() == false )
				return true;
			auto hasAny = [e]( auto* pSet ) -> bool
			{
				return pSet != nullptr && pSet->contains( e.index() );
			};
			return ( ( !hasAny( std::get<Is>( _excludeSets ) ) ) && ... );
		}

		template <size_t... Is>
		void _findSmallestSet( size_t& minSize, int32& smallestIndex, std::index_sequence<Is...> ) const
		{
			bool bAnyNull = false;
			( ..., [&]()
			{
				auto* set = std::get<Is>( _includeSets );
				if ( set == nullptr )
				{
					bAnyNull = true;
					return;
				}
				if ( set->size() < minSize )
				{
					minSize		  = set->size();
					smallestIndex = static_cast<int32>( Is );
				}
			}() );
			if ( bAnyNull || minSize == static_cast<size_t>( -1 ) )
			{
				minSize		  = 0;
				smallestIndex = -1;
			}
		}

		Entity _getEntityAtSmallestSet( size_t index ) const
		{
			Entity result = kNullEntity;
			_extractEntity( index, result, std::index_sequence_for<Includes...>{} );
			return result;
		}

		template <size_t... Is>
		void _extractEntity( size_t index, Entity& outEntity, std::index_sequence<Is...> ) const
		{
			auto extractOne = [this, index, &outEntity]( size_t targetIndex, auto* pSet )
			{
				if ( _smallestIndex == static_cast<int32>( targetIndex ) && pSet != nullptr && index < pSet->getDenseKeys().size() )
					outEntity = _registry.handleFromIndex( pSet->getDenseKeys()[index] );
			};
			( extractOne( Is, std::get<Is>( _includeSets ) ), ... );
		}

		template <size_t... Is>
		std::tuple<Entity, Includes&...> _getTuple( Entity e, std::index_sequence<Is...> ) const
		{
			return std::tuple<Entity, Includes&...>( e, ( *std::get<Is>( _includeSets ) )[e.index()]... );
		}
	};

	template <typename... Includes, typename... Excludes>
	inline auto makeFilteredView( Registry& registry, Exclude<Excludes...> )
	{
		return FilteredView<std::tuple<Includes...>, std::tuple<Excludes...>>( registry );
	}
} // namespace sw
