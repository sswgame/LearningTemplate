/**
 * @file SpatialTree.h
 * @brief 2D/3D 공통 공간 분할 트리 템플릿 (QuadTree / Octree 공통 기반)
 */
#pragma once

#include "Engine/EngineMinimal.h"

#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include <algorithm>
#include <memory>

namespace sw
{
	/**
	 * @struct AABB2D
	 * @brief 2차원 축 정렬 경계 상자 (Axis-Aligned Bounding Box)
	 */
	struct AABB2D
	{
		float32 _minX{ 0.0f };
		float32 _minY{ 0.0f };
		float32 _maxX{ 0.0f };
		float32 _maxY{ 0.0f };

		static constexpr AABB2D empty() noexcept
		{
			return AABB2D{ MathUtil::MaxFloat, MathUtil::MaxFloat, MathUtil::MinFloat, MathUtil::MinFloat };
		}

		static constexpr AABB2D infinite() noexcept
		{
			return AABB2D{ MathUtil::MinFloat, MathUtil::MinFloat, MathUtil::MaxFloat, MathUtil::MaxFloat };
		}

		static constexpr AABB2D zero() noexcept
		{
			return AABB2D{ 0.0f, 0.0f, 0.0f, 0.0f };
		}

		bool isValid() const noexcept
		{
			return _minX <= _maxX && _minY <= _maxY;
		}

		bool contains( float32 pointX, float32 pointY ) const noexcept
		{
			return pointX >= _minX && pointX <= _maxX && pointY >= _minY && pointY <= _maxY;
		}

		bool intersects( const AABB2D& other ) const noexcept
		{
			if ( _maxX < other._minX || _minX > other._maxX )
				return false;
			if ( _maxY < other._minY || _minY > other._maxY )
				return false;
			return true;
		}

		bool contains( const AABB2D& other ) const noexcept
		{
			return other._minX >= _minX && other._maxX <= _maxX && other._minY >= _minY && other._maxY <= _maxY;
		}

		float32 getWidth() const noexcept { return _maxX - _minX; }
		float32 getHeight() const noexcept { return _maxY - _minY; }
		float32 getCenterX() const noexcept { return ( _minX + _maxX ) * 0.5f; }
		float32 getCenterY() const noexcept { return ( _minY + _maxY ) * 0.5f; }
	};

	/**
	 * @struct SpatialElement
	 * @brief 2D 공간 트리에 등록되는 단위 객체
	 */
	struct SpatialElement
	{
		uint64 _id{ 0 };
		AABB2D _bounds{};
		void*  _pUserData{ nullptr };
	};

	/**
	 * @struct AABB3D
	 * @brief 3차원 축 정렬 경계 상자 (Axis-Aligned Bounding Box)
	 */
	struct AABB3D
	{
		float3 _min{ 0.0f, 0.0f, 0.0f };
		float3 _max{ 0.0f, 0.0f, 0.0f };

		static constexpr AABB3D empty() noexcept
		{
			return AABB3D{
				float3{MathUtil::MaxFloat, MathUtil::MaxFloat, MathUtil::MaxFloat},
				float3{MathUtil::MinFloat, MathUtil::MinFloat, MathUtil::MinFloat}
			 };
		}

		static constexpr AABB3D infinite() noexcept
		{
			return AABB3D{
				float3{MathUtil::MinFloat, MathUtil::MinFloat, MathUtil::MinFloat},
				float3{MathUtil::MaxFloat, MathUtil::MaxFloat, MathUtil::MaxFloat}
			 };
		}

		static constexpr AABB3D zero() noexcept
		{
			return AABB3D{
				float3{0.0f, 0.0f, 0.0f},
				float3{0.0f, 0.0f, 0.0f}
			   };
		}

		bool isValid() const noexcept
		{
			return _min._x <= _max._x && _min._y <= _max._y && _min._z <= _max._z;
		}

