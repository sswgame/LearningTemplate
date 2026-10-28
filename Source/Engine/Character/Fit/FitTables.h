/**
 * @file FitTables.h
 * @brief 장비 피팅의 데이터 테이블입니다(`*.fit.xml`). 겹 테이블, 몸 영역 테이블, 피팅 프로필, 상호작용 테이블을 담습니다. 사람이 고치는 데이터이며 코드에 이름을 박지 않습니다.
 * @details 겹, 영역, 조합을 하나 추가하는 일이 테이블 항목 하나다. 상호작용 테이블은 (안쪽 프로필 × 바깥 프로필)에서 연산 이름과 인자로 가는 매핑이고,
 *          연산은 코드의 이름 붙은 레지스트리(`FitOperatorRegistry`)에서 이름으로 고른다. 모르는 연산, 인자, 프로필은 로드 오류다.
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
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Asset/AssetID.h"

#include <optional>

namespace sw
{
    struct AppearanceGeometry;
    struct CharacterBoneArray;

    class CharacterDataReader;
    class FitOperatorRegistry;
    class XMLNode;

    /** @brief 겹 하나입니다. 순서가 작을수록 안쪽입니다(몸 < 셔츠 < 재킷 < 팔찌). */
    struct SW_API FitLayerDef
    {
        hashed_string _name{};
        int32         _order{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 몸 영역 하나입니다. 본 가중치(`_listBone` 의 가중치 합 ≥ `_minWeight`)나 칠한 정점 그룹(`_group`)으로 정합니다. */
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
     * @brief 피팅 프로필 하나입니다. 단단함은 0(천, 소매)에서 1(팔찌, 갑옷)까지의 연속값이고, 가죽은 중간입니다.
     * @details 조임 세기와 감쇠 폭은 이 프로필이 **안쪽**일 때, 밀어내기 거리는 **바깥**일 때, 덮임 판정 거리는 **안쪽**일 때 쓴다.
     *          상호작용 테이블의 인자가 있으면 인자가 우선한다.
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
    /** @brief 상호작용 테이블의 항목 하나입니다. (안쪽 프로필 × 바깥 프로필)에서 연산 이름과 인자로 가는 매핑이며, 같은 조합에 여러 항목을 둘 수 있습니다(테이블 순서로 실행합니다). */
    struct SW_API FitInteractionDef
    {
        hashed_string       _inner{};
        hashed_string       _outer{};
        hashed_string       _operator{};
        vector<FitArgument> _listArgument{};

        /** @brief 인자 값입니다. 이 상호작용이 적지 않은 인자면 비어 있습니다(연산자가 프로필 값으로 대신한다). */
        [[nodiscard]] std::optional<float32> findArgument( const hashed_string& name ) const;
    };
} // namespace sw

namespace sw
{
    /** @brief 피팅 테이블 모음 하나입니다. */
    class SW_API FitTables
    {
    public:
        /** @brief XML 텍스트에서 읽습니다(기존 내용은 비웁니다). 연산 이름과 인자는 @p operators 에 대조합니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName, const FitOperatorRegistry& operators );
        /** @brief 리소스 파일에서 읽습니다. */
        [[nodiscard]] bool loadFromAsset( const AssetID& assetID, const FitOperatorRegistry& operators );

        /** @brief 겹을 더합니다(같은 이름은 바꿈). */
        void addLayer( const FitLayerDef& layer );
        /** @brief 몸 영역을 더합니다(같은 이름은 바꿈). */
        void addRegion( const BodyRegionDef& region );
        /** @brief 상호작용 줄을 더합니다(끝에). */
        void addInteraction( const FitInteractionDef& interaction );

        /** @brief 이름의 겹입니다. 없으면 nullptr 입니다. */
        const FitLayerDef* findLayer( const hashed_string& name ) const;
        /** @brief 이름의 영역 번호입니다. 없으면 -1 입니다. */
        int32 findRegionIndex( const hashed_string& name ) const;
        /** @brief 이름의 프로필입니다. 없으면 nullptr 입니다. */
        const FitProfileDef* findProfile( const hashed_string& name ) const;
        /** @brief (안쪽, 바깥) 조합의 항목들을 테이블 순서로 채웁니다(비우고 채웁니다). */
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
         * @brief 형상의 정점마다 영역 번호(테이블 순서, 없으면 `kNoGroup`)를 매깁니다. 테이블의 앞 항목이 먼저입니다.
         * @details 본 가중치 영역은 스킨이 있어야 하고 본 이름을 @p bones 에서 찾는다. 정점 그룹 영역은 형상의 칠한 그룹 이름으로 찾는다.
         */
        void assignRegions( const AppearanceGeometry& geometry, const CharacterBoneArray& bones, vector<uint16>& outListVertexRegion ) const;

    private:
        void readRoot( const XMLNode& root, const FitOperatorRegistry& operators, CharacterDataReader& reader );

    private:
        vector<FitLayerDef>       _listLayer;
        vector<BodyRegionDef>     _listRegion;
        vector<FitProfileDef>     _listProfile;
        vector<FitInteractionDef> _listInteraction;
    };
} // namespace sw
