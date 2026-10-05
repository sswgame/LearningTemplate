/**
 * @file FitTables.h
 * @brief 장비 피팅의 데이터 표 — 겹 표 · 몸 영역 표 · 피팅 프로필 · 상호작용 표(`*.fit.xml`)입니다. 사람이 고치는 데이터이고 코드에 이름을 박지 않습니다.
 * @details 겹 하나 · 영역 하나 · 조합 하나를 더하는 것이 표 한 줄입니다. 상호작용 표는 (안쪽 프로필 × 바깥 프로필) → 연산 이름 + 인자이고,
 *          연산은 코드의 이름 붙은 등록부(`FitOperatorRegistry`)에서 이름으로 고릅니다 — 모르는 연산 · 모르는 인자 · 모르는 프로필은 로드 오류입니다.
 *
 *          XML 예(엔진 기본: `engine/character/default.fit.xml`):
 *          @code
 *          <FitTables>
 *            <Layer name="Body" order="0"/>
 *            <Layer name="Shirt" order="10"/>
 *            <Region name="Forearm_L" bones="lowerarm_l" minWeight="0.5"/>
 *            <Region name="Belly" group="Belly"/>
 *            <Profile name="Cloth" rigidity="0" shrinkStrength="1" pushDistance="0.004" falloffWidth="0.05" coverageDistance="0.02"/>
 *            <Profile name="Rigid" rigidity="1"/>
 *            <Interaction inner="Cloth" outer="Rigid" operator="Shrink" args="margin=0.002"/>
 *            <Interaction inner="Cloth" outer="Cloth" operator="Cut"/>
 *          </FitTables>
 *          @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    struct AppearanceGeometry;
    struct CharacterBoneArray;

    class CharacterDataReader;
    class FitOperatorRegistry;
    class XmlNode;

    /** @brief 겹 하나입니다. 순서가 작을수록 안쪽입니다(몸 < 셔츠 < 재킷 < 팔찌). */
    struct SW_API FitLayerDef
    {
        hashed_string _name{};
        int32         _order{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 몸 영역 하나 — 본 가중치(`_listBone` 의 가중치 합 ≥ `_minWeight`) 또는 칠한 정점 그룹(`_group`)으로 정합니다. */
    struct SW_API BodyRegionDef
    {
        hashed_string         _name{};
        hashed_string         _group{};
        vector<hashed_string> _listBone{};
        float32               _minWeight{ 0.5f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 피팅 프로필 하나입니다. 단단함은 0(천 · 소매) ~ 1(팔찌 · 갑옷)의 연속값이고, 가죽은 중간입니다.
     * @details 조임 세기 · 감쇠 폭은 이 프로필이 **안쪽**일 때, 밀어내기 거리는 **바깥**일 때, 덮임 판정 거리는 **안쪽**일 때 씁니다.
     *          상호작용 표의 인자가 있으면 인자가 이깁니다.
     */
    struct SW_API FitProfileDef
    {
        hashed_string _name{};
        float32       _rigidity{ 0.0f };
        float32       _shrinkStrength{ 1.0f };
        float32       _pushDistance{ 0.004f };
        float32       _falloffWidth{ 0.05f };
        float32       _coverageDistance{ 0.02f };
    };
} // namespace sw

namespace sw
{
    /** @brief 연산 인자 하나(이름 = 수)입니다. */
    struct SW_API FitArgument
    {
        hashed_string _name{};
        float32       _value{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 상호작용 표의 한 줄 — (안쪽 프로필 × 바깥 프로필) → 연산 이름 + 인자입니다. 같은 조합에 여러 줄을 둘 수 있습니다(표 순서로 돕니다). */
    struct SW_API FitInteractionDef
    {
        hashed_string       _inner{};
        hashed_string       _outer{};
        hashed_string       _operator{};
        vector<FitArgument> _listArgument{};

        /** @brief 인자 값입니다. 없으면 @p fallback 입니다. */
        float32 findArgument( const hashed_string& name, float32 fallback ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 피팅 표 묶음 하나입니다. */
    class SW_API FitTables
    {
    public:
        /** @brief XML 텍스트에서 읽습니다(비우고). 연산 이름 · 인자는 @p operators 에 대조합니다. */
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName, const FitOperatorRegistry& operators );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromResource( string_view path, const FitOperatorRegistry& operators );

        /** @brief 겹을 더합니다(같은 이름은 바꿈). */
        void addLayer( const FitLayerDef& layer );
        /** @brief 몸 영역을 더합니다(같은 이름은 바꿈). */
        void addRegion( const BodyRegionDef& region );
        /** @brief 프로필을 더합니다(같은 이름은 바꿈). */
        void addProfile( const FitProfileDef& profile );
        /** @brief 상호작용 줄을 더합니다(끝에). */
        void addInteraction( const FitInteractionDef& interaction );

        /** @brief 이름의 겹입니다. 없으면 nullptr 입니다. */
        const FitLayerDef* findLayer( const hashed_string& name ) const;
        /** @brief 이름의 영역 번호입니다. 없으면 -1 입니다. */
        int32 findRegionIndex( const hashed_string& name ) const;
        /** @brief 이름의 프로필입니다. 없으면 nullptr 입니다. */
        const FitProfileDef* findProfile( const hashed_string& name ) const;
        /** @brief (안쪽, 바깥) 조합의 줄들을 표 순서로 채웁니다(비우고 채움). */
        void collectInteractions( const hashed_string& innerProfile, const hashed_string& outerProfile, vector<const FitInteractionDef*>& outListInteraction ) const;

        /** @brief 겹들입니다. */
        const vector<FitLayerDef>& getLayers() const { return _listLayer; }
        /** @brief 영역들입니다. */
        const vector<BodyRegionDef>& getRegions() const { return _listRegion; }
        /** @brief 프로필들입니다. */
        const vector<FitProfileDef>& getProfiles() const { return _listProfile; }
        /** @brief 상호작용 줄들입니다. */
        const vector<FitInteractionDef>& getInteractions() const { return _listInteraction; }

        /**
         * @brief 형상의 정점마다 영역 번호(표 순서, 없으면 `kNoGroup`)를 매깁니다. 표의 앞 줄이 먼저입니다.
         * @details 본 가중치 영역은 스킨이 있어야 하고 본 이름을 @p bones 에서 찾습니다. 정점 그룹 영역은 형상의 칠한 그룹 이름으로 찾습니다.
         */
        void assignRegions( const AppearanceGeometry& geometry, const CharacterBoneArray& bones, vector<uint16>& outListVertexRegion ) const;

    private:
        void readRoot( const XmlNode& root, const FitOperatorRegistry& operators, CharacterDataReader& reader );

    private:
        vector<FitLayerDef>       _listLayer;
        vector<BodyRegionDef>     _listRegion;
        vector<FitProfileDef>     _listProfile;
        vector<FitInteractionDef> _listInteraction;
    };
} // namespace sw
