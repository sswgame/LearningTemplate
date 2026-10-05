#include "pch.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

namespace sw
{
    namespace
    {
        struct DebugDrawQueueInternal
        {
            /** @brief 이보다 짧은 축 · 선은 0 으로 봅니다. */
            static constexpr float32 kDegenerateLength = 1e-6f;

            /**
             * @brief 남은 시간을 흘려 끝난 것을 뺍니다. 지속 시간 0 인 것은 한 번 보인 뒤 빠집니다.
             * @details 델타가 0 이면(일시정지) 아무것도 빼지 않습니다 — 멈춘 화면에서 마지막 프레임의 디버그 도형을 그대로 본다.
             */
            template <typename T>
            static void age( vector<T>& inoutList, float32 deltaSeconds )
            {
                if ( deltaSeconds <= 0.0f )
                    return;
                size_t keepCount = 0;
                for ( size_t index = 0; index < inoutList.size(); ++index )
                {
                    T& item = inoutList[index];
                    item._remainingSeconds -= deltaSeconds;
                    if ( item._remainingSeconds <= 0.0f )
                        continue;
                    if ( keepCount != index )
                        inoutList[keepCount] = std::move( item );
                    ++keepCount;
                }
                inoutList.resize( keepCount );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    DebugDrawQueue::DebugDrawQueue()
        : _mutex{}
        , _listLine{}
        , _listSphere{}
        , _listText{}
        , _listVisibleLine{}
        , _listVisibleSphere{}
        , _listVisibleText{}
        , _mapCategoryEnabled{}
    {
    }

    void DebugDrawQueue::clear()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _listLine.clear();
        _listSphere.clear();
        _listText.clear();
        _listVisibleLine.clear();
        _listVisibleSphere.clear();
        _listVisibleText.clear();
    }

    void DebugDrawQueue::endFrame( float32 deltaSeconds )
    {
        std::scoped_lock<mutex> lock{ _mutex };

        _listVisibleLine.clear();
        for ( const DebugLine& line : _listLine )
        {
            if ( isCategoryEnabledLocked( line._category ) )
                _listVisibleLine.push_back( line );
        }
        _listVisibleSphere.clear();
        for ( const DebugSphere& sphere : _listSphere )
        {
            if ( isCategoryEnabledLocked( sphere._category ) )
                _listVisibleSphere.push_back( sphere );
        }
        _listVisibleText.clear();
        for ( const DebugText& text : _listText )
        {
            if ( isCategoryEnabledLocked( text._category ) )
                _listVisibleText.push_back( text );
        }

        DebugDrawQueueInternal::age( _listLine, deltaSeconds );
        DebugDrawQueueInternal::age( _listSphere, deltaSeconds );
        DebugDrawQueueInternal::age( _listText, deltaSeconds );
    }

    void DebugDrawQueue::drawLine( const float3& from, const float3& to, const float4& color, float32 durationSeconds, const hashed_string& category )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        pushLineLocked( from, to, color, durationSeconds, registerCategoryLocked( category ) );
    }

    void DebugDrawQueue::drawSphere( const float3& center, float32 radius, const float4& color, float32 durationSeconds, const hashed_string& category )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        DebugSphere             sphere{};
        sphere._center           = center;
        sphere._radius           = radius;
        sphere._color            = color;
        sphere._category         = registerCategoryLocked( category );
        sphere._remainingSeconds = durationSeconds;
        _listSphere.push_back( sphere );
    }

    void DebugDrawQueue::drawBox( const float3& center, const float3& halfExtent, const float4& color, float32 durationSeconds, const hashed_string& category )
    {
        drawOrientedBox( center, halfExtent, quaternion::Identity, color, durationSeconds, category );
    }

    void DebugDrawQueue::drawOrientedBox( const float3& center, const float3& halfExtent, const quaternion& rotation, const float4& color, float32 durationSeconds,
                                          const hashed_string& category )
    {
        const float3 axisX = float3::transform( float3{ halfExtent._x, 0.0f, 0.0f }, rotation );
        const float3 axisY = float3::transform( float3{ 0.0f, halfExtent._y, 0.0f }, rotation );
        const float3 axisZ = float3::transform( float3{ 0.0f, 0.0f, halfExtent._z }, rotation );

        std::scoped_lock<mutex> lock{ _mutex };
        const hashed_string     resolvedCategory = registerCategoryLocked( category );

        // 반 크기가 0 인 축이 있으면 사각형 하나다(2D). 위 · 아래 면이 겹치고 세로 모서리는 길이가 0 이라 선 넷만 남긴다.
        const float3  arrAxis[]  = { axisX, axisY, axisZ };
        const float32 arrHalf[]  = { halfExtent._x, halfExtent._y, halfExtent._z };
        float3        arrEdge[2] = {};
        uint32        edgeCount  = 0;
        for ( uint32 axisIndex = 0; axisIndex < 3; ++axisIndex )
        {
            if ( MathUtil::abs( arrHalf[axisIndex] ) > DebugDrawQueueInternal::kDegenerateLength && edgeCount < 2 )
                arrEdge[edgeCount++] = arrAxis[axisIndex];
        }
        const bool bFlat = MathUtil::abs( halfExtent._x ) <= DebugDrawQueueInternal::kDegenerateLength ||
                           MathUtil::abs( halfExtent._y ) <= DebugDrawQueueInternal::kDegenerateLength ||
                           MathUtil::abs( halfExtent._z ) <= DebugDrawQueueInternal::kDegenerateLength;
        if ( bFlat )
        {
            const float3 arrCorner[] = { center - arrEdge[0] - arrEdge[1], center + arrEdge[0] - arrEdge[1], center + arrEdge[0] + arrEdge[1],
                                         center - arrEdge[0] + arrEdge[1] };
            for ( uint32 cornerIndex = 0; cornerIndex < 4; ++cornerIndex )
            {
                pushLineLocked( arrCorner[cornerIndex], arrCorner[( cornerIndex + 1 ) % 4], color, durationSeconds, resolvedCategory );
            }
            return;
        }

        // 꼭짓점 번호의 비트 0 · 1 · 2 가 X · Y · Z 의 +/- 다. 모서리는 비트 하나만 다른 두 꼭짓점이다.
        float3 arrCorner[8];
        for ( uint32 cornerIndex = 0; cornerIndex < 8; ++cornerIndex )
        {
            const float32 signX    = ( cornerIndex & 1u ) != 0 ? 1.0f : -1.0f;
            const float32 signY    = ( cornerIndex & 2u ) != 0 ? 1.0f : -1.0f;
            const float32 signZ    = ( cornerIndex & 4u ) != 0 ? 1.0f : -1.0f;
            arrCorner[cornerIndex] = center + axisX * signX + axisY * signY + axisZ * signZ;
        }
        for ( uint32 cornerIndex = 0; cornerIndex < 8; ++cornerIndex )
        {
            for ( uint32 bit = 1; bit < 8; bit <<= 1 )
            {
                const uint32 otherIndex = cornerIndex | bit;
                if ( otherIndex != cornerIndex )
                    pushLineLocked( arrCorner[cornerIndex], arrCorner[otherIndex], color, durationSeconds, resolvedCategory );
            }
        }
    }