		bool contains( const float3& point ) const noexcept
		{
			return point._x >= _min._x && point._x <= _max._x &&
				   point._y >= _min._y && point._y <= _max._y &&
				   point._z >= _min._z && point._z <= _max._z;
		}

		bool intersects( const AABB3D& other ) const noexcept
		{
			if ( _max._x < other._min._x || _min._x > other._max._x )
				return false;
			if ( _max._y < other._min._y || _min._y > other._max._y )
				return false;
			if ( _max._z < other._min._z || _min._z > other._max._z )
				return false;
			return true;
		}

		bool contains( const AABB3D& other ) const noexcept
		{
			return other._min._x >= _min._x && other._max._x <= _max._x &&
				   other._min._y >= _min._y && other._max._y <= _max._y &&
				   other._min._z >= _min._z && other._max._z <= _max._z;
		}

		float3 getCenter() const noexcept
		{
			return float3{ ( _min._x + _max._x ) * 0.5f, ( _min._y + _max._y ) * 0.5f, ( _min._z + _max._z ) * 0.5f };
		}

		float3 getExtents() const noexcept
		{
			return float3{ ( _max._x - _min._x ) * 0.5f, ( _max._y - _min._y ) * 0.5f, ( _max._z - _min._z ) * 0.5f };
		}
	};

	/**
	 * @struct SpatialElement3D
	 * @brief 3D 공간 트리에 등록되는 단위 객체
	 */
	struct SpatialElement3D
	{
		uint64 _id{ 0 };
		AABB3D _bounds{};
		void*  _pUserData{ nullptr };
	};

	/**
	 * @struct QuadTreeTraits
	 * @brief 2차원 4분할 트리 정책
	 */
	struct QuadTreeTraits
	{
		using BoundsType					 = AABB2D;
		using ElementType					 = SpatialElement;
		using PointType						 = float2;
		static constexpr size_t kChildCount	 = 4;
		static constexpr size_t kMaxElements = 16;
		static constexpr size_t kMaxDepth	 = 8;

		static void subdivide( const AABB2D& parent, AABB2D arrOutChildren[4] )
		{
			const float32 midX = parent.getCenterX();
			const float32 midY = parent.getCenterY();

			// 0: Top-Left (NW)
			arrOutChildren[0] = AABB2D{ parent._minX, midY, midX, parent._maxY };
			// 1: Top-Right (NE)
			arrOutChildren[1] = AABB2D{ midX, midY, parent._maxX, parent._maxY };
			// 2: Bottom-Left (SW)
			arrOutChildren[2] = AABB2D{ parent._minX, parent._minY, midX, midY };
			// 3: Bottom-Right (SE)
			arrOutChildren[3] = AABB2D{ midX, parent._minY, parent._maxX, midY };
		}
	};

	/**
	 * @struct OctreeTraits
	 * @brief 3차원 8분할 트리 정책
	 */
	struct OctreeTraits
	{
		using BoundsType					 = AABB3D;
		using ElementType					 = SpatialElement3D;
		using PointType						 = float3;
		static constexpr size_t kChildCount	 = 8;
		static constexpr size_t kMaxElements = 16;
		static constexpr size_t kMaxDepth	 = 6;

		static void subdivide( const AABB3D& parent, AABB3D arrOutChildren[8] )
		{
			const float3 mid = parent.getCenter();

			for ( size_t octantIndex = 0; octantIndex < 8; ++octantIndex )
			{
				const float32 minX = ( ( octantIndex & 1 ) != 0 ) ? mid._x : parent._min._x;
				const float32 maxX = ( ( octantIndex & 1 ) != 0 ) ? parent._max._x : mid._x;

				const float32 minY = ( ( octantIndex & 2 ) != 0 ) ? mid._y : parent._min._y;
				const float32 maxY = ( ( octantIndex & 2 ) != 0 ) ? parent._max._y : mid._y;

				const float32 minZ = ( ( octantIndex & 4 ) != 0 ) ? mid._z : parent._min._z;
				const float32 maxZ = ( ( octantIndex & 4 ) != 0 ) ? parent._max._z : mid._z;

				arrOutChildren[octantIndex] = AABB3D{
					float3{minX, minY, minZ},
					float3{maxX, maxY, maxZ}
				   };
			}
		}
	};

