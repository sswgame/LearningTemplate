/**
 * @file LandRegistry.h
 * @brief 공유 땅 — 칸마다 그 칸을 쓰는 키트(주인)와 지나갈 수 있는지를 듭니다. 땅에 무언가 놓는 키트는 놓기 전에 얻고(`claimRect`) 치우면 놓습니다.
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
     * @brief 월드 XZ 평면의 칸 표입니다(원점 · 칸 크기 — `NavGrid` 와 같은 모양). 주인 0 은 빈 땅입니다.
     * @details "(x, y) 에 무엇이 있나" 의 정본은 키트마다의 격자(밭 칸 · 마을 오브젝트 · 도시 칸)이고, 이 표는 "누가 그 칸을 쓰는가" 만 듭니다
     *          (언리얼 World Partition 의 셀 소유 · 내비 변경 알림의 자리). 둘 이상의 격자 키트를 한 맵에 겹칠 때 남의 칸에 놓지 못하게 합니다.
     *          - 주인 번호는 등록 순서지만 상태 바이트에는 **이름**으로 싣는다(다른 실행에서 등록 순서가 달라도 맞게).
     *          - 얻기는 사각 전부가 비었거나 내 것일 때만 된다(반쯤 얻지 않는다).
     *          - 막힌 칸(`bBlocking`)은 다른 키트의 길찾기가 피한다 — `getRevision` 이 바뀌면 다시 읽는다.
     */
    class SW_GF_API LandRegistry
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "LAND" );
        static constexpr uint32 kStateVersion = 1;
        static constexpr uint16 kNoOwner      = 0;

        LandRegistry();

        /** @brief 땅을 엽니다(모든 칸이 빈 땅, 주인 등록도 비운다). 칸 크기는 0 보다 커야 합니다. */
        void initialize( int32 width, int32 height, float32 cellSize, const float3& origin );
        /** @brief 주인(키트) 하나를 등록하고 번호를 줍니다. 같은 이름이면 같은 번호입니다. 빈 이름 · 땅이 안 열렸으면 `kNoOwner` 입니다. */
        uint16 registerOwner( const hashed_string& ownerName );

        /** @brief 사각(양 끝 포함)을 모두 얻습니다. 밖이거나 남의 칸이 하나라도 있으면 아무것도 바꾸지 않고 false 입니다. 내 칸은 막힘만 새로 씁니다. */
        [[nodiscard]] bool claimRect( uint16 owner, int32 minX, int32 minY, int32 maxX, int32 maxY, bool bBlocking );
        /** @brief 내 칸만 놓습니다(남의 칸 · 빈 칸은 그대로). 밖은 잘라 냅니다. */
        void releaseRect( uint16 owner, int32 minX, int32 minY, int32 maxX, int32 maxY );
        /** @brief 월드 사각(가운데 · 크기, XZ)을 덮는 칸을 얻습니다 — 칸 격자가 없는 키트(공원 놀이기구)용. */
        [[nodiscard]] bool claimWorldRect( uint16 owner, const float3& center, const float3& size, bool bBlocking );
        /** @brief 월드 사각(가운데 · 크기, XZ)을 덮는 내 칸을 놓습니다. */
        void releaseWorldRect( uint16 owner, const float3& center, const float3& size );

        bool   isInitialized() const { return _topology.getCellCount() > 0; }
        bool   isInside( int32 x, int32 y ) const { return _topology.isInside( x, y ); }
        uint16 getOwner( int32 x, int32 y ) const;
        /** @brief 칸 주인의 이름입니다(빈 땅 · 밖이면 빈 이름). */
        hashed_string getOwnerName( int32 x, int32 y ) const;
        /** @brief @p owner 가 쓸 수 있는 칸입니다(빈 땅이거나 내 것). 밖이면 false 입니다. */
        bool isUsableBy( uint16 owner, int32 x, int32 y ) const;
        /** @brief 남이 막아 둔 칸입니다(@p owner 의 칸은 막힘이 아니다). 밖이면 false 입니다. */
        bool                isBlockedFor( uint16 owner, int32 x, int32 y ) const;
        int2                computeCell( const float3& worldPosition ) const;
        uint32              getRevision() const { return _revision; }
        const GridTopology& getTopology() const { return _topology; }
        float32             getCellSize() const { return _cellSize; }
        const float3&       getOrigin() const { return _origin; }

        /** @brief 크기 · 주인 이름 목록 · 칸마다 (주인 번호, 막힘)을 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 저장된 이름은 지금 번호로 다시 매기고(없는 이름은 새로 등록), 크기가 지금과 다르거나 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        /** @brief 한 칸 — 주인 번호와 막힘. */
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
     * @brief 키트 하나가 빌린 땅 — 땅 · 키트 칸 (0, 0) 이 놓인 땅 칸 · 주인 번호입니다. 키트 칸 좌표로 얻고 놓습니다(땅 칸 = 원점 + 키트 칸).
     * @details 묶지 않았으면(`_pLand` 가 nullptr) 키트는 단독 — 얻기는 늘 되고 막힌 칸은 없습니다. 키트를 지울 때 땅을 놓지 않습니다(키트 값은 옮겨지고
     *          되살려진다 — 놓는 것은 키트의 명시적인 치우기 · `unbind…` 뿐이다).
     */
    struct LandBinding
    {
        LandRegistry* _pLand{ nullptr };
        int2          _origin{};
        uint16        _owner{ LandRegistry::kNoOwner };

        /** @brief 묶습니다(@p pLand 가 nullptr 이면 풉니다 — 이미 얻은 칸은 그대로). 주인 이름은 키트 이름입니다. */
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
        /** @brief 키트 칸을 이 키트가 쓸 수 있는가입니다(빈 땅이거나 내 것). 묶지 않았으면 true 입니다. */
        bool isUsable( int32 x, int32 y ) const { return isBound() == false || _pLand->isUsableBy( _owner, _origin._x + x, _origin._y + y ); }
        /** @brief 키트 칸을 다른 키트가 막아 두었는가입니다. 묶지 않았으면 false 입니다. */
        bool   isBlocked( int32 x, int32 y ) const { return isBound() && _pLand->isBlockedFor( _owner, _origin._x + x, _origin._y + y ); }
        uint32 getRevision() const { return _pLand != nullptr ? _pLand->getRevision() : 0u; }
    };
} // namespace sw
