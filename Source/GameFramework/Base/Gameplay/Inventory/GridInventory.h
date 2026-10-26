/**
 * @file GridInventory.h
 * @brief 격자 인벤토리입니다(바이오하자드 4 의 가방, 타르코프, 디아블로). 아이템이 w × h 셀을 차지하고 회전할 수 있으며, 빈 위치를 찾아 넣고, 옮기고, 겹칩니다.
 * @details 아이템의 크기와 겹침 수는 델리게이트(`ShapeDelegate`)로 물어본다. 그래서 특정 카탈로그 종류에 의존하지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Utility/Grid/GridTopology.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    /** @brief 격자 아이템의 모양입니다. 회전하지 않았을 때의 가로 × 세로 셀 수와, 한 위치에 겹칠 수 있는 개수를 담습니다. */
    struct GridItemShape
    {
        int32 _width{ 1 };
        int32 _height{ 1 };
        int32 _maxStack{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 격자에 놓인 아이템 하나입니다. `_x` 와 `_y` 는 왼쪽 위 셀의 좌표입니다. */
    struct GridItem
    {
        hashed_string _itemID{};
        int32         _instanceID{ -1 }; ///< 놓을 때 받는 번호(다시 쓰지 않음)
        int32         _count{ 0 };
        int32         _x{ 0 };
        int32         _y{ 0 };
        uint8         _bRotated{ SW_FALSE }; ///< 회전하면 가로와 세로가 바뀐다
    };
} // namespace sw

namespace sw
{
    /**
     * @class GridInventory
     * @brief 셀마다 차지한 아이템 번호를 보관합니다. 아이템 모양(크기, 겹침)은 `initialize` 에 넘긴 델리게이트로 물어봅니다.
     * @details 빈 위치 찾기는 회전하지 않은 채로 위쪽 줄의 왼쪽부터 먼저 찾고, 없으면 회전해서 같은 순서로 찾는다. 그래서 같은 상태에서는 항상 같은 위치가 나온다.
     *          넣기는 같은 아이템의 덜 찬 위치를 먼저 채우고, 남는 것을 새 위치에 둔다. 빼기는 모두 있을 때만 빼며, 나중에 놓은 위치부터 뺀다.
     */
    class SW_GF_API GridInventory
    {
    public:
        /** @brief 아이템 id 로 모양을 얻는 함수입니다. 알 수 없는 아이템이면 false 를 반환하고, 그 아이템은 놓을 수 없습니다. */
        using ShapeDelegate = Delegate<bool( const hashed_string& itemID, GridItemShape& outShape )>;

        GridInventory();

        /** @brief 칸 수가 1 미만이면 오류를 남기고 false 입니다(조용히 1 로 맞추지 않습니다). */
        [[nodiscard]] bool initialize( const ShapeDelegate& shapeLookup, int32 width, int32 height );
        /** @brief 격자를 키웁니다(허리 가방, 가방 업그레이드). 줄이는 것은 안 되고 false 를 반환합니다. 놓인 아이템은 그대로입니다. */
        [[nodiscard]] bool growGrid( int32 width, int32 height );
        void               clear();

        /** @brief 이 아이템을 이 위치와 방향으로 놓을 수 있는지 판정합니다. @p ignoreInstanceID 의 셀은 빈 것으로 취급합니다(옮기기에 씁니다). */
        bool canPlace( const hashed_string& itemID, int32 x, int32 y, bool bRotated, int32 ignoreInstanceID = -1 ) const;
        /** @brief 빈 위치를 찾습니다. 회전하지 않은 쪽을 먼저, 위에서 아래로, 왼쪽에서 오른쪽으로 찾습니다. */
        [[nodiscard]] bool findFreeSpot( const hashed_string& itemID, int32& outX, int32& outY, bool& outRotated ) const;
        /** @brief 이 위치에 새로 놓습니다. 겹치지 않으며, 받은 번호를 반환합니다. 못 놓으면 -1 입니다. */
        int32 placeItem( const hashed_string& itemID, int32 count, int32 x, int32 y, bool bRotated );
        /** @brief 넣고 넣은 개수를 반환합니다. 덜 찬 위치를 먼저 채우고, 남으면 빈 위치에 둡니다. 위치가 모자라면 일부만 넣습니다. */
        int32 addItem( const hashed_string& itemID, int32 count );
        /** @brief @p count 개가 모두 있으면 뺍니다. 나중에 놓은 위치부터 뺍니다. */
        [[nodiscard]] bool removeItem( const hashed_string& itemID, int32 count );
        /** @brief 한 위치에서 @p count 개까지 빼고 뺀 개수를 반환합니다. 개수가 0 이 되면 그 위치가 비워집니다. */
        int32 takeFromInstance( int32 instanceID, int32 count );
        /** @brief 옮깁니다. 방향도 바꿀 수 있습니다. 목적지가 막혀 있으면 그대로 두고 false 를 반환합니다. */
        [[nodiscard]] bool moveItem( int32 instanceID, int32 x, int32 y, bool bRotated );
        /** @brief 왼쪽 위 셀을 고정한 채 제자리에서 회전합니다. 막히면 false 입니다. */
        [[nodiscard]] bool rotateItem( int32 instanceID );

        /** @brief 셀을 차지한 아이템 번호입니다. 비어 있거나 격자 밖이면 -1 입니다. */
        int32                   findInstanceAt( int32 x, int32 y ) const;
        const GridItem*         findInstance( int32 instanceID ) const;
        int32                   getItemCount( const hashed_string& itemID ) const;
        bool                    hasItem( const hashed_string& itemID, int32 count = 1 ) const { return getItemCount( itemID ) >= count; }
        int32                   countFreeCells() const;
        const vector<GridItem>& getItems() const { return _listItem; }
        int32                   getWidth() const { return _topology._width; }
        int32                   getHeight() const { return _topology._height; }
        uint32                  getRevision() const { return _revision; }

        /** @brief 크기, 셀, 아이템(id, 번호, 개수, 위치, 회전 여부), 다음 번호를 씁니다. 모양 조회 함수는 `initialize` 가 받은 것이므로 저장하지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 바뀌지 않습니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        bool  findShape( const hashed_string& itemID, GridItemShape& outShape ) const;
        int32 findItemIndex( int32 instanceID ) const;
        void  computeFootprint( const GridItemShape& shape, bool bRotated, int32& outWidth, int32& outHeight ) const;
        void  stampItem( const GridItem& item, int32 value );
        void  eraseItemAt( size_t itemIndex );

        vector<int32>    _listCell; ///< 셀마다 차지한 번호(-1 이면 빔), 줄 단위로 나열
        vector<GridItem> _listItem; ///< 놓은 순서
        ShapeDelegate    _shapeLookup;
        GridTopology     _topology; ///< 격자의 너비, 높이, 셀 번호 계산
        int32            _nextInstanceID;
        uint32           _revision;
    };
} // namespace sw