	/**
	 * @class SpatialTree
	 * @brief 2D/3D 공통 공간 분할 인덱서 템플릿
	 */
	template <typename Traits>
	class SpatialTree
	{
	public:
		using BoundsType  = typename Traits::BoundsType;
		using ElementType = typename Traits::ElementType;

		static constexpr size_t kMaxElementsPerNode = Traits::kMaxElements;
		static constexpr size_t kMaxDepth			= Traits::kMaxDepth;

		SpatialTree()
			: _worldBounds{}
			, _maxElementsPerNode{ kMaxElementsPerNode }
			, _maxDepth{ kMaxDepth }
			, _totalElements{ 0 }
			, _pRoot{ nullptr }
			, _mapElements{}
			, _mapElementLocations{}
		{
		}

		explicit SpatialTree( const BoundsType& worldBounds, size_t maxElements = kMaxElementsPerNode, size_t maxDepth = kMaxDepth )
			: _worldBounds{ worldBounds }
			, _maxElementsPerNode{ maxElements }
			, _maxDepth{ maxDepth }
			, _totalElements{ 0 }
			, _pRoot{ nullptr }
			, _mapElements{}
			, _mapElementLocations{}
		{
			initialize( worldBounds, maxElements, maxDepth );
		}

		~SpatialTree()
		{
			clear();
		}

		void initialize( const BoundsType& worldBounds, size_t maxElements = kMaxElementsPerNode, size_t maxDepth = kMaxDepth )
		{
			clear();
			_worldBounds		= worldBounds;
			_maxElementsPerNode = maxElements;
			_maxDepth			= maxDepth;
			_pRoot				= sw::make_unique<Node>( worldBounds, 0 );
		}

		void clear()
		{
			_pRoot.reset();
			_mapElements.clear();
			_mapElementLocations.clear();
			_totalElements = 0;
		}

		bool insert( uint64 id, const BoundsType& bounds, void* pUserData = nullptr )
		{
			if ( _pRoot == nullptr || _mapElements.find( id ) != _mapElements.end() )
				return false;

			ElementType elem{ id, bounds, pUserData };
			if ( _pRoot->insert( elem, _maxElementsPerNode, _maxDepth ) )
			{
				_mapElements[id]		 = elem;
				_mapElementLocations[id] = bounds;
				++_totalElements;
				return true;
			}
			return false;
		}

		bool remove( uint64 id )
		{
			if ( _pRoot == nullptr )
				return false;

			auto iter = _mapElements.find( id );
			if ( iter == _mapElements.end() )
				return false;

			if ( _pRoot->remove( id, _maxElementsPerNode ) )
			{
				_mapElements.erase( iter );
				_mapElementLocations.erase( id );
				if ( _totalElements > 0 )
					--_totalElements;
				return true;
			}
			return false;
		}

		bool update( uint64 id, const BoundsType& newBounds )
		{
			auto iter = _mapElements.find( id );
			if ( iter == _mapElements.end() )
				return false;

			void* pSavedUserData = iter->second._pUserData;
			if ( remove( id ) == false )
				return false;

			return insert( id, newBounds, pSavedUserData );
		}

		void queryRange( const BoundsType& range, vector<ElementType>& listOutElements ) const
		{
			if ( _pRoot != nullptr )
				_pRoot->query( range, listOutElements );
		}

		size_t			  getTotalElements() const { return _totalElements; }
		const BoundsType& getWorldBounds() const { return _worldBounds; }

	protected:
		struct Node
		{
			BoundsType			 _bounds{};
			size_t				 _depth{ 0 };
			vector<ElementType>	 _listElements{};
			sw::unique_ptr<Node> _arrChildren[Traits::kChildCount]{};
			bool				 _bIsDivided{ false };

