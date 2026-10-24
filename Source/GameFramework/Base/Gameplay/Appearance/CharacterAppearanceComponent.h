/**
 * @file CharacterAppearanceComponent.h
 * @brief 외형 컴포넌트입니다. 외형 프리셋(`CharacterAppearance`)을 펼쳐 해석하고, 그 결과를 오브젝트로 조립합니다. 몸 메시, 장비 부품, 소켓 부착, 염색을 처리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Fit/SurfaceState.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/SceneAnimation/AnimationSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Resource/Cache/SharedAssetCache.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceSocketRig.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterAppearanceState.h"
#include "GameFramework/Base/Gameplay/Appearance/CharacterSurfaceBinding.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct CharacterAppearanceSpec;
    struct FitResult;

    class AppearanceDatabase;
    class CharacterAppearanceComponent;
    class Equipment;
    class FitPartData;
    class FitTables;
    class GameObjectManager;
    class MeshSectionComponent;
    class SkeletalMeshComponent;

    /**
     * @class CharacterAppearanceSocketTask
     * @brief 외형 컴포넌트가 몸 유닛에 등록하는 단계 작업입니다. 몸의 포즈가 끝난 프레임에 부품의 소켓 위치를 고칩니다. 게임 스레드에서 `finishAnimationFrame` 이 부릅니다.
     * @details 리플렉션 컴포넌트는 기반 클래스를 하나만 둔다(`castTo` 가 단일 상속을 가정한다). 그래서 인터페이스는 이 작은 객체가 구현한다. 이 작업은 할 일이 있다고
     *          알리지 않으므로, 몸이 쉬고 있으면(재생 없음) 이 작업도 실행되지 않는다. 본이 움직이지 않으면 소켓도 그대로이기 때문이다.
     */
    class SW_GF_API CharacterAppearanceSocketTask final : public IAnimationPhaseTask
    {
    public:
        explicit CharacterAppearanceSocketTask( CharacterAppearanceComponent& owner );

        bool isAnimationActive() const override { return false; }
        /** @brief 몸의 모델 공간을 읽어 소켓 위치만 고친다 — 군중 묶음을 나누는 몸이면 묶음의 포즈를 읽는다. */
        AnimationPoseAccess getPoseAccess() const override { return AnimationPoseAccess::ReadsOnly; }
        void                runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override;
        void                finishAnimationFrame( SkeletalMeshComponent& unit ) override;
        void                onAnimationUnitDetached( SkeletalMeshComponent& unit ) override;

    private:
        CharacterAppearanceComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CharacterAppearanceProportionTask
     * @brief 외형 컴포넌트가 몸 유닛에 등록하는 두 번째 단계 작업입니다 — 체형 축이 정한 본 비율(`BoneProportion`)을 몸의 애니메이션 포즈 위에 겹칩니다(가산 층).
     * @details `Attachment` 단계에 한다 — 애니메이터(`BasePose`)가 포즈를 만든 뒤, 후처리 리그(`PostProcess`)가 돌기 전이라 리그가 비율이 반영된 포즈를 본다.
     *          포즈를 쓰는 작업이라(`Writes`) 몸의 군중 포즈 공유가 꺼진다(유닛마다 비율이 달라 같은 포즈를 나눌 수 없다). 비율이 없으면(`BoneProportion` 비었음) 아무것도 하지 않는다.
     */
    class SW_GF_API CharacterAppearanceProportionTask final : public IAnimationPhaseTask
    {
    public:
        explicit CharacterAppearanceProportionTask( CharacterAppearanceComponent& owner );

        bool                isAnimationActive() const override { return false; }
        AnimationPoseAccess getPoseAccess() const override { return AnimationPoseAccess::Writes; }
        void                runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override;
        void                onAnimationUnitDetached( SkeletalMeshComponent& unit ) override;

    private:
        CharacterAppearanceComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CharacterAppearanceComponent
     * @brief 몸 유닛(같은 오브젝트의 `SkeletalMeshComponent`)에 외형 프리셋을 입힙니다. 외형 데이터는 게임 서비스 `AppearanceDatabase` 에서 가져옵니다.
     * @details - **조립**: 프리셋을 시드로 펼치고(`CharacterAppearanceCatalog::expand`), 슬롯 덮어쓰기(`setSlotItem`, 무기 바꾸기에 씀)를 적용해 해석한다.
     *            몸 부품(주인이 비어 있는 첫 스킨드 부품)은 이 오브젝트의 유닛에 메시, 스켈레톤, 머티리얼로 적용한다. 다른 스킨드 부품은 몸을 리더로 따르는 자식 유닛으로 만든다.
     *            자기 리그(`rig`)를 적은 스킨드 부품은 리더를 두지 않고 자기 스켈레톤을 그 리그가 움직인다(스프링 망토, 리그 유닛 `Body` = 몸).
     *          - **리그 유닛**: 같은 오브젝트의 후처리 리그에 소켓 에셋을 가진 소켓 부품을 칸 이름의 강체 유닛으로 잇는다(왼손 IK 가 `MainHand` 의 `SupportHand` 를 따른다).
     *          - **부품 풀**: 바꾼 소켓 부품과 `destroyOwnerStowingParts` 로 거둔 소켓 부품은 씬의 `SocketPartPool` 에 쉬게 하고, 다음 스폰이 같은 에셋을 꺼낸다.
     *            소켓 부품(`SocketPrefab`)은 프리팹을 스폰해서 `SocketBindingComponent` 로 소켓에 붙인다. 다시 조립할 때 같은 부품(주인, 이름, 에셋)은 그대로 둔다.
     *          - **소켓**: 몸과 부품의 소켓 에셋이 하나의 이름 공간을 이룬다(`AppearanceSocketRig`, 예: `MainHand.Muzzle`). 몸의 포즈가 끝난 프레임마다(단계 작업) 부품의
     *            소켓 위치를 고친다. `findSocketWorldTransform` 은 지난 프레임의 포즈와 월드로 답하며, 틱 안에서는 읽기만 한다.
     *          - **체형 · 피팅**: 해석의 모프(체형)와, 몸 부품에 피팅 표(`fitTables`)가 있으면 피팅 데이터(`fit`)를 가진 스킨드 부품의 피팅을 베이크한 메시 사본으로 그린다
     *            (`AppearanceMeshAssembly` — 덮인 삼각형 · 숨김 영역을 빼고 델타와 모프를 레스트에 베이크한다). 원본 메시 · 데이터는 비동기로 읽고, 다 읽힌 틱 뒤에 유닛의
     *            스킨 원본을 바꾼다(`SkeletalMeshComponent::setSkinSourceMesh`). 그동안 피팅하는 부품은 숨긴다. 원본 메시 · 표 · 피팅 데이터가 다시 읽히면 다시 조립한다.
     *          - **병합**(`_bMergeParts`): 켜면 몸을 따르는 스킨드 부품(자기 리그 · 다른 스켈레톤이 아닌 것)은 오브젝트를 따로 두지 않는다. 몸과 그 부품들의 메시(베이크한 사본이거나 원본)를
     *            구간이 있는 메시 하나로 합쳐(`CharacterMeshUtil::mergeMeshes`) 몸 유닛이 한 번에 스키닝하고, 부품마다 몸 오브젝트의 구간 컴포넌트(`MeshSectionComponent`)가 자기 머티리얼로 그린다.
     *            병합한 메시가 준비되기 전에는 몸 원본만 보인다. 합친 메시는 낮은 상세 단계(LOD)를 갖지 못한다(구간과 LOD 는 함께 쓰지 못한다 — LOD 0 만 가진다).
     *          - **염색**: 해석 결과의 머티리얼 값(`Parameter`, 꾸미기의 `MaterialColor` 와 `MaterialScalar`)을 주인의 부품마다 머티리얼 인스턴스에 적용한다.
     *          - **장비**: `setEquipment` 로 빌린 장비(`Equipment`)의 슬롯이 프리셋의 장비 구성을 덮는다. 해석은 외형 상태(`CharacterAppearanceState`)가 하고,
     *            장비 · 데이터 · 입력의 버전이 그대로이면 다시 해석하지 않는다. 지난 해석에 붙어 있다가 떨어져 나간 소켓 부품은 지우지 않고 물리로 뗀다
     *            (`SocketBindingComponent::release( Physics )`, 해석의 충격 힌트가 시작 속도). 물리 바디가 없는 부품은 알리고 지운다.
     *          - **틱 안 호출**: 구조를 바꾸는 일(스폰, 부착)은 틱 뒤로 미룬다. 그래서 바꾸는 함수는 언제 불러도 된다.
     *          - 스폰한 부품은 현재 상태의 결과일 뿐이므로, 상태를 저장하기 전에 `despawnParts` 로 정리한다. 다시 시작하면 다시 조립한다.
     */
    REFLECT( Category = "Character", DisplayName = "Character Appearance", Tooltip = "Assembles an appearance preset: body mesh, equipment on sockets and dye" )
    class SW_GF_API CharacterAppearanceComponent : public Component
    {
        friend class CharacterAppearanceSocketTask;
        friend class CharacterAppearanceProportionTask;

    public:
        REFLECT_BODY();

        /** @brief 자기 리그를 가진 스킨드 부품(`ItemVisual` 부품의 `rig`)에 몸을 이어 주는 리그 유닛 이름입니다 — 리그 데이터가 `"unit": "Body"` 로 몸 뼈를 가리킨다. */
        static constexpr const utf8* kBodyRigUnit = "Body";

        CharacterAppearanceComponent();
        virtual ~CharacterAppearanceComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;
        /**
         * @brief 베이크한 메시의 재료가 다 읽혔으면 틱 뒤에 겁니다. 조립에 쓴 에셋(소켓 · 원본 메시 · 피팅 표 · 피팅 데이터)을 에셋 매니저가 다시 읽었으면 다시 조립을 예약합니다(에셋 핫 리로드).
         */
        void onTick( float32 deltaTime ) override;

        /** @brief 프리셋과 시드를 바꾸고 다시 조립합니다. 슬롯 덮어쓰기는 그대로 둡니다. */
        void setPreset( const hashed_string& presetID, uint32 seed );
        /** @brief 지금 입히려는 프리셋 id 입니다(`setPreset` 이나 프로퍼티가 정한 값 — 조립이 끝났는지와 상관없다). */
        const string& getPresetID() const { return _presetID; }
        /** @brief 프리셋의 무작위 선택 시드입니다. */
        int32 getSeed() const { return _seed; }
        /** @brief 슬롯의 아이템을 덮어쓰고 다시 조립합니다. 빈 아이템이면 슬롯을 비웁니다. 같은 값이면 아무것도 하지 않습니다. */
        void setSlotItem( const hashed_string& slot, const hashed_string& itemID );
        /**
         * @brief 미리보기 입히기 — 프리셋 id 대신 외형 선택 @p selection 을 데이터 @p database 로 조립합니다. 게임 서비스가 없는 씬(에디터 미리보기 창)이 씁니다.
         * @details 시작(`onBeginPlay`)을 기다리지 않고 조립을 예약한다 — 미리보기 씬은 플레이를 돌리지 않는다. 한 번 부르면 이 컴포넌트는 프리셋 id 와
         *          게임 서비스 `AppearanceDatabase` 를 보지 않고 이 선택 · 데이터로만 조립한다(두 길이 섞이지 않는다). @p database 는 빌린 것이라 컴포넌트보다 오래 살아야 한다.
         *          선택을 펼치며 버리거나 되돌린 것(`AppearanceSelectionReport`)은 경고로 남긴다.
         */
        void previewSelection( const AppearanceSelection& selection, const AppearanceDatabase& database );
        /**
         * @brief 장비를 빌립니다. 그 슬롯이 프리셋의 장비 구성을 덮어씁니다(슬롯 덮어쓰기 `setSlotItem` 은 그 위에 다시 덮는다). 다시 조립을 예약합니다.
         * @details 소유하지 않습니다 — 장비가 사라지기 전에 nullptr 로 부릅니다. 끼기 · 벗기 뒤에 같은 포인터로 다시 부르면 다시 조립하고, 장비의 버전이 그대로이면 해석은 건너뜁니다.
         */
        void setEquipment( const Equipment* pEquipment );
        /** @brief 몸과 부품을 보이거나 숨깁니다. 1인칭에서 몸을 숨길 때 씁니다. */
        void setPartsVisible( bool bVisible );
        bool isPartsVisible() const { return _bPartsVisible; }
        /**
         * @brief 이 칸(부품 주인 — 슬롯 또는 꾸미기 매개변수)들의 부품을 숨깁니다. 몸과 다른 칸은 `setPartsVisible` 을 따릅니다. 빈 목록이면 숨기는 칸이 없습니다.
         * @details 1인칭에서 몸은 보이고 머리 칸 · 손 무기 칸만 숨길 때 쓴다. 다시 조립해도(장비 바꿈) 목록을 지킨다.
         */
        void                         setHiddenSlots( const vector<hashed_string>& listSlot );
        const vector<hashed_string>& getHiddenSlots() const { return _listHiddenSlot; }
        /** @brief 스폰한 부품 오브젝트를 모두 지웁니다. 상태를 저장하기 전에 부릅니다(풀에도 남기지 않는다). 다음 조립에서 다시 스폰합니다. */
        void despawnParts();
        /**
         * @brief 캐릭터 오브젝트(이 컴포넌트의 주인)를 지웁니다. 지우기 전에 소켓 부품을 씬의 부품 풀(`SocketPartPool`)에 쉬게 하고(다음 캐릭터의 조립이 같은
         *        에셋을 꺼내 쓴다), 나머지(스킨드 부품, 물리로 떨어진 부품)는 지웁니다. 틱 안이면 둘 다 틱 뒤에 합니다.
         * @details 한 함수인 까닭: 붙은 부품은 캐릭터의 자식이라, 캐릭터를 먼저 지우면 함께 지울 예정이 되어 풀에 넣을 수 없다. 그냥 `destroyObject` 로 지우면
         *          부품은 끝날 때 함께 지워진다(풀을 쓰지 않을 뿐 틀리지는 않는다).
         */
        void destroyOwnerStowingParts();

        /**
         * @brief 이름(접두어 포함, 예: `MainHand.Muzzle`, `Eyes`)의 소켓 월드 변환입니다. 지난 프레임의 몸 포즈와 부품 월드로 계산합니다.
         * @return 조립 전이거나 이번 외형에 없는 소켓이면 false 입니다.
         */
        bool findSocketWorldTransform( const hashed_string& fullName, float4x4& outWorldTransform ) const;
        /** @brief 이름의 몸 소켓을 몸의 바인드(레퍼런스) 포즈로 계산한 유닛 공간 변환입니다. 눈높이처럼 흔들리면 안 되는 값에 씁니다. */
        bool findBindSocketTransform( const hashed_string& fullName, float4x4& outUnitTransform ) const;
        /** @brief 지난 해석 결과입니다. */
        const ResolvedAppearance& getResolvedAppearance() const { return _state.getResolved(); }
        /** @brief 소켓 테이블입니다. */
        const AppearanceSocketRig& getSocketRig() const { return _rig; }
        /**
         * @brief 몸 표면 상태입니다(젖음 · 흙 · 피 · 찢김). 게임플레이가 영역 값을 정하고 맞은 위치에 스탬프를 찍는다(`CharacterSurfaceState`) — 틱이 감쇠시키고 바뀐 프레임에 머티리얼로 올린다.
         *        `_surfaceChannelsPath` 가 비어 있으면 채널이 없는 빈 상태입니다(`hasSurfaceState` 가 false).
         */
        CharacterSurfaceState&       getSurfaceState() { return _surface; }
        const CharacterSurfaceState& getSurfaceState() const { return _surface; }
        /** @brief 표면 상태가 채널을 가지고 있으면 true 입니다. */
        bool hasSurfaceState() const { return _surface.getChannels().empty() == false; }
        /** @brief 조립한 횟수입니다. 진단과 테스트에 씁니다. */
        uint32 getAssembleCount() const { return _assembleCount; }
        /** @brief 합친 메시의 구간 수입니다(몸 구간 + 합친 부품 하나씩). 합치지 않았으면 0 입니다. 진단과 탐침에 씁니다. */
        uint32 getMergedSectionCount() const;
        /**
         * @brief 지난 조립이 푼 피팅 결과입니다(캐릭터 편집 창의 피팅 확인 표시). 미리보기 입히기(`previewSelection`)만 남깁니다 — 게임의 조립은 같은 차림이 구운 메시를 나누므로
         *        (`AppearanceBakedMeshCache`) 풀지 않을 때가 있어 결과를 두지 않는다. 풀어 본 적이 없으면 nullptr 입니다.
         */
        const FitResult* getLastFitResult() const { return _lastFitResult.get(); }
        /** @brief `getLastFitResult` 가 바뀔 때마다 오르는 번호입니다(창이 요약을 다시 만들지 가릅니다). */
        uint32 getFitResultRevision() const { return _fitResultRevision; }
        /** @brief 체형 축(`bodyShapes` 의 축 값 = 꾸미기 `BoneProportion` 드라이브)이 정한 본 비율입니다. 단계 작업이 몸 포즈 위에 겹칩니다. 비어 있으면 비율이 없습니다. */
        const BoneProportion& getBodyProportion() const { return _bodyProportion; }
        /** @brief 지금 스폰해 둔 부품 오브젝트입니다. */
        void collectPartObjects( vector<GameObjectHandle>& outListObject ) const;

    private:
        /** @brief 스폰한 부품 하나입니다. */
        struct SpawnedPart
        {
            GameObjectHandle                       _object{};
            hashed_string                          _owner{};
            hashed_string                          _partName{};
            hashed_string                          _asset{};
            uint32                                 _partIndex{ 0 };
            uint32                                 _holderUnit{ AppearanceSocketRig::kNoUnit }; ///< 붙은 유닛(몸이면 0). 몸이면 포즈마다 위치를 고친다
            uint8                                  _bSocketPart{ SW_TRUE };                     ///< 소켓에 붙는 부품인지 여부(아니면 몸을 따르는 스킨드 부품)
            uint8                                  _bBaked{ SW_FALSE };                         ///< 베이크한 메시(체형 · 피팅)를 그린다 — 다시 조립하는 동안 숨기지 않는다
            uint8                                  _bMerged{ SW_FALSE };                        ///< 몸 메시에 합쳐 구간으로 그린다(오브젝트가 없다). `_section` 은 병합한 메시를 건 뒤에 생긴다
            TComponentHandle<MeshSectionComponent> _section{};                                  ///< 병합한 부품의 구간 컴포넌트(몸 오브젝트에 붙는다)
        };

        /** @brief 슬롯 덮어쓰기 하나입니다. */
        struct SlotOverride
        {
            hashed_string _slot{};
            hashed_string _itemID{};
        };

        /** @brief 베이크한 메시를 짓는 스킨드 부품 하나의 로드입니다(원본 메시 · 피팅 데이터). 준비된 핸들이 원본을 쥔다. */
        struct MeshAssemblyLoad
        {
            MeshHandle                     _mesh{};
            SharedAssetHandle<FitPartData> _fit{};                ///< 피팅 데이터가 없는 부품이면 요청이 없다
            uint64                         _sourceContentID{ 0 }; ///< 구울 때의 원본 내용 번호. 바뀌면(메시 핫 리로드) 다시 조립한다
            uint32                         _partIndex{ 0 };
        };

    private:
        /** @brief 조립을 틱 뒤(또는 지금)로 예약합니다. 이미 예약되어 있으면 그대로 둡니다. */
        void requestAssemble();
        /** @brief 펼치고, 해석하고, 조립합니다. 게임 스레드의 틱 밖에서 실행합니다. */
        void assemble();
        /** @brief 몸 부품을 이 오브젝트의 유닛에 입힙니다. 몸 부품 번호를 반환하고, 없으면 -1 입니다. */
        int32 applyBodyPart( SkeletalMeshComponent& body );
        /**
         * @brief 해석의 체형 축 값(`ResolvedBoneProportion`, 주인이 빈 것)을 몸 부품의 체형 축 데이터(`bodyShapes`)로 풀어 본 비율(`_bodyProportion`)과 모프 가중치(`_listShapeMorph`)를 정합니다.
         * @details 축 값이 있는데 몸 부품에 `bodyShapes` 가 없거나 읽지 못하면, 또는 모르는 축 이름이 있으면 오류를 남깁니다(슬라이더가 조용히 아무것도 안 하지 않게). 몸의 포즈를 다시 만들게 표시합니다.
         */
        void applyBodyShapes( SkeletalMeshComponent& body );
        /**
         * @brief 부품 오브젝트를 해석 결과와 맞춥니다. 같은 것은 그대로 두고, 없어진 것은 지우고, 새로 생긴 것은 스폰합니다.
         * @param listDetachEvent 이번 해석에서 떨어져 나간 부품입니다. 없어진 것 가운데 여기 있는 소켓 부품은 지우지 않고 물리로 뗍니다.
         */
        void syncParts( GameObjectManager& manager, SkeletalMeshComponent& body, int32 bodyPart, const vector<AppearanceDetachEvent>& listDetachEvent );
        /**
         * @brief 해석에서 빠진 부품 @p spawned 가 이번에 떨어져 나간 소켓 부품이면 물리로 떼고 true 입니다. 떼어 낸 오브젝트는 더 이상 이 컴포넌트의 것이 아닙니다.
         * @return 떨어져 나간 부품이 아니거나 뗄 수 없으면(물리 바디 없음 — 알린다) false 이고, 부르는 쪽이 지웁니다.
         */
        [[nodiscard]] bool releaseDetachedPart( GameObject& object, const SpawnedPart& spawned, const vector<AppearanceDetachEvent>& listDetachEvent ) const;
        /** @brief 부품을 모두 거둡니다. @p bPool 이면 소켓 부품을 풀에 넣고, 아니면(끝날 때) 모두 지웁니다. */
        void removeParts( bool bPool );
        /** @brief 부품 하나를 거둡니다 — @p bPool 이고 소켓 부품이면 풀에 넣고, 풀이 거절하거나 아니면 지웁니다. */
        void retirePart( GameObjectManager& manager, GameObject& object, const SpawnedPart& spawned, bool bPool ) const;
        /** @brief 소켓 부품을 붙을 위치에 부착합니다. */
        void bindSocketParts( GameObjectManager& manager );
        /**
         * @brief 같은 오브젝트의 후처리 리그(`PoseModifierComponent`)에 소켓 에셋을 가진 소켓 부품을 칸 이름의 강체 유닛으로 잇습니다(`bindRigidUnit`).
         *        리그의 `{ "unit": "MainHand", "socket": "SupportHand" }` 가 지금 든 무기의 소켓을 따른다. 이번 조립에 없는 칸은 연결을 풉니다.
         */
        void bindRigUnits( GameObjectManager& manager );
        /** @brief 리그에 이은 칸을 모두 풉니다. */
        void unbindRigUnits();
        /** @brief 머티리얼 값을 주인의 부품마다 적용합니다. */
        void applyMaterialValues( GameObjectManager& manager, SkeletalMeshComponent& body );
        /** @brief 보임 여부를 몸과 부품에 적용합니다. */
        void applyVisibility( GameObjectManager& manager );
        /** @brief 몸의 현재 포즈로 몸 소켓에 붙은 부품의 위치를 고칩니다. 게임 스레드에서 실행합니다. */
        void refreshSocketTransforms( const SkeletalMeshComponent& body );
        /** @brief 몸 유닛에 단계 작업을 등록합니다. 이미 등록했으면 그대로 둡니다. */
        void registerSocketTask( SkeletalMeshComponent& body );
        void unregisterSocketTask();
        /** @brief 유닛 번호에 해당하는 오브젝트입니다. 몸이면 이 오브젝트입니다. */
        GameObject*        findUnitObject( GameObjectManager& manager, uint32 unitIndex ) const;
        GameObjectManager* findManager() const;
        /** @brief 이번 조립의 외형 전개입니다. 미리보기 입히기면 선택을, 아니면 프리셋 id 를 펼칩니다. 펼치지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool expandSpec( const AppearanceDatabase& database, CharacterAppearanceSpec& outSpec ) const;
        /** @brief 외형 데이터가 다시 읽혔습니다(`AppearanceDatabase::registerReloaded`). 다시 조립을 예약합니다. */
        void onAppearanceDataReloaded();
        /**
         * @brief 베이크한 메시에 필요한 원본 메시 · 피팅 표 · 피팅 데이터를 요청합니다(조립 끝). 구울 것이 없고 지난번에도 베이크하지 않았으면 아무것도 하지 않습니다.
         * @details 다 읽히면 `applyMeshAssembly` 가 유닛에 건다 — 플레이 중이면 틱이 기다렸다 틱 뒤에, 플레이하지 않는 미리보기 씬이면 이 자리에서 기다린다.
         */
        void requestMeshAssembly( GameObjectManager& manager );
        /** @brief 요청한 것 가운데 아직 읽는 것이 있으면 true 입니다. */
        bool isMeshAssemblyLoading() const;
        /** @brief 읽은 것으로 베이크한 메시를 지어 몸과 스킨드 부품 유닛의 스킨 원본으로 겁니다(바꿀 것이 없는 부품은 원본). 틱 밖, 게임 스레드입니다. */
        void applyMeshAssembly();
        /** @brief 베이크한 메시의 재료(원본 메시 · 피팅 표 · 피팅 데이터)가 다시 읽혔으면 true 입니다. */
        bool isMeshAssemblyStale() const;
        /** @brief 부품 번호의 스킨드 유닛입니다(몸이면 이 오브젝트의 유닛). 없으면 nullptr 입니다. */
        SkeletalMeshComponent* findSkinnedUnit( GameObjectManager& manager, uint32 partIndex ) const;
        /** @brief 병합이 켜져 있고 이 부품이 몸 메시에 합칠 수 있으면(몸이 아닌 스킨드 부품, 자기 리그 없음, 몸과 같은 스켈레톤) true 입니다. */
        bool isMergedPart( const ResolvedPart& part, int32 partIndex ) const;
        /** @brief 합친 부품이 하나라도 있으면 true 입니다. */
        bool hasMergedParts() const;
        /**
         * @brief 몸과 합친 부품의 메시(@p listSource 원본 · @p listBaked 베이크한 사본, 로드 순서)를 하나로 합쳐 몸 유닛에 걸고 부품마다 구간 컴포넌트를 둡니다.
         * @details 같은 차림이 합친 메시를 나누도록 @p bakeKey 로 `AppearanceBakedMeshCache` 에 둔다. 합치지 못하면 오류를 남기고 false 입니다(몸 원본만 그려진다).
         */
        [[nodiscard]] bool applyMergedMeshes( GameObjectManager& manager, SkeletalMeshComponent& body, const vector<shared_ptr<Mesh>>& listSource,
                                              const vector<shared_ptr<Mesh>>& listBaked, uint64 bakeKey );
        /** @brief 합친 부품의 구간 컴포넌트를 몸 오브젝트에서 뗍니다. */
        void retireMergedPart( GameObjectManager& manager, const SpawnedPart& spawned ) const;
        /** @brief 부품 하나가 그리는 메시 컴포넌트마다 @p func 를 부릅니다 — 합친 부품은 구간 컴포넌트 하나, 아니면 부품 오브젝트의 메시 전부입니다. */
        template <typename Func>
        void forEachPartMesh( GameObjectManager& manager, const SpawnedPart& spawned, Func&& func ) const;
        /** @brief 피팅 표 · 피팅 데이터 캐시의 다시 읽은 횟수 합입니다(오르기만 한다). */
        static uint32 computeFitReloadStamp();
        /**
         * @brief 표면 상태를 채널 파일(`_surfaceChannelsPath`)과 영역 수로 맞춥니다. 파일이 비면 상태를 끄고, 같은 파일 · 같은 영역 수면 값을 지키며, 바뀌면 처음부터 다시 만듭니다.
         *        읽지 못하면 오류를 남기고 상태를 끕니다(말없이 다른 채널을 쓰지 않는다).
         */
        void rebuildSurfaceState( uint32 regionCount );
        /** @brief 표면 상태를 부품의 머티리얼에 올립니다(채널 스칼라 · 영역 벡터 · 마스크 텍스처). 상태 번호가 그대로면 @p bForce 가 아닌 한 올리지 않습니다. */
        void applySurface( GameObjectManager& manager, bool bForce );

    private:
        PROPERTY( Category = "Appearance", DisplayName = "Preset", Tooltip = "CharacterAppearance preset id (data/appearance/presets.xml)" )
        string _presetID;
        PROPERTY( Category = "Appearance", DisplayName = "Seed", Tooltip = "Seed for the preset's random choices (lists, ranges, colour lists)" )
        int32 _seed;
        PROPERTY( Category = "Appearance", DisplayName = "Surface Channels",
                  Tooltip = "Surface channel table (*.surfacechannels.xml): wetness, dirt, blood, tears... Empty keeps the body surface state off" )
        string _surfaceChannelsPath;
        PROPERTY( Category = "Appearance", DisplayName = "Merge Parts",
                  Tooltip = "Merge the body and the skinned parts that follow it into one sectioned mesh: one skinning unit and one pose, each part still drawn with its own material" )
        uint8                  _bMergeParts      : 1;
        [[maybe_unused]] uint8 _reservedProperty : 7;

        CharacterAppearanceSocketTask           _socketTask;
        CharacterAppearanceProportionTask       _proportionTask;
        BoneProportion                          _bodyProportion;       ///< 체형 축이 정한 본 비율(`applyBodyShapes`)
        vector<BodyMorphWeight>                 _listShapeMorph;       ///< 체형 축이 정한 몸 모프 가중치 — 구울 때 해석의 모프에 더한다
        shared_ptr<const BodyShapeSet>          _bodyShapeSet;         ///< 쥐고 있는 체형 축 데이터(핫 리로드 판정)
        uint32                                  _seenShapeReloadCount; ///< `applyBodyShapes` 가 본 `SharedAssetCache<BodyShapeSet>` 의 다시 읽은 횟수
        AppearanceSocketRig                     _rig;
        AppearanceSocketSetCache                _socketCache;
        CharacterAppearanceState                _state;      ///< 펼친 프리셋과 해석 결과. 장비 · 데이터 · 입력의 버전으로 다시 해석할지 정한다
        const Equipment*                        _pEquipment; ///< 빌린 장비(`setEquipment`). 없으면 프리셋의 장비 구성 그대로
        CharacterBoneArray                      _bodyBindBones;
        CharacterBoneArray                      _bodyBones; ///< 지난 포즈(모델 공간). 소켓 질의가 읽는다
        vector<SpawnedPart>                     _listSpawnedPart;
        vector<SlotOverride>                    _listSlotOverride;
        vector<hashed_string>                   _listHiddenSlot;       ///< 보임과 따로 숨기는 슬롯(`setHiddenSlots`)
        vector<hashed_string>                   _listRigUnit;          ///< 후처리 리그에 강체 유닛으로 이은 칸(`bindRigUnits`)
        TComponentHandle<SkeletalMeshComponent> _taskUnit;             ///< 단계 작업을 등록한 몸 유닛
        AppearanceSelection                     _previewSelection;     ///< 미리보기 입히기의 선택(`previewSelection`)
        const AppearanceDatabase*               _pPreviewDatabase;     ///< 미리보기 입히기의 데이터(빌린 것). 있으면 프리셋 id · 게임 서비스를 보지 않는다
        uint64                                  _reloadDatabaseID;     ///< 다시 읽기를 구독한 외형 데이터의 번호(`AppearanceDatabase::getInstanceID`, 플레이 중에만 — 포인터를 들지 않는다)
        DelegateHandle                          _reloadSubscription;   ///< `_reloadDatabaseID` 데이터의 구독
        vector<MeshAssemblyLoad>                _listMeshAssemblyLoad; ///< 베이크한 메시의 부품 로드(몸이 먼저)
        SharedAssetHandle<FitTables>            _fitTablesLoad;        ///< 몸의 피팅 표 로드. 표가 없으면 요청이 없다
        shared_ptr<FitResult>                   _lastFitResult;        ///< 지난 미리보기 조립이 푼 피팅 결과(`getLastFitResult`)
        uint32                                  _fitResultRevision;
        uint32                                  _assembleCount;
        uint32                                  _seenFitReloadStamp; ///< 마지막으로 구울 때의 `computeFitReloadStamp`
        int32                                   _bodyPart;
        bool                                    _bPartsVisible;
        uint8                                   _bAssembleScheduled   : 1;
        uint8                                   _bStarted             : 1;
        uint8                                   _bMeshAssemblyPending : 1; ///< 요청한 것을 아직 걸지 않았다(틱이 기다린다)
        uint8                                   _bMeshAssembled       : 1; ///< 유닛이 베이크한 메시를 그린다 — 구울 것이 없어져도 원본으로 되돌려야 한다
        uint8                                   _bProportionTask      : 1; ///< 본 비율 단계 작업을 몸 유닛에 등록했다(비율이 있을 때만 등록해 군중 공유를 막지 않는다)
        uint8                                   _bMeshMerged          : 1; ///< 몸 유닛이 합친 메시를 그리고 구간 이름을 가졌다 — 합치지 않게 되면 되돌려야 한다
        [[maybe_unused]] uint8                  _reserved             : 2;
        hashed_string                           _mergedBodySection;      ///< 합친 메시에서 몸이 그리는 구간 이름(몸 부품 이름)
        CharacterSurfaceState                   _surface;                ///< 표면 상태(`_surfaceChannelsPath` 가 있을 때만 채널을 가진다)
        CharacterSurfaceBinding                 _surfaceBinding;         ///< 표면 상태를 부품 머티리얼에 올리고 마스크 텍스처를 쥔다
        string                                  _loadedSurfaceChannels;  ///< 지금 상태가 읽은 채널 파일 경로(바뀌었는지 가른다)
        uint64                                  _surfaceAppliedRevision; ///< 머티리얼에 마지막으로 올린 상태 번호(같은 번호는 다시 올리지 않는다)
        uint32                                  _surfaceRegionCount;     ///< 상태가 가진 영역 수(피팅 표의 영역 수)
    };
} // namespace sw
