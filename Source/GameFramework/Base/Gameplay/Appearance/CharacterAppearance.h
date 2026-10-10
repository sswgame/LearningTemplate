/**
 * @file CharacterAppearance.h
 * @brief 캐릭터 외형 프리셋(`CharacterAppearance`) — 몸 종류 · 체형 · 얼굴 · 꾸미기 값 · 장비 구성(세트 + 칸별 장비 + 보이는 장비 덮어쓰기) · 소켓 덮어쓰기를 묶은 것입니다.
 * @details 부모 프리셋 + 다른 값만 적습니다(종족 기본 → 직업 복장 → 개별 NPC). 값 대신 범위 · 후보 목록을 적으면 씨앗으로 뽑습니다(무작위 NPC).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/TagSystem.h"

#include "GameFramework/Base/Foundation/Data/CustomizationValueSet.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class CustomizationSchemaCatalog;
    class EquipSetCatalog;
    class SlotTable;
    class XMLNode;

    /** @brief 프리셋의 꾸미기 값 하나 — 고정 값 또는 범위 · 후보 목록입니다. 종류는 스키마가 정합니다. */
    struct CharacterAppearanceValueDef
    {
        vector<hashed_string> _listOption{}; ///< 고르기 · 부착 후보(하나면 고정)
        vector<float4>        _listColor{};  ///< 색 후보(하나면 고정)
        hashed_string         _parameter{};
        float32               _min{ 0.0f }; ///< 슬라이더: [min, max] 에서 뽑는다(같으면 고정)
        float32               _max{ 0.0f };
        uint8                 _bHasNumber{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 프리셋의 칸 하나입니다. */
    struct CharacterAppearanceSlotDef
    {
        CustomizationValueSet _customization{}; ///< 그 아이템 인스턴스의 꾸미기 값
        vector<hashed_string> _listItem{};      ///< 후보(하나면 고정, 비고 `_bClear` 면 칸을 비운다)
        hashed_string         _slot{};
        hashed_string         _visibleVisual{}; ///< 형상 변경
        hashed_string         _state{};
        float32               _damage{ 0.0f };
        uint8                 _bClear{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 프리셋 하나(파일에 적힌 그대로 — 부모와 합치기 전)입니다. */
    struct CharacterAppearanceDef
    {
        vector<CharacterAppearanceValueDef> _listValue{};
        vector<CharacterAppearanceSlotDef>  _listSlot{};
        vector<AppearanceSocketOverride>    _listSocketOverride{};
        vector<hashed_string>               _listSet{};
        vector<hashed_string>               _listBodyType{};  ///< 후보
        vector<hashed_string>               _listBodyShape{}; ///< 후보
        vector<hashed_string>               _listFace{};      ///< 후보
        vector<hashed_string>               _listBody{};      ///< 몸 외형(ItemVisual id) 후보
        TagContainer                        _tags{};
        hashed_string                       _id{};
        hashed_string                       _parent{};
        hashed_string                       _schema{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 펼친 프리셋 — 후보를 모두 뽑고 부모와 합친 구체 값입니다. 해석기(`AppearanceResolver`)의 입력입니다.
     * @details `_listSlot` 은 슬롯 표의 모든 칸을 표 순서로 가집니다(빈 칸은 `_itemID` 가 빈 것).
     */
    struct SW_GF_API CharacterAppearanceSpec
    {
        CustomizationValueSet            _customization{};
        vector<AppearanceSlotRequest>    _listSlot{};
        vector<AppearanceSocketOverride> _listSocketOverride{};
        TagContainer                     _tags{};
        hashed_string                    _presetID{};
        hashed_string                    _schema{};
        hashed_string                    _bodyType{};
        hashed_string                    _bodyShape{};
        hashed_string                    _face{};
        hashed_string                    _bodyVisual{};
        uint32                           _seed{ 0 };

        AppearanceSlotRequest*       findSlot( const hashed_string& slot );
        const AppearanceSlotRequest* findSlot( const hashed_string& slot ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CharacterAppearanceCatalog
     * @brief `<CharacterAppearanceCatalog><CharacterAppearance id="Soldier" parent="HumanBase" schema="Human" bodyType="Male" bodyShape="Average,Heavy" face="face_a"
     *        body="body_male" tags="Faction.Army">` 아래 `<Value>` · `<Equip>` · `<SocketOverride>` 를 읽습니다(형식은 `Appearance/README.md`).
     */
    class SW_GF_API CharacterAppearanceCatalog
    {
    public:
        [[nodiscard]] bool loadFromNode( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName );
        void               clear() { _listPreset.clear(); }

        const CharacterAppearanceDef*         findPreset( const hashed_string& id ) const;
        const vector<CharacterAppearanceDef>& getPresets() const { return _listPreset; }

        /**
         * @brief 부모 사슬을 뿌리부터 합칩니다(값은 이름별, 칸은 칸별, 소켓은 이름별로 아래가 덮는다. 세트는 쌓이고 태그는 합쳐진다).
         * @details 모르는 부모 · 순환이면 false 입니다(로드 검사가 막는다).
         */
        [[nodiscard]] bool mergeChain( const hashed_string& id, CharacterAppearanceDef& outMerged ) const;

        /**
         * @brief 프리셋을 펼칩니다 — 후보를 @p seed 로 뽑고(키마다 따로 섞어 매개변수를 하나 더해도 다른 값이 바뀌지 않는다), 세트를 칸에 풀고,
         *        칸별 장비를 덮습니다(층마다 세트 → 칸 순서라 아래 층의 세트가 위 층의 칸을 덮는다). 결정적입니다 — 같은 데이터 · 같은 씨앗이면 같은 결과입니다.
         */
        [[nodiscard]] bool expand( const hashed_string& id, uint32 seed, const SlotTable& slotTable, const EquipSetCatalog& sets, const CustomizationSchemaCatalog& schemas,
                                   CharacterAppearanceSpec& outSpec ) const;

        /** @brief 씨앗과 키로 [0, count) 하나를 뽑습니다. */
        static uint32 pickIndex( uint32 seed, uint64 keyHash, uint32 count );
        /** @brief 씨앗과 키로 [0, 1] 하나를 뽑습니다. */
        static float32 pickUnit( uint32 seed, uint64 keyHash );

    private:
        /** @brief 부모 사슬을 뿌리부터 모읍니다. 모르는 부모 · 순환이면 false 입니다. */
        bool collectChain( const hashed_string& id, vector<const CharacterAppearanceDef*>& outListChain ) const;

        vector<CharacterAppearanceDef> _listPreset{};
    };
} // namespace sw
