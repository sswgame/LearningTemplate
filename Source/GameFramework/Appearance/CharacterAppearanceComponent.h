/**
 * @file CharacterAppearanceComponent.h
 * @brief 외형 컴포넌트 — 외형 프리셋(`CharacterAppearance`)을 펼쳐 해석하고, 그 결과를 오브젝트로 조립합니다(몸 메시 · 장비 부품 · 소켓 부착 · 염색).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/CharacterGeometry.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Appearance/AppearanceResolver.h"
#include "GameFramework/Appearance/AppearanceSocketRig.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class CharacterAppearanceComponent;
    class GameObjectManager;
    class SkeletalMeshComponent;

    /**
     * @class CharacterAppearanceSocketTask
     * @brief 외형 컴포넌트가 몸 유닛에 거는 단계 일입니다 — 몸의 포즈가 끝난 프레임에 부품의 소켓 자리를 고칩니다(게임 스레드, `finishAnimationFrame`).
     * @details 리플렉션 컴포넌트는 기반 클래스를 하나만 두므로(`castTo` 가 단일 상속을 가정) 인터페이스는 이 작은 객체가 구현합니다. 할 일이 있다고
     *          말하지 않으므로 몸이 쉬면(재생 없음) 이 일도 돌지 않습니다 — 본이 움직이지 않으면 소켓도 그대로입니다.
     */
    class SW_GF_API CharacterAppearanceSocketTask final : public IAnimationPhaseTask
    {
    public:
        explicit CharacterAppearanceSocketTask( CharacterAppearanceComponent& owner );

        bool isAnimationActive() const override { return false; }
        void runAnimationPhase( AnimationPhase phase, SkeletalMeshComponent& unit, const AnimationFrameContext& context ) override;
        void finishAnimationFrame( SkeletalMeshComponent& unit ) override;
        void onAnimationUnitDetached( SkeletalMeshComponent& unit ) override;

    private:
        CharacterAppearanceComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CharacterAppearanceComponent
     * @brief 몸 유닛(같은 오브젝트의 `SkeletalMeshComponent`)에 외형 프리셋을 입힙니다. 외형 데이터는 게임 서비스 `AppearanceDatabase` 입니다.
     * @details - **조립**: 프리셋을 씨앗으로 펼치고(`CharacterAppearanceCatalog::expand`) 칸 덮어쓰기(`setSlotItem` — 무기 바꾸기)를 얹어 해석합니다.
     *            몸 부품(주인이 빈 첫 스킨드 부품)은 이 오브젝트의 유닛에 메시 · 스켈레톤 · 머티리얼로, 다른 스킨드 부품은 몸을 리더로 따르는 자식 유닛으로,
     *            소켓 부품(`SocketPrefab`)은 프리팹을 세워 `SocketBindingComponent` 로 소켓에 붙입니다. 다시 조립할 때 같은 부품(주인 · 이름 · 에셋)은 그대로 둡니다.
     *          - **소켓**: 몸과 부품의 소켓 에셋이 한 이름 공간(`AppearanceSocketRig` — `MainHand.Muzzle`)입니다. 몸의 포즈가 끝난 프레임마다(단계 일) 부품의
     *            소켓 자리를 고칩니다. `findSocketWorldTransform` 은 지난 프레임의 포즈 · 월드로 답합니다(틱 안에서 읽기만).
     *          - **염색**: 해석의 머티리얼 값(`Parameter` — 꾸미기 `MaterialColor` · `MaterialScalar`)을 주인의 부품마다 머티리얼 인스턴스로 겁니다.
     *          - **틱 안 호출**: 구조를 바꾸는 일(스폰 · 부착)은 틱 뒤로 미룹니다 — 바꾸는 함수는 언제 불러도 됩니다.
     *          - 세운 부품은 판의 모습일 뿐이라 상태 저장 전에 `despawnParts` 로 걷습니다(다시 시작하면 다시 조립한다).
     */
    REFLECT( Category = "Character", DisplayName = "Character Appearance", Tooltip = "Assembles an appearance preset: body mesh, equipment on sockets and dye" )
    class SW_GF_API CharacterAppearanceComponent : public Component
    {
        friend class CharacterAppearanceSocketTask;

    public:
        REFLECT_BODY();

        CharacterAppearanceComponent();
        virtual ~CharacterAppearanceComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 프리셋 · 씨앗을 바꾸고 다시 조립합니다(칸 덮어쓰기는 남김). */
        void setPreset( const hashed_string& presetId, uint32 seed );
        /** @brief 칸의 아이템을 덮어쓰고 다시 조립합니다(빈 아이템이면 칸을 비움). 같은 값이면 아무것도 안 합니다. */
        void setSlotItem( const hashed_string& slot, const hashed_string& itemId );
        /** @brief 몸과 부품을 보이거나 숨깁니다(1인칭에서 몸 숨기기). */
        void setPartsVisible( bool bVisible );
        bool isPartsVisible() const { return _bPartsVisible; }
        /** @brief 세운 부품 오브젝트를 모두 지웁니다(상태 저장 전 · 끝낼 때). 다음 조립이 다시 세웁니다. */
        void despawnParts();

        /**
         * @brief 이름(접두어 포함 — `MainHand.Muzzle` · `Eyes`)의 소켓 월드 변환입니다. 지난 프레임의 몸 포즈 · 부품 월드로 계산합니다.
         * @return 조립 전이거나 이번 외형에 없는 소켓이면 false 입니다.
         */
        bool findSocketWorldTransform( const hashed_string& fullName, float4x4& outWorldTransform ) const;
        /** @brief 이름의 몸 소켓을 몸의 바인드(레퍼런스) 포즈로 계산한 유닛 공간 변환입니다(눈높이처럼 흔들리지 않을 값). */
        bool findBindSocketTransform( const hashed_string& fullName, float4x4& outUnitTransform ) const;
        /** @brief 지난 해석 결과입니다. */
        const ResolvedAppearance& getResolvedAppearance() const { return _resolved; }
        /** @brief 소켓 표입니다. */
        const AppearanceSocketRig& getSocketRig() const { return _rig; }
        /** @brief 조립한 횟수입니다(진단 · 시험). */
        uint32 getAssembleCount() const { return _assembleCount; }
        /** @brief 지금 세워 둔 부품 오브젝트입니다. */
        void collectPartObjects( vector<GameObjectHandle>& outListObject ) const;

    private:
        /** @brief 세운 부품 하나입니다. */
        struct SpawnedPart
        {
            GameObjectHandle _object{};
            hashed_string    _owner{};
            hashed_string    _partName{};
            hashed_string    _asset{};
            uint32           _partIndex{ 0 };
            uint32           _holderUnit{ AppearanceSocketRig::kNoUnit }; ///< 붙은 유닛(몸이면 0) — 몸이면 포즈마다 자리를 고친다
            uint8            _bSocketPart{ SW_TRUE };                     ///< 소켓에 붙는 부품(아니면 몸을 따르는 스킨드 부품)
        };

        /** @brief 칸 덮어쓰기 하나입니다. */
        struct SlotOverride
        {
            hashed_string _slot{};
            hashed_string _itemId{};
        };

    private:
        /** @brief 조립을 틱 뒤(또는 지금)로 겁니다. 이미 걸려 있으면 그대로입니다. */
        void requestAssemble();
        /** @brief 펼치고 해석하고 조립합니다(게임 스레드, 틱 밖). */
        void assemble();
        /** @brief 몸 부품을 이 오브젝트의 유닛에 입힙니다. 몸 부품 번호입니다(없으면 -1). */
        int32 applyBodyPart( SkeletalMeshComponent& body );
        /** @brief 부품 오브젝트를 해석 결과와 맞춥니다(같은 것은 두고, 없어진 것은 지우고, 새것은 세움). */
        void syncParts( GameObjectManager& manager, SkeletalMeshComponent& body, int32 bodyPart );
        /** @brief 소켓 부품을 붙을 자리에 묶습니다. */
        void bindSocketParts( GameObjectManager& manager );
        /** @brief 머티리얼 값을 주인의 부품마다 겁니다. */
        void applyMaterialValues( GameObjectManager& manager, SkeletalMeshComponent& body );
        /** @brief 보임을 몸과 부품에 겁니다. */
        void applyVisibility( GameObjectManager& manager );
        /** @brief 몸의 지금 포즈로 몸 소켓에 붙은 부품의 자리를 고칩니다(게임 스레드). */
        void refreshSocketTransforms( const SkeletalMeshComponent& body );
        /** @brief 몸 유닛에 단계 일을 겁니다(이미 걸었으면 그대로). */
        void registerSocketTask( SkeletalMeshComponent& body );
        void unregisterSocketTask();
        /** @brief 유닛 번호의 오브젝트(몸이면 이 오브젝트)입니다. */
        GameObject*        findUnitObject( GameObjectManager& manager, uint32 unitIndex ) const;
        GameObjectManager* findManager() const;

    private:
        PROPERTY( Category = "Appearance", DisplayName = "Preset", Tooltip = "CharacterAppearance preset id (data/appearance/presets.xml)" )
        string _presetId;
        PROPERTY( Category = "Appearance", DisplayName = "Seed", Tooltip = "Seed for the preset's random choices (lists, ranges, colour lists)" )
        int32 _seed;

        CharacterAppearanceSocketTask _socketTask;
        AppearanceSocketRig           _rig;
        AppearanceSocketSetCache      _socketCache;
        ResolvedAppearance            _resolved;
        CharacterBoneArray            _bodyBindBones;
        CharacterBoneArray            _bodyBones; ///< 지난 포즈(모델 칸) — 소켓 질의가 읽는다
        vector<SpawnedPart>           _listSpawnedPart;
        vector<SlotOverride>          _listSlotOverride;
        ComponentHandle               _taskUnit; ///< 단계 일을 건 몸 유닛
        uint32                        _assembleCount;
        int32                         _bodyPart;
        bool                          _bPartsVisible;
        uint8                         _bAssembleScheduled : 1;
        uint8                         _bStarted           : 1;
        [[maybe_unused]] uint8        _reserved           : 6;
    };
} // namespace sw
