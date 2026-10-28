/**
 * @file EquipCondition.h
 * @brief 장착 조건입니다. 아이템을 끼려면 무엇이 있어야 하는지(세트 완성, 세트 n 조각, 다른 장비의 태그, 캐릭터 태그, 체형)와 조건이 깨질 때의 처리를 정합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/TagID.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct EquipSlot;

    class ItemCatalog;

    /** @brief 장착 조건의 종류입니다. */
    enum class EquipConditionKind : uint8
    {
        SetComplete = 0, ///< 세트의 모든 조각을 끼고 있다
        SetPieces,       ///< 세트 조각을 `_pieceCount` 개 이상 끼고 있다
        EquippedTag,     ///< 다른 슬롯의 장비 하나가 태그(하위 태그 포함)를 가지고 있다
        CharacterTag,    ///< 캐릭터가 태그(하위 태그 포함)를 가지고 있다
        BodyShape        ///< 캐릭터의 체형이 목록에 있는 것 중 하나다
    };

    /** @brief 장착 조건이 깨질 때(세트 한 조각을 벗거나 태그를 잃을 때)의 처리 방식입니다. 아이템마다 데이터로 고릅니다. */
    ENUM()
    enum class EquipBreakPolicy : uint8
    {
        UnequipTogether = 0, ///< 함께 벗는다(벗은 것은 호출한 쪽에 돌려준다)
        KeepHidden,          ///< 낀 채로 둔다. 외형에서 숨기고, 능력치와 다른 조건에서는 세지 않는다
        RefuseUnequip        ///< 조건을 깨뜨리는 벗기와 바꿔 끼기를 거부한다. 캐릭터 쪽 변화로 깨지면 숨김으로 둔다
    };

    /** @brief 데이터 이름을 정책으로 읽습니다. 알 수 없는 이름이면 false 입니다. */
    [[nodiscard]] SW_GF_API bool parseEquipBreakPolicy( string_view text, EquipBreakPolicy& outPolicy );

    /** @brief 장착 조건 하나입니다. 한 아이템의 조건은 모두 맞아야 합니다(AND). */
    struct EquipCondition
    {
        vector<hashed_string> _listBodyShape{}; ///< BodyShape
        hashed_string         _setID{};         ///< SetComplete, SetPieces 에서 쓴다
        TagID                 _tag{};           ///< EquippedTag, CharacterTag 에서 쓴다
        int32                 _pieceCount{ 0 }; ///< SetPieces
        EquipConditionKind    _kind{ EquipConditionKind::SetComplete };
    };
} // namespace sw

namespace sw
{
    /** @brief 장착 조건이 참조하는 캐릭터 쪽 정보입니다. 캐릭터의 태그나 체형이 바뀔 때 게임이 넘겨 줍니다. */
    struct EquipCharacterContext
    {
        TagContainer  _tags{};
        hashed_string _bodyShape{}; ///< 체형 id(`BodyShape` 조건)
        hashed_string _bodyType{};  ///< 몸 종류(성별, 종족). 세트의 몸 변형을 고르는 데 쓴다
    };
} // namespace sw

namespace sw
{
    /** @brief 세트 조각을 몇 개 끼고 있는지 나타냅니다. */
    struct EquipSetProgress
    {
        int32 _equippedCount{ 0 };
        int32 _totalCount{ 0 };

        bool isComplete() const { return _totalCount > 0 && _equippedCount >= _totalCount; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class IEquipSetLookup
     * @brief 장비 세트의 조각 정보를 `Equipment` 에 넘기는 인터페이스입니다. 세트 데이터는 외형 쪽(`EquipSetCatalog`)에 있고, 장착 규칙은 그 데이터를 참조만 합니다.
     */
    class IEquipSetLookup
    {
    public:
        IEquipSetLookup()                                        = default;
        virtual ~IEquipSetLookup()                               = default;
        IEquipSetLookup( const IEquipSetLookup& )                = default;
        IEquipSetLookup& operator=( const IEquipSetLookup& )     = default;
        IEquipSetLookup( IEquipSetLookup&& ) noexcept            = default;
        IEquipSetLookup& operator=( IEquipSetLookup&& ) noexcept = default;

        virtual bool hasSet( const hashed_string& setID ) const = 0;
        /** @brief 슬롯 목록에서 세트 조각을 셉니다. 숨김(`_bSuppressed`) 슬롯은 세지 않습니다. */
        virtual EquipSetProgress computeProgress( const hashed_string& setID, const vector<EquipSlot>& listSlot, const hashed_string& bodyType ) const = 0;
        /** @brief 세트의 조각이 될 수 있는 모든 아이템(모든 몸 변형)을 추가합니다. 조건 순환 검사가 씁니다. */
        virtual void collectSetItems( const hashed_string& setID, vector<hashed_string>& outListItemID ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @struct EquipConditionUtil
     *  @brief 장착 조건 판정과 조건 순환 검사를 제공합니다. */
    struct SW_GF_API EquipConditionUtil
    {
        /**
         * @brief 슬롯 @p selfSlotIndex 의 아이템 조건 @p condition 이 @p listSlot 에서 맞는지 판정합니다.
         * @details 다른 장비의 태그는 자기 슬롯과 숨김 슬롯을 빼고 검사한다. 세트 조건은 @p pSetLookup 이 없으면 맞지 않는다.
         */
        static bool isMet( const EquipCondition& condition, const vector<EquipSlot>& listSlot, int32 selfSlotIndex, const ItemCatalog& catalog,
                           const IEquipSetLookup* pSetLookup, const EquipCharacterContext& context );

        /**
         * @brief 아이템 조건끼리의 순환(A 가 B 를, B 가 A 를 요구)을 찾습니다. 순환이 있으면 그 경로를 @p outListCycle 에 담고 true 를 반환합니다.
         * @details 간선은 "A 의 조건을 B 가 채울 수 있다" 는 뜻이다. A 가 요구하는 세트의 조각이나 A 가 요구하는 태그를 가진 아이템이 B 가 된다.
         *          조건이 없는 아이템은 경로를 연결하지 않는다. 그냥 끼면 되기 때문이다. 자기 자신으로 가는 간선은 세지 않는다. 조건은 자기를 낀 상태로 판정하기 때문이다.
         */
        [[nodiscard]] static bool findConditionCycle( const ItemCatalog& catalog, const IEquipSetLookup* pSetLookup, vector<hashed_string>& outListCycle );
    };
} // namespace sw
