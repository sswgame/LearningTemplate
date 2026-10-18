/**
 * @file LandRegistry.h
 * @brief 공유 땅을 정의합니다.
 * @details 칸마다 그 칸을 쓰는 키트(주인)와 지나갈 수 있는지를 든다. 땅에 무언가 놓는 키트는 놓기 전에 얻고(`claimRect`), 치우면 놓는다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Utility/Grid/GridTopology.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    /**
     * @class LandRegistry
     * @brief 월드 XZ 평면의 칸 표입니다. 원점과 칸 크기를 가지며 `NavGrid` 와 같은 모양이고, 주인 0 은 빈 땅입니다.
     * @details "(x, y) 에 무엇이 있나"의 원본은 키트마다의 격자(밭 칸, 마을 오브젝트, 도시 칸)이고, 이 표는 "누가 그 칸을 쓰는가"만 든다.
     *          언리얼 World Partition 의 셀 소유와 내비 변경 알림에 해당한다.
     *          둘 이상의 격자 키트를 한 맵에 겹칠 때 남의 칸에 놓지 못하게 한다.
     *          - 주인 번호는 등록 순서이지만 상태 바이트에는 **이름**으로 싣는다. 다른 실행에서 등록 순서가 달라도 맞게 하려는 것이다.
     *          - 얻기는 사각 전체가 비었거나 내 것일 때만 된다. 반쯤 얻는 일은 없다.
     *          - 막힌 칸(`bBlocking`)은 다른 키트의 길찾기가 피한다. `getRevision` 이 바뀌면 다시 읽어야 한다.
     */
    class SW_GF_API LandRegistry
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "LAND" );
        static constexpr uint32 kStateVersion = 1;
        static constexpr uint16 kNoOwner      = 0;

        LandRegistry();

        /** @brief 땅을 엽니다. 모든 칸이 빈 땅이 되고 주인 등록도 비웁니다. 칸 크기는 0 보다 커야 합니다(전제 조건이며 맞추지 않습니다. 판 상태는 `SessionStateSettings::_landCellSize` 의 `Min` 으로 받을 때 거절합니다). */
        void initialize( int32 width, int32 height, float32 cellSize, const float3& origin );
        /** @brief 주인(키트) 하나를 등록하고 번호를 줍니다. 같은 이름이면 같은 번호이며, 빈 이름이거나 땅이 안 열렸으면 `kNoOwner` 입니다. */
        uint16 registerOwner( const hashed_string& ownerName );

        /** @brief 사각(양 끝 포함)을 모두 얻습니다. 밖이거나 남의 칸이 하나라도 있으면 아무것도 바꾸지 않고 false 입니다. 내 칸은 막힘만 새로 씁니다. */
        [[nodiscard]] bool claimRect( uint16 owner, int32 minX, int32 minY, int32 maxX, int32 maxY, bool bBlocking );
        /** @brief 내 칸만 놓습니다. 남의 칸과 빈 칸은 그대로 두고, 밖은 잘라 냅니다. */
        void releaseRect( uint16 owner, int32 minX, int32 minY, int32 maxX, int32 maxY );
        /** @brief 월드 사각(가운데와 크기, XZ)을 덮는 칸을 얻습니다. 칸 격자가 없는 키트(공원 놀이기구)가 씁니다. */
        [[nodiscard]] bool claimWorldRect( uint16 owner, const float3& center, const float3& size, bool bBlocking );
        /** @brief 월드 사각(가운데와 크기, XZ)을 덮는 내 칸을 놓습니다. */
        void releaseWorldRect( uint16 owner, const float3& center, const float3& size );

        bool   isInitialized() const { return _topology.getCellCount() > 0; }
        bool   isInside( int32 x, int32 y ) const { return _topology.isInside( x, y ); }
        uint16 getOwner( int32 x, int32 y ) const;
        /** @brief 칸 주인의 이름을 반환합니다. 빈 땅이거나 밖이면 빈 이름입니다. */
        hashed_string getOwnerName( int32 x, int32 y ) const;
        /** @brief @p owner 가 쓸 수 있는 칸이면 true 입니다. 빈 땅이거나 내 것이어야 하며, 밖이면 false 입니다. */
        bool isUsableBy( uint16 owner, int32 x, int32 y ) const;
        /** @brief 남이 막아 둔 칸이면 true 입니다. @p owner 의 칸은 막힘이 아니며, 밖이면 false 입니다. */
        bool                isBlockedFor( uint16 owner, int32 x, int32 y ) const;
        int2                computeCell( const float3& worldPosition ) const;
        uint32              getRevision() const { return _revision; }
        const GridTopology& getTopology() const { return _topology; }
        float32             getCellSize() const { return _cellSize; }
        const float3&       getOrigin() const { return _origin; }

        /** @brief 크기, 주인 이름 목록, 칸마다 (주인 번호, 막힘)을 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 상태를 바꿉니다. 저장된 이름은 지금 번호로 다시 매기며(없는 이름은 새로 등록), 크기가 지금과 다르거나 바이트가 깨졌으면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 한 칸입니다. 주인 번호와 막힘을 담습니다. */
        struct Cell
        {
            uint16 _owner{ kNoOwner };
            uint8  _bBlocking{ SW_FALSE };
        };

        /** @brief 월드 사각을 칸 사각(양 끝 포함)으로 바꿉니다. */
        void computeWorldRect( const float3& center, const float3& size, int2& outMin, int2& outMax ) const;

        vector<Cell>          _listCell;
        vector<hashed_string> _listOwnerName; ///< 번호 − 1 자리의 이름
        GridTopology          _topology;
        float3                _origin;
        float32               _cellSize;
        uint32                _revision;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct LandBinding
     * @brief 키트 하나가 빌린 땅입니다. 땅, 키트 칸 (0, 0) 이 놓인 땅 칸, 주인 번호를 담습니다.
     * @details 키트 칸 좌표로 얻고 놓는다. 땅 칸은 원점에 키트 칸을 더한 값이다.
     *          묶지 않았으면(`_pLand` 가 nullptr) 키트는 단독으로 동작한다. 얻기는 늘 되고 막힌 칸은 없다.
     *          키트를 지울 때 땅을 놓지 않는다. 키트 값은 옮겨지고 복원되기 때문이며, 땅을 놓는 것은 키트의 명시적인 치우기와 `unbind…` 뿐이다.
     */
    struct LandBinding
    {
        LandRegistry* _pLand{ nullptr };
        int2          _origin{};
        uint16        _owner{ LandRegistry::kNoOwner };

        /** @brief 땅에 묶습니다. @p pLand 가 nullptr 이면 풀며, 이미 얻은 칸은 그대로 둡니다. 주인 이름은 키트 이름입니다. */
        void bind( LandRegistry* pLand, const int2& origin, const hashed_string& ownerName )
        {
            _pLand  = pLand;
            _origin = origin;
            _owner  = pLand != nullptr ? pLand->registerOwner( ownerName ) : LandRegistry::kNoOwner;
        }
        bool isBound() const { return _pLand != nullptr && _owner != LandRegistry::kNoOwner; }
        /** @brief 키트 칸 사각(양 끝 포함)을 얻습니다. 묶지 않았으면 true 입니다. */
        [[nodiscard]] bool claimRect( int32 minX, int32 minY, int32 maxX, int32 maxY, bool bBlocking ) const
        {
            return isBound() == false || _pLand->claimRect( _owner, _origin._x + minX, _origin._y + minY, _origin._x + maxX, _origin._y + maxY, bBlocking );
        }
        void releaseRect( int32 minX, int32 minY, int32 maxX, int32 maxY ) const
        {
            if ( isBound() )
                _pLand->releaseRect( _owner, _origin._x + minX, _origin._y + minY, _origin._x + maxX, _origin._y + maxY );
        }
        /** @brief 키트 칸을 이 키트가 쓸 수 있으면 true 입니다. 빈 땅이거나 내 것이어야 하며, 묶지 않았으면 true 입니다. */
        bool isUsable( int32 x, int32 y ) const { return isBound() == false || _pLand->isUsableBy( _owner, _origin._x + x, _origin._y + y ); }
        /** @brief 키트 칸을 다른 키트가 막아 두었으면 true 입니다. 묶지 않았으면 false 입니다. */
        bool   isBlocked( int32 x, int32 y ) const { return isBound() && _pLand->isBlockedFor( _owner, _origin._x + x, _origin._y + y ); }
        uint32 getRevision() const { return _pLand != nullptr ? _pLand->getRevision() : 0u; }
    };
} // namespace sw
