/**
 * @file EquipmentSetCatalog.h
 * @brief 장비 세트입니다. 슬롯별 장비 목록(한 번에 입힘), 몸 종류(성별, 종족)별 변형, 세트를 모두 갖추면 한 외형으로 바꾸는 "세트 완성 표현" 을 담습니다.
 * @details 세트 효과 같은 수치는 게임플레이(어빌리티 시스템)가 맡으므로 여기에 두지 않는다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Inventory/EquipmentCondition.h"

namespace sw
{
    class AppearanceLoadReport;
    class XMLNode;

    /** @brief 세트 조각입니다. 슬롯 하나와, 그 슬롯에서 조각으로 인정하는 아이템 목록을 담습니다. 목록의 첫 아이템이 세트를 입힐 때 고르는 아이템입니다. */
    REFLECT()
    struct SW_GF_API EquipmentSetPieceDef
    {
        REFLECT_BODY();

        vector<hashed_string> _listItem{}; ///< `items="a,b"` — 이름 목록은 손으로 푼다
        PROPERTY( Required )
        hashed_string _slot{};
    };
} // namespace sw

namespace sw
{
    /** @brief 몸 종류 변형입니다. 같은 슬롯의 조각을 바꿉니다. */
    REFLECT()
    struct SW_GF_API EquipmentSetVariantDef
    {
        REFLECT_BODY();

        vector<EquipmentSetPieceDef> _listPiece{};
        PROPERTY( Required )
        hashed_string _bodyType{};
    };
} // namespace sw

namespace sw
{
    /** @brief 세트 하나입니다. */
    REFLECT()
    struct SW_GF_API EquipmentSetDef
    {
        REFLECT_BODY();

        vector<EquipmentSetPieceDef>   _listPiece{};
        vector<EquipmentSetVariantDef> _listVariant{};
        vector<hashed_string>          _listCompleteSlot{}; ///< 세트 완성 표현이 대신하는 슬롯(첫 슬롯이 그 외형을 가진다)
        PROPERTY( Required )
        hashed_string _id{};
        hashed_string _completeVisual{}; ///< 세트 완성 표현의 외형. 비어 있으면 없음(`<Complete visual slots>`)
    };

    /** @brief 세트 완성 표현(`<Complete visual slots/>`)의 칸입니다. 슬롯 목록은 손으로 푼다. */
    REFLECT()
    struct SW_GF_API EquipmentSetCompleteFields
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _visual{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class EquipmentSetCatalog
     * @brief `<EquipmentSetCatalog><Set id="Recon"><Piece slot="Head" items="recon_helmet"/><Variant bodyType="Female"><Piece slot="Body" items="recon_vest_f"/></Variant>
     *        <Complete visual="recon_full" slots="Head,Body"/></Set></EquipmentSetCatalog>` 형식으로 읽습니다.
     * @details 장착 조건(`Equipment`)에는 `IEquipmentSetLookup` 으로 세트 정보를 넘겨 준다. 소유권은 넘기지 않는다.
     */
    class SW_GF_API EquipmentSetCatalog : public IEquipmentSetLookup
    {
    public:
        [[nodiscard]] bool loadFromNode( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear() { _listSet.clear(); }

        const EquipmentSetDef*         findSet( const hashed_string& id ) const;
        const vector<EquipmentSetDef>& getSets() const { return _listSet; }
        /** @brief @p bodyType 의 조각 목록입니다. 기본 조각에 몸 변형을 슬롯별로 덮어쓴 결과입니다. */
        void computePieces( const EquipmentSetDef& def, const hashed_string& bodyType, vector<EquipmentSetPieceDef>& outListPiece ) const;

        /**
         * @brief 슬롯에서 아이템을 찾는 함수(@p findItemInSlot( slot ) → 아이템 id, 비어 있으면 없음)로 조각 수를 셉니다. 외형 해석과 장착 조건이 같은 계산을 씁니다.
         */
        template <typename TFindItem>
        EquipmentSetProgress computeProgressWith( const EquipmentSetDef& def, const hashed_string& bodyType, TFindItem&& findItemInSlot ) const
        {
            vector<EquipmentSetPieceDef> listPiece;
            computePieces( def, bodyType, listPiece );
            EquipmentSetProgress progress;
            progress._totalCount = static_cast<int32>( listPiece.size() );
            for ( const EquipmentSetPieceDef& piece : listPiece )
            {
                const hashed_string itemID = findItemInSlot( piece._slot );
                if ( itemID.empty() )
                    continue;
                for ( const hashed_string& pieceItem : piece._listItem )
                {
                    if ( pieceItem == itemID )
                    {
                        ++progress._equippedCount;
                        break;
                    }
                }
            }
            return progress;
        }

        bool                 hasSet( const hashed_string& setID ) const override { return findSet( setID ) != nullptr; }
        EquipmentSetProgress computeProgress( const hashed_string& setID, const vector<EquipmentSlot>& listSlot, const hashed_string& bodyType ) const override;
        void                 collectSetItems( const hashed_string& setID, vector<hashed_string>& outListItemID ) const override;

    private:
        vector<EquipmentSetDef> _listSet{};
    };
} // namespace sw
