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
#include "Core/Math/MatrixMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/SceneAnimation/AnimationSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceResolver.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceSelection.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceSocketRig.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct CharacterAppearanceSpec;

    class AppearanceDatabase;
    class CharacterAppearanceComponent;
    class GameObjectManager;
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
     * @class CharacterAppearanceComponent
     * @brief 몸 유닛(같은 오브젝트의 `SkeletalMeshComponent`)에 외형 프리셋을 입힙니다. 외형 데이터는 게임 서비스 `AppearanceDatabase` 에서 가져옵니다.
     * @details - **조립**: 프리셋을 시드로 펼치고(`CharacterAppearanceCatalog::expand`), 슬롯 덮어쓰기(`setSlotItem`, 무기 바꾸기에 씀)를 적용해 해석한다.
     *            몸 부품(주인이 비어 있는 첫 스킨드 부품)은 이 오브젝트의 유닛에 메시, 스켈레톤, 머티리얼로 적용한다. 다른 스킨드 부품은 몸을 리더로 따르는 자식 유닛으로 만든다.
     *            소켓 부품(`SocketPrefab`)은 프리팹을 스폰해서 `SocketBindingComponent` 로 소켓에 붙인다. 다시 조립할 때 같은 부품(주인, 이름, 에셋)은 그대로 둔다.
     *          - **소켓**: 몸과 부품의 소켓 에셋이 하나의 이름 공간을 이룬다(`AppearanceSocketRig`, 예: `MainHand.Muzzle`). 몸의 포즈가 끝난 프레임마다(단계 작업) 부품의
     *            소켓 위치를 고친다. `findSocketWorldTransform` 은 지난 프레임의 포즈와 월드로 답하며, 틱 안에서는 읽기만 한다.
     *          - **염색**: 해석 결과의 머티리얼 값(`Parameter`, 꾸미기의 `MaterialColor` 와 `MaterialScalar`)을 주인의 부품마다 머티리얼 인스턴스에 적용한다.
     *          - **틱 안 호출**: 구조를 바꾸는 일(스폰, 부착)은 틱 뒤로 미룬다. 그래서 바꾸는 함수는 언제 불러도 된다.
     *          - 스폰한 부품은 현재 상태의 결과일 뿐이므로, 상태를 저장하기 전에 `despawnParts` 로 정리한다. 다시 시작하면 다시 조립한다.
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

        /** @brief 프리셋과 시드를 바꾸고 다시 조립합니다. 슬롯 덮어쓰기는 그대로 둡니다. */
        void setPreset( const hashed_string& presetID, uint32 seed );
        /** @brief 슬롯의 아이템을 덮어쓰고 다시 조립합니다. 빈 아이템이면 슬롯을 비웁니다. 같은 값이면 아무것도 하지 않습니다. */
        void setSlotItem( const hashed_string& slot, const hashed_string& itemID );
        /**
         * @brief 미리보기 입히기 — 프리셋 id 대신 외형 선택 @p selection 을 데이터 @p database 로 조립합니다. 게임 서비스가 없는 씬(에디터 미리보기 창)이 씁니다.
         * @details 시작(`onBeginPlay`)을 기다리지 않고 조립을 예약한다 — 미리보기 씬은 플레이를 돌리지 않는다. 한 번 부르면 이 컴포넌트는 프리셋 id 와
         *          게임 서비스 `AppearanceDatabase` 를 보지 않고 이 선택 · 데이터로만 조립한다(두 길이 섞이지 않는다). @p database 는 빌린 것이라 컴포넌트보다 오래 살아야 한다.
         *          선택을 펼치며 버리거나 되돌린 것(`AppearanceSelectionReport`)은 경고로 남긴다.
         */
        void previewSelection( const AppearanceSelection& selection, const AppearanceDatabase& database );
        /** @brief 몸과 부품을 보이거나 숨깁니다. 1인칭에서 몸을 숨길 때 씁니다. */
        void setPartsVisible( bool bVisible );
        bool isPartsVisible() const { return _bPartsVisible; }
        /**
         * @brief 이 칸(부품 주인 — 슬롯 또는 꾸미기 매개변수)들의 부품을 숨깁니다. 몸과 다른 칸은 `setPartsVisible` 을 따릅니다. 빈 목록이면 숨기는 칸이 없습니다.
         * @details 1인칭에서 몸은 보이고 머리 칸 · 손 무기 칸만 숨길 때 쓴다. 다시 조립해도(장비 바꿈) 목록을 지킨다.
         */
        void                         setHiddenSlots( const vector<hashed_string>& listSlot );
        const vector<hashed_string>& getHiddenSlots() const { return _listHiddenSlot; }
        /** @brief 스폰한 부품 오브젝트를 모두 지웁니다. 상태를 저장하기 전과 끝낼 때 부릅니다. 다음 조립에서 다시 스폰합니다. */
        void despawnParts();

        /**
         * @brief 이름(접두어 포함, 예: `MainHand.Muzzle`, `Eyes`)의 소켓 월드 변환입니다. 지난 프레임의 몸 포즈와 부품 월드로 계산합니다.
         * @return 조립 전이거나 이번 외형에 없는 소켓이면 false 입니다.
         */
        bool findSocketWorldTransform( const hashed_string& fullName, float4x4& outWorldTransform ) const;
        /** @brief 이름의 몸 소켓을 몸의 바인드(레퍼런스) 포즈로 계산한 유닛 공간 변환입니다. 눈높이처럼 흔들리면 안 되는 값에 씁니다. */
        bool findBindSocketTransform( const hashed_string& fullName, float4x4& outUnitTransform ) const;
        /** @brief 지난 해석 결과입니다. */
        const ResolvedAppearance& getResolvedAppearance() const { return _resolved; }
        /** @brief 소켓 테이블입니다. */
        const AppearanceSocketRig& getSocketRig() const { return _rig; }
        /** @brief 조립한 횟수입니다. 진단과 테스트에 씁니다. */
        uint32 getAssembleCount() const { return _assembleCount; }
        /** @brief 지금 스폰해 둔 부품 오브젝트입니다. */
        void collectPartObjects( vector<GameObjectHandle>& outListObject ) const;

    private:
        /** @brief 스폰한 부품 하나입니다. */
        struct SpawnedPart
        {
            GameObjectHandle _object{};
            hashed_string    _owner{};
            hashed_string    _partName{};
            hashed_string    _asset{};
            uint32           _partIndex{ 0 };
            uint32           _holderUnit{ AppearanceSocketRig::kNoUnit }; ///< 붙은 유닛(몸이면 0). 몸이면 포즈마다 위치를 고친다
            uint8            _bSocketPart{ SW_TRUE };                     ///< 소켓에 붙는 부품인지 여부(아니면 몸을 따르는 스킨드 부품)
        };

        /** @brief 슬롯 덮어쓰기 하나입니다. */
        struct SlotOverride
        {
            hashed_string _slot{};
            hashed_string _itemID{};
        };

    private:
        /** @brief 조립을 틱 뒤(또는 지금)로 예약합니다. 이미 예약되어 있으면 그대로 둡니다. */
        void requestAssemble();
        /** @brief 펼치고, 해석하고, 조립합니다. 게임 스레드의 틱 밖에서 실행합니다. */
        void assemble();
        /** @brief 몸 부품을 이 오브젝트의 유닛에 입힙니다. 몸 부품 번호를 반환하고, 없으면 -1 입니다. */
        int32 applyBodyPart( SkeletalMeshComponent& body );
        /** @brief 부품 오브젝트를 해석 결과와 맞춥니다. 같은 것은 그대로 두고, 없어진 것은 지우고, 새로 생긴 것은 스폰합니다. */
        void syncParts( GameObjectManager& manager, SkeletalMeshComponent& body, int32 bodyPart );
        /** @brief 소켓 부품을 붙을 위치에 부착합니다. */
        void bindSocketParts( GameObjectManager& manager );
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

    private:
        PROPERTY( Category = "Appearance", DisplayName = "Preset", Tooltip = "CharacterAppearance preset id (data/appearance/presets.xml)" )
        string _presetID;
        PROPERTY( Category = "Appearance", DisplayName = "Seed", Tooltip = "Seed for the preset's random choices (lists, ranges, colour lists)" )
        int32 _seed;

        CharacterAppearanceSocketTask           _socketTask;
        AppearanceSocketRig                     _rig;
        AppearanceSocketSetCache                _socketCache;
        ResolvedAppearance                      _resolved;
        CharacterBoneArray                      _bodyBindBones;
        CharacterBoneArray                      _bodyBones; ///< 지난 포즈(모델 공간). 소켓 질의가 읽는다
        vector<SpawnedPart>                     _listSpawnedPart;
        vector<SlotOverride>                    _listSlotOverride;
        vector<hashed_string>                   _listHiddenSlot;   ///< 보임과 따로 숨기는 슬롯(`setHiddenSlots`)
        TComponentHandle<SkeletalMeshComponent> _taskUnit;         ///< 단계 작업을 등록한 몸 유닛
        AppearanceSelection                     _previewSelection; ///< 미리보기 입히기의 선택(`previewSelection`)
        const AppearanceDatabase*               _pPreviewDatabase; ///< 미리보기 입히기의 데이터(빌린 것). 있으면 프리셋 id · 게임 서비스를 보지 않는다
        uint32                                  _assembleCount;
        int32                                   _bodyPart;
        bool                                    _bPartsVisible;
        uint8                                   _bAssembleScheduled : 1;
        uint8                                   _bStarted           : 1;
        [[maybe_unused]] uint8                  _reserved           : 6;
    };
} // namespace sw
