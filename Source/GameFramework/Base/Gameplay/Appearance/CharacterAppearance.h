/**
 * @file CharacterAppearance.h
 * @brief 캐릭터 외형 프리셋(`CharacterAppearance`)입니다. 몸 종류, 체형, 얼굴, 꾸미기 값, 장비 구성(세트, 슬롯별 장비, 보이는 장비 덮어쓰기), 소켓 덮어쓰기를 하나로 묶었습니다.
 * @details 부모 프리셋을 지정하고 다른 값만 적는다. 종족 기본, 직업 복장, 개별 NPC 순으로 상속한다. 값 대신 범위나 후보 목록을 적으면 시드로 뽑는다. 무작위 NPC에 쓴다.
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

    /** @brief 프리셋의 꾸미기 값 하나입니다. 고정 값이거나 범위, 후보 목록입니다. 종류는 스키마가 정합니다. */
    struct CharacterAppearanceValueDef
    {
        vector<hashed_string> _listOption{}; ///< 고르기와 부착의 후보(하나면 고정)
        vector<float4>        _listColor{};  ///< 색 후보(하나면 고정)
        hashed_string         _parameter{};
        float32               _min{ 0.0f }; ///< 슬라이더 값. [min, max] 에서 뽑으며, 같으면 고정이다
        float32               _max{ 0.0f };
        uint8                 _bHasNumber{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 프리셋의 슬롯 하나입니다. */
    struct CharacterAppearanceSlotDef
    {
        CustomizationValueSet _customization{}; ///< 그 아이템 인스턴스의 꾸미기 값
        vector<hashed_string> _listItem{};      ///< 후보(하나면 고정). 비어 있고 `_bClear` 가 켜져 있으면 슬롯을 비운다
        hashed_string         _slot{};
        hashed_string         _visibleVisual{}; ///< 형상 변경
        hashed_string         _state{};
        float32               _damage{ 0.0f };
        uint8                 _bClear{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 프리셋 하나입니다. 파일에 적힌 그대로이며 부모와 합치기 전의 상태입니다. */
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
     * @brief 펼친 프리셋입니다. 후보를 모두 뽑고 부모와 합친 구체적인 값이며, 해석기(`AppearanceResolver`)의 입력입니다.
     * @details `_listSlot` 은 슬롯 테이블의 모든 슬롯을 테이블 순서대로 가진다. 빈 슬롯은 `_itemID` 가 비어 있다.
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
     *        body="body_male" tags="Faction.Army">` 아래의 `<Value>`, `<Equip>`, `<SocketOverride>` 를 읽습니다. 형식은 `Appearance/README.md` 에 있습니다.
     */
    class SW_GF_API CharacterAppearanceCatalog
    {
    public:
        [[nodiscard]] bool loadFromNode( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName );
        /**
         * @brief `<CharacterAppearance>` 원소 하나(프리셋 에셋 파일 `*.appearance.xml` 의 루트)를 읽어 더합니다. 같은 id 가 있으면 바꿉니다(에디터가 다시 내보낸 파일).
         * @details 읽기 규칙은 카탈로그 파일의 원소와 같다. 오류는 @p report 에 남기고 그 프리셋은 더하지 않는다.
         */
        [[nodiscard]] bool loadPresetFromNode( const XMLNode& presetNode, AppearanceLoadReport& report, string_view sourceName );
        /**
         * @brief @p def 를 @p outNode(`<CharacterAppearance>` 원소)에 씁니다 — `loadPresetFromNode` 가 같은 값으로 다시 읽는 형식입니다(읽기와 쓰기가 이 파일 한 자리).
         * @details 슬롯의 아이템 인스턴스 꾸미기 값은 종류(슬라이더 · 색)를 값만으로 알 수 없어 쓰지 않는다 — 그런 슬롯이 있으면 false 와 @p outReason 이다(조용히 버리지 않는다).
         */
        [[nodiscard]] static bool writePresetToNode( const CharacterAppearanceDef& def, const XMLNode& outNode, string& outReason );
        void                      clear() { _listPreset.clear(); }

        const CharacterAppearanceDef*         findPreset( const hashed_string& id ) const;
        const vector<CharacterAppearanceDef>& getPresets() const { return _listPreset; }

        /**
         * @brief 부모 체인을 뿌리부터 합칩니다. 값은 이름별, 슬롯은 슬롯별, 소켓은 이름별로 아래쪽이 덮어씁니다. 세트는 쌓이고 태그는 합쳐집니다.
         * @details 알 수 없는 부모이거나 순환이면 false 이다. 로드 검사가 이런 데이터를 막는다.
         */
        [[nodiscard]] bool mergeChain( const hashed_string& id, CharacterAppearanceDef& outMerged ) const;

        /**
         * @brief 프리셋을 펼칩니다. 후보를 @p seed 로 뽑고(키마다 따로 섞으므로 매개변수를 하나 더해도 다른 값이 바뀌지 않음), 세트를 슬롯에 풀고,
         *        슬롯별 장비를 덮어씁니다. 레이어마다 세트, 슬롯 순서로 처리하므로 아래 레이어의 세트가 위 레이어의 슬롯을 덮습니다. 결정적이어서 같은 데이터와 같은 시드이면 같은 결과가 나옵니다.
         */
        [[nodiscard]] bool expand( const hashed_string& id, uint32 seed, const SlotTable& slotTable, const EquipSetCatalog& sets, const CustomizationSchemaCatalog& schemas,
                                   CharacterAppearanceSpec& outSpec ) const;

        /** @brief 시드와 키로 [0, count) 에서 하나를 뽑습니다. */
        static uint32 pickIndex( uint32 seed, uint64 keyHash, uint32 count );
        /** @brief 시드와 키로 [0, 1] 에서 하나를 뽑습니다. */
        static float32 pickUnit( uint32 seed, uint64 keyHash );

    private:
        /** @brief 부모 체인을 뿌리부터 모읍니다. 알 수 없는 부모이거나 순환이면 false 입니다. */
        bool collectChain( const hashed_string& id, vector<const CharacterAppearanceDef*>& outListChain ) const;

        vector<CharacterAppearanceDef> _listPreset{};
    };
} // namespace sw