    void DebugDrawQueue::drawArrow( const float3& from, const float3& to, const float4& color, float32 durationSeconds, const hashed_string& category )
    {
        const float3  shaft  = to - from;
        const float32 length = shaft.getLength();
        if ( length <= DebugDrawQueueInternal::kDegenerateLength )
            return;
        const float3 direction = shaft * ( 1.0f / length );

        // 머리 한 쌍은 XY 평면 안의 수직(2D 뷰에서 보이는 화살촉), 다른 한 쌍은 그 둘에 수직이다.
        const float32 planarLength = MathUtil::sqrt( direction._x * direction._x + direction._y * direction._y );
        const float3  sideA        = planarLength > DebugDrawQueueInternal::kDegenerateLength ? float3{ -direction._y / planarLength, direction._x / planarLength, 0.0f }
                                                                                              : float3{ 1.0f, 0.0f, 0.0f };
        const float3  sideB        = direction.cross( sideA ).normalize();
        const float32 headLength   = length * kArrowHeadRatio;
        const float32 headWidth    = headLength * 0.5f;
        const float3  headBase     = to - direction * headLength;

        std::scoped_lock<mutex> lock{ _mutex };
        const hashed_string     resolvedCategory = registerCategoryLocked( category );
        pushLineLocked( from, to, color, durationSeconds, resolvedCategory );
        pushLineLocked( to, headBase + sideA * headWidth, color, durationSeconds, resolvedCategory );
        pushLineLocked( to, headBase - sideA * headWidth, color, durationSeconds, resolvedCategory );
        pushLineLocked( to, headBase + sideB * headWidth, color, durationSeconds, resolvedCategory );
        pushLineLocked( to, headBase - sideB * headWidth, color, durationSeconds, resolvedCategory );
    }

    void DebugDrawQueue::drawText( const float3& position, string_view text, const float4& color, float32 durationSeconds, const hashed_string& category )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        DebugText               item{};
        item._position         = position;
        item._color            = color;
        item._text             = string( text );
        item._category         = registerCategoryLocked( category );
        item._remainingSeconds = durationSeconds;
        _listText.push_back( std::move( item ) );
    }

    void DebugDrawQueue::setCategoryEnabled( const hashed_string& category, bool bEnabled )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        const hashed_string     resolved = registerCategoryLocked( category );
        _mapCategoryEnabled[resolved]    = bEnabled ? 1 : 0;
    }

    bool DebugDrawQueue::isCategoryEnabled( const hashed_string& category ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return isCategoryEnabledLocked( category.empty() ? hashed_string( kDefaultCategoryName ) : category );
    }

    void DebugDrawQueue::collectCategories( vector<hashed_string>& outListCategory ) const
    {
        outListCategory.clear();
        {
            std::scoped_lock<mutex> lock{ _mutex };
            outListCategory.reserve( _mapCategoryEnabled.size() );
            for ( const auto& [category, enabled] : _mapCategoryEnabled )
            {
                (void)enabled;
                outListCategory.push_back( category );
            }
        }
        std::sort( outListCategory.begin(), outListCategory.end(), HashedStringLexicalLess{} );
    }

    hashed_string DebugDrawQueue::registerCategoryLocked( const hashed_string& category )
    {
        const hashed_string resolved = category.empty() ? hashed_string( kDefaultCategoryName ) : category;
        if ( _mapCategoryEnabled.find( resolved ) == _mapCategoryEnabled.end() )
            _mapCategoryEnabled.emplace( resolved, uint8{ 1 } );
        return resolved;
    }

    void DebugDrawQueue::pushLineLocked( const float3& from, const float3& to, const float4& color, float32 durationSeconds, const hashed_string& category )
    {
        if ( ( to - from ).getLength() <= DebugDrawQueueInternal::kDegenerateLength )
            return;
        DebugLine line{};
        line._from             = from;
        line._to               = to;
        line._color            = color;
        line._category         = category;
        line._remainingSeconds = durationSeconds;
        _listLine.push_back( line );
    }

    bool DebugDrawQueue::isCategoryEnabledLocked( const hashed_string& category ) const
    {
        const auto it = _mapCategoryEnabled.find( category );
        return it == _mapCategoryEnabled.end() || it->second != 0;
    }
} // namespace sw
