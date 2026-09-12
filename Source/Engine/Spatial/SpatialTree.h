/**
 * @file SpatialTree.h
 * @brief 2D/3D 공통 공간 분할 트리 템플릿 (QuadTree / Octree 공통 기반)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Physics/AABB.h"

namespace sw
{
    /**
     * @struct AABB2D
     * @brief 2차원 축 정렬 경계 상자 (Axis-Aligned Bounding Box)
     */
    struct AABB2D
    {
        /** @note 3차원 `AABB` 와 같은 모양이다 — 그쪽도 `float3 _min/_max` 를 든다. */
        float2 _min{ 0.0f, 0.0f };
        float2 _max{ 0.0f, 0.0f };

        static constexpr AABB2D empty() noexcept
        {
            return AABB2D{
                float2{MathUtil::MaxFloat, MathUtil::MaxFloat},
                float2{MathUtil::MinFloat, MathUtil::MinFloat}
            };
        }

        static constexpr AABB2D infinite() noexcept
        {
            return AABB2D{
                float2{MathUtil::MinFloat, MathUtil::MinFloat},
                float2{MathUtil::MaxFloat, MathUtil::MaxFloat}
            };
        }

        static constexpr AABB2D zero() noexcept
        {
            return AABB2D{
                float2{0.0f, 0.0f},
                float2{0.0f, 0.0f}
            };
        }

        bool isValid() const noexcept
        {
            return _min._x <= _max._x && _min._y <= _max._y;
        }

        bool contains( float32 pointX, float32 pointY ) const noexcept
        {
            return _min._x <= pointX && pointX <= _max._x && _min._y <= pointY && pointY <= _max._y;
        }

        bool intersects( const AABB2D& other ) const noexcept
        {
            return _min._x <= other._max._x && other._min._x <= _max._x &&
                   _min._y <= other._max._y && other._min._y <= _max._y;
        }

        bool contains( const AABB2D& other ) const noexcept
        {
            return _min._x <= other._min._x && other._max._x <= _max._x && _min._y <= other._min._y && other._max._y <= _max._y;
        }

        float32 getWidth() const noexcept { return _max._x - _min._x; }
        float32 getHeight() const noexcept { return _max._y - _min._y; }
        float32 getCenterX() const noexcept { return ( _min._x + _max._x ) * 0.5f; }
        float32 getCenterY() const noexcept { return ( _min._y + _max._y ) * 0.5f; }
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
     * @struct SpatialElement3D
     * @brief 3D 공간 트리에 등록되는 단위 객체
     */
    struct SpatialElement3D
    {
        uint64 _id{ 0 };
        AABB   _bounds{};
        void*  _pUserData{ nullptr };
    };

    /**
     * @struct QuadTreeTraits
     * @brief 2차원 4분할 트리 정책
     */
    struct QuadTreeTraits
    {
        using BoundsType                     = AABB2D;
        using ElementType                    = SpatialElement;
        using PointType                      = float2;
        static constexpr size_t kChildCount  = 4;
        static constexpr size_t kMaxElements = 16;
        static constexpr size_t kMaxDepth    = 8;

        static void subdivide( const AABB2D& parent, AABB2D outArrChildren[4] )
        {
            const float32 midX = parent.getCenterX();
            const float32 midY = parent.getCenterY();

            // 0: Top-Left (NW)
            outArrChildren[0] = AABB2D{
                float2{parent._min._x,           midY},
                float2{          midX, parent._max._y}
            };
            // 1: Top-Right (NE)
            outArrChildren[1] = AABB2D{
                float2{          midX,           midY},
                float2{parent._max._x, parent._max._y}
            };
            // 2: Bottom-Left (SW)
            outArrChildren[2] = AABB2D{
                float2{parent._min._x, parent._min._y},
                float2{          midX,           midY}
            };
            // 3: Bottom-Right (SE)
            outArrChildren[3] = AABB2D{
                float2{          midX, parent._min._y},
                float2{parent._max._x,           midY}
            };
        }
    };

    /**
     * @struct OctreeTraits
     * @brief 3차원 8분할 트리 정책
     */
    struct OctreeTraits
    {
        using BoundsType                     = AABB;
        using ElementType                    = SpatialElement3D;
        using PointType                      = float3;
        static constexpr size_t kChildCount  = 8;
        static constexpr size_t kMaxElements = 16;
        static constexpr size_t kMaxDepth    = 6;

        static void subdivide( const AABB& parent, AABB outArrChildren[8] )
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

                outArrChildren[octantIndex] = AABB{
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
        static constexpr size_t kMaxDepth           = Traits::kMaxDepth;

        SpatialTree()
            : _worldBounds{}
            , _maxElementsPerNode{ kMaxElementsPerNode }
            , _maxDepth{ kMaxDepth }
            , _totalElements{ 0 }
            , _pRoot{ nullptr }
            , _mapElement{}
            , _mapElementLocation{}
        {
        }

        explicit SpatialTree( const BoundsType& worldBounds, size_t maxElements = kMaxElementsPerNode, size_t maxDepth = kMaxDepth )
            : _worldBounds{ worldBounds }
            , _maxElementsPerNode{ maxElements }
            , _maxDepth{ maxDepth }
            , _totalElements{ 0 }
            , _pRoot{ nullptr }
            , _mapElement{}
            , _mapElementLocation{}
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
            _worldBounds        = worldBounds;
            _maxElementsPerNode = maxElements;
            _maxDepth           = maxDepth;
            _pRoot              = sw::make_unique<Node>( worldBounds, size_t{ 0 } );
        }

        void clear()
        {
            _pRoot.reset();
            _mapElement.clear();
            _mapElementLocation.clear();
            _totalElements = 0;
        }

        bool insert( uint64 id, const BoundsType& bounds, void* pUserData = nullptr )
        {
            if ( _pRoot == nullptr || _mapElement.find( id ) != _mapElement.end() )
                return false;

            ElementType elem{ id, bounds, pUserData };
            if ( _pRoot->insert( elem, _maxElementsPerNode, _maxDepth ) )
            {
                _mapElement[id]         = elem;
                _mapElementLocation[id] = bounds;
                ++_totalElements;
                return true;
            }
            return false;
        }

        bool remove( uint64 id )
        {
            if ( _pRoot == nullptr )
                return false;

            auto iter = _mapElement.find( id );
            if ( iter == _mapElement.end() )
                return false;

            if ( _pRoot->remove( id, _maxElementsPerNode ) )
            {
                _mapElement.erase( iter );
                _mapElementLocation.erase( id );
                if ( _totalElements > 0 )
                    --_totalElements;
                return true;
            }
            return false;
        }

        bool update( uint64 id, const BoundsType& newBounds )
        {
            auto iter = _mapElement.find( id );
            if ( iter == _mapElement.end() )
                return false;

            void* pSavedUserData = iter->second._pUserData;
            if ( remove( id ) == false )
                return false;

            return insert( id, newBounds, pSavedUserData );
        }

        void queryRange( const BoundsType& range, vector<ElementType>& outListElement ) const
        {
            if ( _pRoot != nullptr )
                _pRoot->query( range, outListElement );
        }

        size_t            getTotalElements() const { return _totalElements; }
        const BoundsType& getWorldBounds() const { return _worldBounds; }

    protected:
        struct Node
        {
            BoundsType           _bounds{};
            size_t               _depth{ 0 };
            vector<ElementType>  _listElement{};
            sw::unique_ptr<Node> _arrChild[Traits::kChildCount]{};
            bool                 _bIsDivided{ false };

            explicit Node( const BoundsType& bounds, size_t depth = 0 )
                : _bounds{ bounds }
                , _depth{ depth }
                , _listElement{}
                , _arrChild{}
                , _bIsDivided{ false }
            {
            }

            void subdivide()
            {
                BoundsType arrChildBound[Traits::kChildCount]{};
                Traits::subdivide( _bounds, arrChildBound );

                for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
                {
                    _arrChild[childIndex] = sw::make_unique<Node>( arrChildBound[childIndex], _depth + 1 );
                }

                _bIsDivided = true;

                vector<ElementType> listRemaining;
                for ( const ElementType& element : _listElement )
                {
                    bool bPushedToChild = false;
                    for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
                    {
                        if ( _arrChild[childIndex]->_bounds.contains( element._bounds ) )
                        {
                            _arrChild[childIndex]->_listElement.push_back( element );
                            bPushedToChild = true;
                            break;
                        }
                    }

                    if ( bPushedToChild == false )
                        listRemaining.push_back( element );
                }

                _listElement = std::move( listRemaining );
            }

            bool insert( const ElementType& elem, size_t maxElements, size_t maxDepth )
            {
                if ( _bounds.intersects( elem._bounds ) == false )
                    return false;

                if ( _bIsDivided )
                {
                    for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
                    {
                        if ( _arrChild[childIndex]->_bounds.contains( elem._bounds ) )
                            return _arrChild[childIndex]->insert( elem, maxElements, maxDepth );
                    }
                }

                if ( _listElement.size() < maxElements || _depth >= maxDepth )
                {
                    _listElement.push_back( elem );
                    return true;
                }

                if ( _bIsDivided == false )
                    subdivide();

                for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
                {
                    if ( _arrChild[childIndex]->_bounds.contains( elem._bounds ) )
                        return _arrChild[childIndex]->insert( elem, maxElements, maxDepth );
                }

                _listElement.push_back( elem );
                return true;
            }

            bool remove( uint64 id, size_t maxElements = Traits::kMaxElements )
            {
                for ( auto it = _listElement.begin(); it != _listElement.end(); ++it )
                {
                    if ( it->_id == id )
                    {
                        _listElement.erase( it );
                        return true;
                    }
                }

                if ( _bIsDivided )
                {
                    bool bRemoved = false;
                    for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
                    {
                        if ( _arrChild[childIndex]->remove( id, maxElements ) )
                        {
                            bRemoved = true;
                            break;
                        }
                    }

                    if ( bRemoved )
                    {
                        size_t totalChildElements = 0;
                        bool   bAnyChildDivided   = false;
                        for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
                        {
                            totalChildElements += _arrChild[childIndex]->_listElement.size();
                            if ( _arrChild[childIndex]->_bIsDivided )
                                bAnyChildDivided = true;
                        }

                        if ( bAnyChildDivided == false && ( _listElement.size() + totalChildElements ) <= maxElements )
                        {
                            for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
                            {
                                for ( auto& element : _arrChild[childIndex]->_listElement )
                                    _listElement.push_back( std::move( element ) );
                                _arrChild[childIndex].reset();
                            }
                            _bIsDivided = false;
                        }
                        return true;
                    }
                }

                return false;
            }

            void query( const BoundsType& range, vector<ElementType>& outListElement ) const
            {
                if ( _bounds.intersects( range ) == false )
                    return;

                for ( const ElementType& element : _listElement )
                {
                    if ( range.intersects( element._bounds ) )
                        outListElement.push_back( element );
                }

                if ( _bIsDivided )
                {
                    for ( size_t childIndex = 0; childIndex < Traits::kChildCount; ++childIndex )
                    {
                        _arrChild[childIndex]->query( range, outListElement );
                    }
                }
            }
        };

        BoundsType                         _worldBounds{};
        size_t                             _maxElementsPerNode{ Traits::kMaxElements };
        size_t                             _maxDepth{ Traits::kMaxDepth };
        size_t                             _totalElements{ 0 };
        sw::unique_ptr<Node>               _pRoot{ nullptr };
        unordered_map<uint64, ElementType> _mapElement{};
        unordered_map<uint64, BoundsType>  _mapElementLocation{};
    };
} // namespace sw
