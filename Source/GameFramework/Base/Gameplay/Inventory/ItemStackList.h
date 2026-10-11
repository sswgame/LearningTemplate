/**
 * @file ItemStackList.h
 * @brief 아이템 id 와 개수의 값 목록입니다. 레시피 재료, 전리품, 퀘스트 보상, 출하 대기, 도시 물자가 함께 씁니다.
 * @details 슬롯, 무게, 꾸미기, 소유권을 가진 가방은 `Inventory` 이다. 이 클래스는 "아이템 몇 개" 라는 값일 뿐이어서 슬롯이 없다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    /**
     * @struct ItemStack
     * @brief 값 목록의 원소입니다. 아이템 하나와 그 개수를 담으며 인스턴스 상태는 없습니다. 가방의 슬롯 하나는 `InventorySlot` 입니다.
     */
    struct ItemStack
    {
        hashed_string _itemID{};
        int32         _count{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ItemStackList
     * @brief 같은 아이템은 한 원소로 합치고, 개수가 0 이 되면 원소를 지웁니다. 순서는 처음 추가한 순서입니다.
     * @details 슬롯이나 무게 같은 장르 규칙은 이 클래스에 추가하지 않는다. 그런 규칙은 `Inventory` 와 `GridInventory` 가 담당한다. 원소가 몇 개뿐인 목록이라 찾기는 앞에서부터 순회한다.
     */
    class SW_GF_API ItemStackList
    {
    public:
        ItemStackList();

        /** @brief 추가합니다. id 가 비어 있거나 개수가 0 이하이면 아무것도 하지 않습니다. */
        void addItem( const hashed_string& itemID, int32 count );
        /** @brief 뺍니다. 모자라면 빼지 않고 false 를 반환합니다. */
        [[nodiscard]] bool removeItem( const hashed_string& itemID, int32 count );
        /** @brief 이 목록의 @p count 개를 @p target 으로 옮깁니다. 모자라면 false 를 반환합니다. */
        [[nodiscard]] bool moveItemTo( ItemStackList& target, const hashed_string& itemID, int32 count );
        void               clear() { _listStack.clear(); }

        int32 getItemCount( const hashed_string& itemID ) const;
        bool  hasItem( const hashed_string& itemID, int32 count = 1 ) const { return getItemCount( itemID ) >= count; }
        /** @brief 모든 아이템 개수의 합입니다. */
        int32 getTotalCount() const;
        bool  isEmpty() const { return _listStack.empty(); }
        /** @brief 가지고 있는 아이템 id 목록입니다. 처음 추가한 순서입니다. */
        void                     getItemIDs( vector<hashed_string>& outListItem ) const;
        const vector<ItemStack>& getItems() const { return _listStack; }

        /** @brief 아이템을 이름 순으로 씁니다. 같은 목록이면 추가한 순서와 상관없이 같은 바이트가 나옵니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 목록을 복원합니다. 데이터가 깨졌으면 false 를 반환하고 목록은 바뀌지 않습니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        ItemStack* findStack( const hashed_string& itemID );

        vector<ItemStack> _listStack;
    };
} // namespace sw
