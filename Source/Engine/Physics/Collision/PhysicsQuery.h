/**
 * @file PhysicsQuery.h
 * @brief 레이캐스트, 셰이프 캐스트, 겹침 질의의 필터 조건과 결과입니다. 2D 와 3D 가 같은 템플릿을 씁니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    /**
     * @brief 질의가 무엇을 볼지 정합니다. 레이어는 비트 마스크(비트 n = 레이어 n)이고, 기본은 모든 레이어를 보며 트리거는 제외합니다.
     * @details 레이어 테이블(`PhysicsSettings`)은 바디끼리의 충돌만 정한다. 질의는 이 마스크만 본다(유니티의 `layerMask` 와 같다).
     */
    struct PhysicsQueryFilter
    {
        PhysicsBodyHandle _ignoreBody{};        ///< 이 바디는 보지 않는다(쏘는 쪽 자신).
        uint64            _ignoreUserData{ 0 }; ///< 0 이 아니면 사용자 값이 이것인 바디는 모두 보지 않는다(쏘는 오브젝트의 래그돌 뼈와 무기 전부).
        uint32            _layerMask{ MathUtil::kMaxUInt32 };
        bool              _bIncludeTriggers{ false };

        /** @brief 레이어가 마스크에 포함되는지 반환합니다. */
        bool acceptsLayer( uint8 layer ) const { return layer < 32 && ( _layerMask & ( 1u << layer ) ) != 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 레이나 셰이프 캐스트가 처음 닿은 지점입니다. */
    template <typename TDimension>
    struct PhysicsCastHit
    {
        using Vector = typename TDimension::Vector;

        PhysicsBodyHandle _body{};
        uint64            _userData{ 0 };
        Vector            _point{};          ///< 닿은 점이다(월드).
        Vector            _normal{};         ///< 닿은 면의 법선이다(월드, 쏜 쪽을 향한다).
        float32           _fraction{ 0.0f }; ///< 쏜 길이에 대한 비율이다(0..1).
        float32           _distance{ 0.0f }; ///< 출발점에서 닿은 지점까지의 거리다(미터).
        hashed_string     _material{};       ///< 닿은 셰이프의 물리 재질 이름이다(발소리와 탄흔에 쓰며, 레이캐스트만 채운다).
    };

    using PhysicsCastHit3D = PhysicsCastHit<PhysicsDimension3D>;
    using PhysicsCastHit2D = PhysicsCastHit<PhysicsDimension2D>;
} // namespace sw

namespace sw
{
    /** @brief 여러 닿음을 모으는 질의(`raycastAll`, `shapeCastAll`)가 두 백엔드에서 같은 모양의 결과를 내게 하는 도우미입니다. */
    struct PhysicsQueryUtil
    {
        /**
         * @brief @p inoutListHit 의 @p firstIndex 부터 끝까지를 바디마다 가장 가까운 닿음 하나만 남기고 가까운 순서로 정렬합니다. 남은 수를 돌려줍니다.
         * @details 컴파운드 바디는 서브 셰이프마다 따로 잡히므로 바디 하나가 여러 번 나온다. 거리가 같으면 바디 핸들 순서로 정해 결과가 결정적이다.
         *          출력 끝에서 제자리로 처리하므로 임시 버퍼를 할당하지 않는다.
         */
        template <typename TDimension>
        static uint32 keepNearestHitPerBody( vector<PhysicsCastHit<TDimension>>& inoutListHit, size_t firstIndex )
        {
            using Hit                                  = PhysicsCastHit<TDimension>;
            const typename vector<Hit>::iterator first = inoutListHit.begin() + static_cast<ptrdiff_t>( firstIndex );
            std::sort( first, inoutListHit.end(), []( const Hit& lhs, const Hit& rhs )
            { return lhs._body == rhs._body ? lhs._fraction < rhs._fraction : lhs._body < rhs._body; } );
            inoutListHit.erase( std::unique( first, inoutListHit.end(), []( const Hit& lhs, const Hit& rhs )
            { return lhs._body == rhs._body; } ),
                                inoutListHit.end() );
            std::sort( first, inoutListHit.end(), []( const Hit& lhs, const Hit& rhs )
            { return lhs._fraction == rhs._fraction ? lhs._body < rhs._body : lhs._fraction < rhs._fraction; } );
            return static_cast<uint32>( inoutListHit.size() - firstIndex );
        }
    };
} // namespace sw