			explicit Node( const BoundsType& bounds, size_t depth = 0 )
				: _bounds{ bounds }
				, _depth{ depth }
				, _listElements{}
				, _arrChildren{}
				, _bIsDivided{ false }
			{
			}

			void subdivide()
			{
				BoundsType arrChildBounds[Traits::kChildCount]{};
				Traits::subdivide( _bounds, arrChildBounds );

				for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
				{
					_arrChildren[childIndex] = sw::make_unique<Node>( arrChildBounds[childIndex], _depth + 1 );
				}

				_bIsDivided = true;

				vector<ElementType> listRemaining;
				for ( const ElementType& element : _listElements )
				{
					bool bPushedToChild = false;
					for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
					{
						if ( _arrChildren[childIndex]->_bounds.contains( element._bounds ) )
						{
							_arrChildren[childIndex]->_listElements.push_back( element );
							bPushedToChild = true;
							break;
						}
					}

					if ( bPushedToChild == false )
						listRemaining.push_back( element );
				}

				_listElements = std::move( listRemaining );
			}

			bool insert( const ElementType& elem, size_t maxElements, size_t maxDepth )
			{
				if ( _bounds.intersects( elem._bounds ) == false )
					return false;

				if ( _bIsDivided )
				{
					for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
					{
						if ( _arrChildren[childIndex]->_bounds.contains( elem._bounds ) )
							return _arrChildren[childIndex]->insert( elem, maxElements, maxDepth );
					}
				}

				if ( _listElements.size() < maxElements || _depth >= maxDepth )
				{
					_listElements.push_back( elem );
					return true;
				}

				if ( _bIsDivided == false )
					subdivide();

				for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
				{
					if ( _arrChildren[childIndex]->_bounds.contains( elem._bounds ) )
						return _arrChildren[childIndex]->insert( elem, maxElements, maxDepth );
				}

				_listElements.push_back( elem );
				return true;
			}

			bool remove( uint64 id, size_t maxElements = Traits::kMaxElements )
			{
				for ( auto it = _listElements.begin(); it != _listElements.end(); ++it )
				{
					if ( it->_id == id )
					{
						_listElements.erase( it );
						return true;
					}
				}

				if ( _bIsDivided )
				{
					bool bRemoved = false;
					for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
					{
						if ( _arrChildren[childIndex]->remove( id, maxElements ) )
						{
							bRemoved = true;
							break;
						}
					}

					if ( bRemoved )
					{
						size_t totalChildElements = 0;
						bool   bAnyChildDivided	  = false;
						for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
						{
							totalChildElements += _arrChildren[childIndex]->_listElements.size();
							if ( _arrChildren[childIndex]->_bIsDivided )
								bAnyChildDivided = true;
						}

						if ( bAnyChildDivided == false && ( _listElements.size() + totalChildElements ) <= maxElements )
						{
							for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
							{
								for ( auto& element : _arrChildren[childIndex]->_listElements )
									_listElements.push_back( std::move( element ) );
								_arrChildren[childIndex].reset();
							}
							_bIsDivided = false;
						}
						return true;
					}
				}

				return false;
			}

			void query( const BoundsType& range, vector<ElementType>& listOutElements ) const
			{
				if ( _bounds.intersects( range ) == false )
					return;

				for ( const ElementType& element : _listElements )
				{
					if ( range.intersects( element._bounds ) )
						listOutElements.push_back( element );
				}

				if ( _bIsDivided )
				{
					for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
					{
						_arrChildren[childIndex]->query( range, listOutElements );
					}
				}
			}
		};

		BoundsType						   _worldBounds{};
		size_t							   _maxElementsPerNode{ Traits::kMaxElements };
		size_t							   _maxDepth{ Traits::kMaxDepth };
		size_t							   _totalElements{ 0 };
		sw::unique_ptr<Node>			   _pRoot{ nullptr };
		unordered_map<uint64, ElementType> _mapElements{};
		unordered_map<uint64, BoundsType>  _mapElementLocations{};
	};
} // namespace sw
