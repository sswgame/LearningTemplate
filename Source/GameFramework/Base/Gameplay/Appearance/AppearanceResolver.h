/**
 * @file AppearanceResolver.h
 * @brief 외형 해석입니다. 프리셋, 장비 상태, 보이는 장비 덮어쓰기, 꾸미기 값, 태그를 입력으로 받아 실제로 그릴 외형(부품, 숨김 영역, 모프, 머티리얼 값, 소켓)과 그 설명을 만듭니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Base/Gameplay/Appearance/ItemVisual.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct CharacterAppearanceSpec;

    class AppearanceDatabase;
    class Equipment;

    /** @brief 해석된 부품 하나입니다. 스폰(풀), 부착, 병합하는 쪽이 이 값을 그대로 씁니다. */
    struct ResolvedPart
    {
        AppearancePlacement _placement{}; ///< 소켓 부착과 스프라이트에 쓰는 배치 정보. 소켓 후보(한 이름 공간), 오프셋, 회전을 담는다
        hashed_string       _owner{};     ///< 슬롯 또는 꾸미기 매개변수. 비어 있으면 몸
        hashed_string       _itemID{};    ///< 장비 부품이면 아이템 id
        hashed_string       _visualID{};
        hashed_string       _partName{};
        hashed_string       _variant{};         ///< 고른 메시 변형(비어 있으면 기본)
        hashed_string       _materialVariant{}; ///< 고른 머티리얼 변형(비어 있으면 기본)
        hashed_string       _asset{};           ///< 변형과 규칙을 반영한 메시, 프리팹, 스프라이트
        hashed_string       _material{};        ///< 변형과 규칙을 반영한 머티리얼(비어 있으면 에셋의 것)
        hashed_string       _skeleton{};        ///< 비어 있으면 루트 본 하나짜리 암묵 스켈레톤
        hashed_string       _socketSet{};
        hashed_string       _rig{};         ///< 스킨드 부품의 자기 후처리 리그(있으면 몸을 리더로 따르지 않는다)
        hashed_string       _fit{};         ///< 피팅 데이터(`*.partfit.xml`). 비어 있으면 피팅하지 않는다
        hashed_string       _fitTables{};   ///< 피팅 표(`*.fit.xml`). 몸 부품만 가진다
        hashed_string       _bodyShapes{};  ///< 체형 축 데이터(`*.bodyshape.xml`). 몸 부품만 가진다
        hashed_string       _state{};       ///< 외형 상태
        hashed_string       _damageStage{}; ///< 피해 단계(비어 있으면 멀쩡함)
        int32               _layer{ 0 };
        AppearancePartKind  _kind{ AppearancePartKind::Skinned };
        uint8               _bDeforms{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 모프 가중치 하나입니다. 주인이 비어 있으면 몸입니다. */
    struct ResolvedMorph
    {
        hashed_string _owner{};
        hashed_string _name{};
        float32       _weight{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 머티리얼 값이 적용되는 대상입니다. */
    enum class ResolvedMaterialTarget : uint8
    {
        Parameter = 0, ///< 머티리얼 매개변수(스칼라이면 x 만 쓴다)
        DyeChannel,    ///< 마스크 염색 채널
        PaletteSwap    ///< 스프라이트 팔레트 슬롯
    };

    /** @brief 머티리얼 매개변수에 쓰는 값의 모양입니다. 값을 만든 쪽이 정하며, 적용은 이 모양 그대로 쓴다(float4 글로 바꿔 스칼라에 쓰지 않는다). */
    enum class ResolvedMaterialShape : uint8
    {
        Scalar = 0, ///< float 하나(`_value._x`). 머티리얼의 Float · Range 매개변수에 쓴다.
        Vector      ///< float4. 머티리얼의 Float2 · Float3 · Float4 · Color 매개변수에 쓴다.
    };

    /** @brief 머티리얼 값 하나입니다. `_part` 가 비어 있으면 주인의 모든 부품에 적용합니다. */
    struct ResolvedMaterialValue
    {
        float4                 _value{};
        ResolvedMaterialShape  _shape{ ResolvedMaterialShape::Vector };
        hashed_string          _owner{};
        hashed_string          _part{};
        hashed_string          _name{};
        int32                  _channel{ 0 };
        ResolvedMaterialTarget _target{ ResolvedMaterialTarget::Parameter };
    };
} // namespace sw

namespace sw
{
    /** @brief 본 비율 보정 값 하나(`BoneProportion` 대상 이름)입니다. */
    struct ResolvedBoneProportion
    {
        hashed_string _owner{};
        hashed_string _name{};
        float32       _value{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 꾸미기 부착물 하나(조준경, 보석, 데칼)입니다. 소켓은 한 이름 공간 안의 이름입니다(`MainHand.Rail`). */
    struct ResolvedAttachment
    {
        AppearancePlacement _placement{};
        hashed_string       _owner{};
        hashed_string       _parameter{};
        hashed_string       _option{};
        hashed_string       _asset{};
    };
} // namespace sw

namespace sw
{
    /** @brief 소켓 이름 공간의 출처 하나입니다. 이 부품의 소켓 에셋 이름 앞에 `<_owner>.` 를 붙여서 이름 공간에 넣습니다. 주인이 비어 있으면 몸 소켓 이름을 그대로 씁니다. */
    struct ResolvedSocketSource
    {
        hashed_string _owner{};
        hashed_string _partName{};
        hashed_string _socketSet{};
    };
} // namespace sw

namespace sw
{
    /** @brief 떨어져 나간 부품입니다. 부착을 해제하고 물리로 넘길 대상(소켓 부착 컴포넌트의 ReleasedPhysics)과 충격 힌트를 담습니다. */
    struct ResolvedDetachedPart
    {
        AppearancePlacement _placement{}; ///< 떨어지기 전의 배치
        float3              _impulse{};
        hashed_string       _owner{};
        hashed_string       _itemID{};
        hashed_string       _partName{};
        hashed_string       _asset{};
        uint8               _bFromDamageStage{ SW_FALSE }; ///< 피해 단계 때문에 떨어졌는지 여부(아니면 맞아서 떨어진 것)
    };
} // namespace sw

namespace sw
{
    /** @brief 해석 단계입니다. 설명을 단계별로 분류하는 데 씁니다. */
    ENUM()
    enum class AppearanceTraceStep : uint8
    {
        Customization = 0,
        Occupancy,
        SetComplete,
        Damage,
        Rule,
        Output
    };

    /** @brief 설명 한 항목입니다. 무엇(규칙 id, 세트 id, 슬롯)이 무엇을 했는지 담으며, 편집 창의 "왜 이렇게 보이나" 에 씁니다. */
    struct AppearanceTraceEntry
    {
        string              _message{};
        hashed_string       _source{};
        AppearanceTraceStep _step{ AppearanceTraceStep::Output };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 해석 결과입니다. 순서는 결정적이며 몸, 꾸미기 외형, 슬롯 테이블 순서의 장비 순입니다. `_hash` 가 캐시 키입니다.
     * @details `_meshHash` 는 병합 메시에 들어가는 것(스킨드 부품, 몸 수정, 모프, 머티리얼, 숨김 영역)만 본다. 외형 상태(뽑음, 꽂음)나 부착물만 바뀌면
     *          해시가 그대로이므로 병합 메시를 다시 베이크하지 않아도 된다.
     */
    struct SW_GF_API ResolvedAppearance
    {
        vector<ResolvedPart>             _listPart{};
        vector<ResolvedMorph>            _listMorph{};
        vector<ResolvedMaterialValue>    _listMaterialValue{};
        vector<ResolvedBoneProportion>   _listBoneProportion{};
        vector<ResolvedAttachment>       _listAttachment{};
        vector<ResolvedSocketSource>     _listSocketSource{};
        vector<AppearanceSocketOverride> _listSocketOverride{};
        vector<ResolvedDetachedPart>     _listDetachedPart{};
        vector<hashed_string>            _listHiddenRegion{};
        vector<AppearanceTraceEntry>     _listTrace{};
        hashed_string                    _presetID{};
        hashed_string                    _bodyType{};
        hashed_string                    _bodyShape{};
        hashed_string                    _face{};
        uint64                           _hash{ 0 };
        uint64                           _meshHash{ 0 };

        const ResolvedPart*  findPart( const hashed_string& owner, const hashed_string& partName ) const;
        bool                 hasOwner( const hashed_string& owner ) const;
        bool                 isRegionHidden( const hashed_string& region ) const;
        const ResolvedMorph* findMorph( const hashed_string& owner, const hashed_string& name ) const;
        /** @brief @p source 가 남긴 설명 항목 수입니다. 테스트와 편집 창에 씁니다. */
        uint32 countTrace( const hashed_string& source ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AppearanceResolver
     * @brief 순수 함수입니다. 같은 데이터와 같은 입력이면 같은 결과와 같은 해시가 나옵니다. 스레드, 시간, 주소는 결과에 영향을 주지 않습니다.
     * @details 정해진 순서로 한 번 돈다. 펼친 프리셋(입력), 꾸미기(값 정규화와 조건), 슬롯(형상 변경과 숨김), 슬롯 점유, 세트 완성 표현, 피해 단계,
     *          규칙, 출력 순서이다. 규칙은 점유와 세트 처리 뒤의 **요청된 목록만** 보고 판정하므로, 규칙의 결과가 다른 규칙의 조건이 되지 않는다.
     *          같은 대상을 여러 규칙이 바꾸면 우선순위가 높은 쪽이 적용되고, 적용되지 못한 규칙도 설명에 남는다.
     */
    struct SW_GF_API AppearanceResolver
    {
        static void resolve( const AppearanceDatabase& database, const CharacterAppearanceSpec& spec, ResolvedAppearance& outResolved );
        /** @brief 결과 전체의 해시입니다. 설명은 포함하지 않습니다. */
        static uint64 computeHash( const ResolvedAppearance& resolved );
        /** @brief 병합 메시에 들어가는 것만으로 계산한 해시입니다. */
        static uint64 computeMeshHash( const ResolvedAppearance& resolved );
        /** @brief 피해를 전송 정밀도(255 단계)로 맞춥니다. 해석기는 맞춘 값만 씁니다. */
        static float32 snapDamage( float32 damage );
    };
} // namespace sw

namespace sw
{
    /** @struct AppearanceInputUtil
     *  @brief 장비 상태를 해석 입력에 반영합니다. */
    struct SW_GF_API AppearanceInputUtil
    {
        /**
         * @brief @p equipment 의 슬롯을 @p inoutSpec 에 덮어씁니다. 아이템, 인스턴스 상태(꾸미기, 피해, 떨어진 부품), 숨김을 반영하고, 형상 변경과 외형 상태는 spec 의 것을 그대로 둡니다.
         * @details 피해는 맞은 피해와 닳은 내구도(1 - 내구도 / 최대) 중 큰 쪽이다. 장비에 없는 슬롯은 비운다.
         */
        static void applyEquipment( const Equipment& equipment, CharacterAppearanceSpec& inoutSpec );
    };
} // namespace sw
