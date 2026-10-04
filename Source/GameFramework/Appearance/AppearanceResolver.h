/**
 * @file AppearanceResolver.h
 * @brief 외형 해석 — 프리셋 · 장비 상태 · 보이는 장비 덮어쓰기 · 꾸미기 값 · 태그 → 실제로 그릴 외형(부품 · 숨김 영역 · 모프 · 머티리얼 값 · 소켓)과 설명입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Appearance/AppearanceTypes.h"
#include "GameFramework/Appearance/ItemVisual.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct CharacterAppearanceSpec;

    class AppearanceDatabase;
    class Equipment;

    /** @brief 해석된 부품 하나 — 스폰(풀) · 부착 · 병합하는 쪽이 그대로 씁니다. */
    struct ResolvedPart
    {
        AppearancePlacement _placement{}; ///< 소켓 부착 · 스프라이트: 소켓 후보(한 이름 공간) + 오프셋 · 회전
        hashed_string       _owner{};     ///< 칸 · 꾸미기 매개변수 — 비면 몸
        hashed_string       _itemId{};    ///< 장비 부품이면 아이템 id
        hashed_string       _visualId{};
        hashed_string       _partName{};
        hashed_string       _variant{};         ///< 고른 메시 변형(비면 기본)
        hashed_string       _materialVariant{}; ///< 고른 머티리얼 변형(비면 기본)
        hashed_string       _asset{};           ///< 변형 · 규칙을 반영한 메시 · 프리팹 · 스프라이트
        hashed_string       _material{};        ///< 변형 · 규칙을 반영한 머티리얼(비면 에셋의 것)
        hashed_string       _skeleton{};        ///< 비면 뿌리 본 하나의 암묵 스켈레톤
        hashed_string       _socketSet{};
        hashed_string       _state{};       ///< 외형 상태
        hashed_string       _damageStage{}; ///< 피해 단계(비면 멀쩡)
        int32               _layer{ 0 };
        AppearancePartKind  _kind{ AppearancePartKind::Skinned };
        uint8               _bDeforms{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 모프 가중치 하나(주인이 비면 몸)입니다. */
    struct ResolvedMorph
    {
        hashed_string _owner{};
        hashed_string _name{};
        float32       _weight{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 머티리얼 값이 가는 곳입니다. */
    enum class ResolvedMaterialTarget : uint8
    {
        Parameter = 0, ///< 머티리얼 매개변수(스칼라는 x)
        DyeChannel,    ///< 마스크 염색 채널
        PaletteSwap    ///< 스프라이트 팔레트 칸
    };

    /** @brief 머티리얼 값 하나입니다. `_part` 가 비면 주인의 모든 부품입니다. */
    struct ResolvedMaterialValue
    {
        float4                 _value{};
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
    /** @brief 꾸미기 부착물 하나(조준경 · 보석 · 데칼)입니다. 소켓은 한 이름 공간의 이름입니다(`MainHand.Rail`). */
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
    /** @brief 소켓 이름 공간의 출처 하나 — 이 부품의 소켓 에셋 이름들이 `<_owner>.` 를 앞에 달고 들어옵니다(주인이 비면 몸 소켓 그대로). */
    struct ResolvedSocketSource
    {
        hashed_string _owner{};
        hashed_string _partName{};
        hashed_string _socketSet{};
    };
} // namespace sw

namespace sw
{
    /** @brief 떨어져 나간 부품 — 부착을 풀고 물리로 넘길 것(소켓 부착 컴포넌트의 ReleasedPhysics)과 충격 힌트입니다. */
    struct ResolvedDetachedPart
    {
        AppearancePlacement _placement{}; ///< 떨어지기 전 자리
        float3              _impulse{};
        hashed_string       _owner{};
        hashed_string       _itemId{};
        hashed_string       _partName{};
        hashed_string       _asset{};
        uint8               _bFromDamageStage{ SW_FALSE }; ///< 피해 단계로 떨어졌다(아니면 맞아서)
    };
} // namespace sw

namespace sw
{
    /** @brief 해석 단계입니다(설명의 묶음). */
    enum class AppearanceTraceStep : uint8
    {
        Customization = 0,
        Occupancy,
        SetComplete,
        Damage,
        Rule,
        Output
    };

    SW_GF_API const utf8* toString( AppearanceTraceStep step );

    /** @brief 설명 한 줄 — 무엇(규칙 id · 세트 id · 칸)이 무엇을 했는가(편집 창의 "왜 이렇게 보이나")입니다. */
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
     * @brief 해석 결과입니다. 순서는 결정적입니다(몸 → 꾸미기 외형 → 슬롯 표 순서의 장비). `_hash` 가 캐시 키입니다.
     * @details `_meshHash` 는 병합 메시에 드는 것(스킨드 부품 · 몸 수정 · 모프 · 머티리얼 · 숨김 영역)만 봅니다 — 외형 상태(뽑음 ↔ 꽂음)나 부착물만 바뀌면
     *          그대로라 병합 메시를 다시 굽지 않아도 됩니다.
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
        hashed_string                    _presetId{};
        hashed_string                    _bodyType{};
        hashed_string                    _bodyShape{};
        hashed_string                    _face{};
        uint64                           _hash{ 0 };
        uint64                           _meshHash{ 0 };

        const ResolvedPart*  findPart( const hashed_string& owner, const hashed_string& partName ) const;
        bool                 hasOwner( const hashed_string& owner ) const;
        bool                 isRegionHidden( const hashed_string& region ) const;
        const ResolvedMorph* findMorph( const hashed_string& owner, const hashed_string& name ) const;
        /** @brief @p source 가 남긴 설명 줄 수입니다(시험 · 편집 창). */
        uint32 countTrace( const hashed_string& source ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AppearanceResolver
     * @brief 순수 함수 — 같은 데이터와 같은 입력이면 같은 결과 · 같은 해시입니다(스레드 · 시간 · 주소를 보지 않는다).
     * @details 정해진 순서로 한 번 돕니다: 펼친 프리셋(입력) → 꾸미기(값 정규화 · 조건) → 칸(형상 변경 · 숨김) → 칸 점유 → 세트 완성 표현 → 피해 단계 →
     *          규칙(점유 · 세트 뒤의 **요청된 목록만** 보고 판정 — 규칙의 결과가 다른 규칙의 조건이 되지 않는다) → 출력. 같은 대상을 여러 규칙이 바꾸면
     *          우선순위가 높은 쪽이 이기고, 진 쪽도 설명에 남습니다.
     */
    struct SW_GF_API AppearanceResolver
    {
        static void resolve( const AppearanceDatabase& database, const CharacterAppearanceSpec& spec, ResolvedAppearance& outResolved );
        /** @brief 결과 전체의 해시입니다(설명은 빼고). */
        static uint64 computeHash( const ResolvedAppearance& resolved );
        /** @brief 병합 메시에 드는 것만의 해시입니다. */
        static uint64 computeMeshHash( const ResolvedAppearance& resolved );
        /** @brief 피해를 전송 정밀도(255 칸)로 맞춥니다. 해석기는 맞춘 값만 씁니다. */
        static float32 snapDamage( float32 damage );
    };
} // namespace sw

namespace sw
{
    /** @struct AppearanceInputUtil
     *  @brief 장비 상태를 해석 입력에 싣습니다. */
    struct SW_GF_API AppearanceInputUtil
    {
        /**
         * @brief @p equipment 의 칸을 @p inoutSpec 에 덮습니다 — 아이템 · 인스턴스 상태(꾸미기 · 피해 · 떨어진 부품) · 숨김. 형상 변경 · 외형 상태는 spec 의 것을 둡니다.
         * @details 피해는 맞은 피해와 닳은 내구도(1 − 내구도 / 최대) 중 큰 쪽입니다. 장비에 없는 칸은 비웁니다.
         */
        static void applyEquipment( const Equipment& equipment, CharacterAppearanceSpec& inoutSpec );
    };
} // namespace sw
