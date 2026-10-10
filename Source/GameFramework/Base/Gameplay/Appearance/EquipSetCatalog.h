/**
 * @file EquipSetCatalog.h
 * @brief 장비 세트 — 칸별 장비 목록(한 번에 입힘), 몸 종류(성별 · 종족)별 변형, 세트를 다 갖추면 한 외형으로 바꾸는 "세트 완성 표현" 입니다.
 * @details 세트 효과 같은 수치는 게임플레이(어빌리티 시스템) 몫이라 여기 두지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Gameplay/Inventory/EquipCondition.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AppearanceLoadReport;
    class XMLNode;

    /** @brief 세트 조각 — 칸 하나와 그 칸에서 조각으로 치는 아이템들(첫 것이 세트를 입힐 때 고르는 것)입니다. */
    struct EquipSetPieceDef
    {
        vector<hashed_string> _listItem{};
        hashed_string         _slot{};
    };
} // namespace sw

namespace sw
{
    /** @brief 몸 종류 변형 — 같은 칸의 조각을 바꿉니다. */
    struct EquipSetVariantDef
    {
        vector<EquipSetPieceDef> _listPiece{};
        hashed_string            _bodyType{};
    };
} // namespace sw

namespace sw
{
    /** @brief 세트 하나입니다. */
    struct EquipSetDef
    {
        vector<EquipSetPieceDef>   _listPiece{};
        vector<EquipSetVariantDef> _listVariant{};
        vector<hashed_string>      _listCompleteSlot{}; ///< 세트 완성 표현이 대신하는 칸(첫 칸이 그 외형을 가진다)
        hashed_string              _id{};
        hashed_string              _completeVisual{}; ///< 세트 완성 표현 외형 — 비면 없음
    };
} // namespace sw

namespace sw
{
    /**
     * @class EquipSetCatalog
     * @brief `<EquipSetCatalog><Set id="Recon"><Piece slot="Head" items="recon_helmet"/><Variant bodyType="Female"><Piece slot="Body" items="recon_vest_f"/></Variant>
     *        <Complete visual="recon_full" slots="Head,Body"/></Set></EquipSetCatalog>` 입니다.
     * @details 장착 조건(`Equipment`)에는 `IEquipSetLookup` 으로 세트 정보를 빌려 줍니다.
     */
    class SW_GF_API EquipSetCatalog : public IEquipSetLookup
    {
    public:
        [[nodiscard]] bool loadFromNode( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear() { _listSet.clear(); }

        const EquipSetDef*         findSet( const hashed_string& id ) const;
        const vector<EquipSetDef>& getSets() const { return _listSet; }
        /** @brief @p bodyType 의 조각 목록(기본 조각에 몸 변형을 칸별로 덮은 것)입니다. */
        void computePieces( const EquipSetDef& def, const hashed_string& bodyType, vector<EquipSetPieceDef>& outListPiece ) const;

        /**
         * @brief 칸 → 아이템 조회(@p findItemInSlot( slot ) → 아이템 id, 비면 없음)로 조각 수를 셉니다. 외형 해석과 장착 조건이 같은 계산을 씁니다.
         */
        template <typename TFindItem>
        EquipSetProgress computeProgressWith( const EquipSetDef& def, const hashed_string& bodyType, TFindItem&& findItemInSlot ) const
        {
            vector<EquipSetPieceDef> listPiece;
            computePieces( def, bodyType, listPiece );
            EquipSetProgress progress;
            progress._totalCount = static_cast<int32>( listPiece.size() );
            for ( const EquipSetPieceDef& piece : listPiece )
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

        bool             hasSet( const hashed_string& setID ) const override { return findSet( setID ) != nullptr; }
        EquipSetProgress computeProgress( const hashed_string& setID, const vector<EquipSlot>& listSlot, const hashed_string& bodyType ) const override;
        void             collectSetItems( const hashed_string& setID, vector<hashed_string>& outListItemID ) const override;

    private:
        vector<EquipSetDef> _listSet{};
    };
} // namespace sw
