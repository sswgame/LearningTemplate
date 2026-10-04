/**
 * @file GridInventory.h
 * @brief 격자 인벤토리(바이오하자드 4 의 가방 · 타르코프 · 디아블로) — 아이템이 w × h 칸을 차지하고 돌릴 수 있으며, 빈자리를 찾아 넣고 옮기고 겹칩니다.
 * @details 아이템의 크기 · 겹침 수는 연결 함수(`ShapeDelegate`)로 묻습니다 — 카탈로그 종류에 묶이지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 격자 아이템의 모양입니다(돌리지 않았을 때 가로 × 세로 칸, 한 자리에 겹치는 수). */
    struct GridItemShape
    {
        int32 _width{ 1 };
        int32 _height{ 1 };
        int32 _maxStack{ 1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 격자에 놓인 아이템 하나입니다. `_x` · `_y` 는 왼쪽 위 칸입니다. */
    struct GridItem
    {
        hashed_string _itemId{};
        int32         _instanceId{ -1 }; ///< 놓을 때 받는 번호(다시 쓰지 않는다)
        int32         _count{ 0 };
        int32         _x{ 0 };
        int32         _y{ 0 };
        uint8         _bRotated{ SW_FALSE }; ///< 돌리면 가로 · 세로가 바뀐다
    };
} // namespace sw

namespace sw
{
    /**
     * @class GridInventory
     * @brief 칸마다 차지한 아이템 번호를 들고 있습니다. 아이템 모양(크기 · 겹침)은 `initialize` 에 넘긴 연결 함수로 묻습니다.
     * @details 빈자리 찾기는 돌리지 않은 채로 위 줄부터 왼쪽부터 먼저 보고, 없으면 돌려서 같은 순서로 봅니다 — 늘 같은 자리입니다.
     *          넣기는 같은 아이템의 덜 찬 자리를 먼저 채우고 남는 것을 새 자리에 둡니다. 빼기는 모두 있을 때만 뺍니다(나중에 놓은 자리부터).
     */
    class SW_GF_API GridInventory
    {
    public:
        /** @brief 아이템 id → 모양. 모르는 아이템이면 false(놓을 수 없다). */
        using ShapeDelegate = Delegate<bool( const hashed_string& itemId, GridItemShape& outShape )>;

        GridInventory();

        void initialize( const ShapeDelegate& shapeLookup, int32 width, int32 height );
        /** @brief 격자를 키웁니다(허리 가방 · 가방 업그레이드). 줄이기는 안 됩니다(false). 놓인 아이템은 그대로입니다. */
        [[nodiscard]] bool growGrid( int32 width, int32 height );
        void               clear();

        /** @brief 이 아이템을 이 자리 · 방향으로 놓을 수 있는가입니다. @p ignoreInstanceId 의 칸은 빈 것으로 봅니다(옮기기). */
        bool canPlace( const hashed_string& itemId, int32 x, int32 y, bool bRotated, int32 ignoreInstanceId = -1 ) const;
        /** @brief 빈자리를 찾습니다(돌리지 않은 쪽 먼저, 위 → 아래, 왼 → 오른). */
        [[nodiscard]] bool findFreeSpot( const hashed_string& itemId, int32& outX, int32& outY, bool& outRotated ) const;
        /** @brief 이 자리에 새로 놓습니다(겹치지 않는다). 받은 번호, 못 놓으면 −1 입니다. */
        int32 placeItem( const hashed_string& itemId, int32 count, int32 x, int32 y, bool bRotated );
        /** @brief 넣고 넣은 개수를 돌려줍니다(덜 찬 자리 먼저, 남으면 빈자리 — 자리가 모자라면 일부만). */
        int32 addItem( const hashed_string& itemId, int32 count );
        /** @brief @p count 개가 모두 있으면 뺍니다(나중에 놓은 자리부터). */
        [[nodiscard]] bool removeItem( const hashed_string& itemId, int32 count );
        /** @brief 한 자리에서 @p count 개까지 빼고 뺀 개수를 돌려줍니다. 0 이 되면 자리가 빕니다. */
        int32 takeFromInstance( int32 instanceId, int32 count );
        /** @brief 옮깁니다(방향도 바꿀 수 있다). 자리가 막혔으면 그대로 두고 false 입니다. */
        [[nodiscard]] bool moveItem( int32 instanceId, int32 x, int32 y, bool bRotated );
        /** @brief 제자리(왼쪽 위 고정)에서 돌립니다. 막히면 false 입니다. */
        [[nodiscard]] bool rotateItem( int32 instanceId );

        /** @brief 칸을 차지한 아이템 번호입니다. 비었거나 밖이면 −1 입니다. */
        int32                   findInstanceAt( int32 x, int32 y ) const;
        const GridItem*         findInstance( int32 instanceId ) const;
        int32                   getItemCount( const hashed_string& itemId ) const;
        bool                    hasItem( const hashed_string& itemId, int32 count = 1 ) const { return getItemCount( itemId ) >= count; }
        int32                   countFreeCells() const;
        const vector<GridItem>& getItems() const { return _listItem; }
        int32                   getWidth() const { return _width; }
        int32                   getHeight() const { return _height; }
        uint32                  getRevision() const { return _revision; }

    private:
        bool  findShape( const hashed_string& itemId, GridItemShape& outShape ) const;
        int32 findItemIndex( int32 instanceId ) const;
        void  computeFootprint( const GridItemShape& shape, bool bRotated, int32& outWidth, int32& outHeight ) const;
        void  stampItem( const GridItem& item, int32 value );
        void  eraseItemAt( size_t itemIndex );

        vector<int32>    _listCell; ///< 칸마다 차지한 번호(−1 = 빔), 줄 단위
        vector<GridItem> _listItem; ///< 놓은 순서
        ShapeDelegate    _shapeLookup;
        int32            _width;
        int32            _height;
        int32            _nextInstanceId;
        uint32           _revision;
    };
} // namespace sw
