#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Skeleton.h"
#include "Engine/Character/Fit/CharacterGeometry.h"
#include "Engine/Character/Pose/CharacterPoseUtil.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Appearance/AppearanceResolver.h"
#include "GameFramework/Base/Appearance/AppearanceSocketRig.h"
#include "GameFramework/Base/Appearance/CharacterAppearance.h"
#include "GameFramework/Base/Inventory/ItemCatalog.h"

#include "TestFramework/TestFramework.h"

// 외형의 소켓 이름 공간 — 몸 소켓 + 칸 이름이 붙은 부품 소켓(MainHand.Muzzle), 부품 자리 = 배치 오프셋 × 소켓 × 본 포즈, 무기를 바꿔도 같은 번호,
// 외형의 소켓 덮어쓰기, 없는 부모 본은 오류. 그리고 스켈레톤 → 캐릭터 본 배열 다리(CharacterPoseUtil).

using namespace sw;

namespace
{
    struct AppearanceSocketRigTestInternal
    {
        static constexpr const utf8* kItemXml          = R"(<ItemCatalog>
            <Item id="gun_short" slot="Weapon" visual="gun_short"/><Item id="gun_long" slot="Weapon" visual="gun_long"/></ItemCatalog>)";
        static constexpr const utf8* kSlotXml          = R"(<SlotTable><Slot name="MainHand" accept="Weapon"/></SlotTable>)";
        static constexpr const utf8* kVisualXml        = R"(<ItemVisualCatalog>
            <ItemVisual id="body"><Part name="Body" kind="Skinned" mesh="m/body.mesh" sockets="mem/body.sockets.xml"/></ItemVisual>
            <ItemVisual id="gun_short"><Part name="Gun" kind="SocketPrefab" prefab="p/gun_short.prefab.xml" sockets="mem/gun_short.sockets.xml" socket="Missing,Grip" offset="0 0 0.25"/></ItemVisual>
            <ItemVisual id="gun_long"><Part name="Gun" kind="SocketPrefab" prefab="p/gun_long.prefab.xml" sockets="mem/gun_long.sockets.xml" socket="Grip"/></ItemVisual>
        </ItemVisualCatalog>)";
        static constexpr const utf8* kPresetXml        = R"(<CharacterAppearanceCatalog>
            <CharacterAppearance id="Hero" body="body"><Equip slot="MainHand" item="gun_short"/></CharacterAppearance>
            <CharacterAppearance id="LeftyHero" parent="Hero"><SocketOverride name="Grip" parent="hand.l" offset="0 0.1 0"/></CharacterAppearance>
        </CharacterAppearanceCatalog>)";
        static constexpr const utf8* kBodySocketXml    = R"(<SocketSet><Socket name="Grip" parent="hand.r" translation="0 0.05 0"/></SocketSet>)";
        static constexpr const utf8* kShortSocketXml   = R"(<SocketSet><Socket name="Muzzle" translation="0 0 0.4"/></SocketSet>)";
        static constexpr const utf8* kLongSocketXml    = R"(<SocketSet><Socket name="Muzzle" translation="0 0 0.9"/></SocketSet>)";
        static constexpr const utf8* kBadBodySocketXml = R"(<SocketSet><Socket name="Grip" parent="hand.missing"/></SocketSet>)";

        /** @brief 메모리의 소켓 에셋(경로 → XML)입니다. */
        class MemorySocketSource final : public IAppearanceSocketSource
        {
        public:
            void add( const utf8* pPath, const utf8* pXml )
            {
                SocketSet set;
                SW_EXPECT_TRUE( set.loadFromXmlText( pXml, pPath, _kinds ) );
                _mapSet[hashed_string( pPath )] = set;
            }
            const SocketSet* findSocketSet( const hashed_string& path ) override
            {
                const auto found = _mapSet.find( path );
                return found != _mapSet.end() ? &found->second : nullptr;
            }

        private:
            SocketKindTable                         _kinds;
            unordered_map<hashed_string, SocketSet> _mapSet;
        };

        /** @brief 아이템 · 외형 데이터 한 벌입니다. */
        struct Data
        {
            ItemCatalog        _items;
            AppearanceDatabase _database;

            bool load()
            {
                return _items.loadFromXmlText( kItemXml, "SocketRigTest.items" ) && _database.loadSectionFromXmlText( kSlotXml, "SocketRigTest.slots" ) &&
                       _database.loadSectionFromXmlText( kVisualXml, "SocketRigTest.visuals" ) && _database.loadSectionFromXmlText( kPresetXml, "SocketRigTest.presets" ) &&
                       _database.finishLoad( &_items );
            }

            bool resolve( const utf8* pPreset, ResolvedAppearance& outResolved ) const
            {
                CharacterAppearanceSpec spec;
                if ( _database.getPresets().expand( hashed_string( pPreset ), 0u, _database.getSlotTable(), _database.getSets(), _database.getSchemas(), spec ) == false )
                    return false;
                AppearanceResolver::resolve( _database, spec, outResolved );
                return true;
            }
        };

        /** @brief 뿌리(원점) · 오른손(1, 1, 0) · 왼손(-1, 1, 0) 세 본입니다. */
        static CharacterBoneArray makeBodyBones()
        {
            CharacterBoneArray bones;
            const int32        root = bones.addBone( hashed_string( "root" ), -1, float4x4::Identity );
            (void)bones.addBone( hashed_string( "hand.r" ), root, float4x4::createTranslation( float3{ 1.0f, 1.0f, 0.0f } ) );
            (void)bones.addBone( hashed_string( "hand.l" ), root, float4x4::createTranslation( float3{ -1.0f, 1.0f, 0.0f } ) );
            bones.computeModelTransforms();
            return bones;
        }

        static int32 findPart( const ResolvedAppearance& resolved, const utf8* pOwner )
        {
            for ( size_t partIndex = 0; partIndex < resolved._listPart.size(); ++partIndex )
            {
                if ( resolved._listPart[partIndex]._owner == hashed_string( pOwner ) )
                    return static_cast<int32>( partIndex );
            }
            return -1;
        }
    };
} // namespace

/**
 * @brief [AppearanceSocketRigTest] 무기 부품은 몸 소켓 후보 중 켜진 것(Grip)에 배치 오프셋만큼 붙고, 본이 움직이면 따라가며, 부품 소켓은 칸 이름 공간(MainHand.Muzzle)이다
 */
SW_TEST_CASE( AppearanceSocketRigTest, WeaponSitsOnTheBodySocketAndExposesItsMuzzle )
{
    using Internal = AppearanceSocketRigTestInternal;
    Internal::Data data;
    SW_ASSERT_TRUE_MSG( data.load(), data._database.getReport().joined().c_str() );
    Internal::MemorySocketSource source;
    source.add( "mem/body.sockets.xml", Internal::kBodySocketXml );
    source.add( "mem/gun_short.sockets.xml", Internal::kShortSocketXml );
    source.add( "mem/gun_long.sockets.xml", Internal::kLongSocketXml );

    ResolvedAppearance resolved;
    SW_ASSERT_TRUE( data.resolve( "Hero", resolved ) );
    const CharacterBoneArray bindBones = Internal::makeBodyBones();
    AppearanceSocketRig      rig;
    string                   error;
    SW_ASSERT_TRUE_MSG( rig.rebuild( resolved, bindBones, source, &error ), error.c_str() );
    SW_EXPECT_EQUAL( 2u, rig.getUnitCount() );

    const int32 gunPart = Internal::findPart( resolved, "MainHand" );
    SW_ASSERT_TRUE( gunPart >= 0 );
    SW_EXPECT_EQUAL( 1u, rig.findPartUnit( static_cast<uint32>( gunPart ) ) );

    // 자리 = 오프셋(0, 0, 0.25) × 소켓(0, 0.05, 0) × 오른손(1, 1, 0). 첫 후보 "Missing" 은 없으니 Grip.
    float4x4 inBody;
    uint32   holderUnit = AppearanceSocketRig::kNoUnit;
    SW_ASSERT_TRUE( rig.computePlacement( resolved._listPart[static_cast<size_t>( gunPart )]._placement, bindBones, inBody, holderUnit ) );
    SW_EXPECT_EQUAL( AppearanceSocketRig::kBodyUnit, holderUnit );
    SW_EXPECT_NEAR_EQUAL( 1.0f, inBody.getTranslation()._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.05f, inBody.getTranslation()._y, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, inBody.getTranslation()._z, 1.0e-5f );

    // 포즈 — 오른손이 위로 0.5 올라가면 무기 자리도 올라간다.
    CharacterBoneArray posed = bindBones;
    posed._listModel[1]      = float4x4::createTranslation( float3{ 1.0f, 1.5f, 0.0f } );
    SW_ASSERT_TRUE( rig.computePlacement( resolved._listPart[static_cast<size_t>( gunPart )]._placement, posed, inBody, holderUnit ) );
    SW_EXPECT_NEAR_EQUAL( 1.55f, inBody.getTranslation()._y, 1.0e-5f );

    // 총구 — 부품 유닛의 월드(무기 오브젝트)에서 계산한다.
    const SocketId muzzleId = rig.getTable().findSocket( hashed_string( "MainHand.Muzzle" ) );
    SW_ASSERT_TRUE( muzzleId != kInvalidSocketId );
    SW_EXPECT_FALSE( rig.getTable().findSocket( hashed_string( "Muzzle" ) ) != kInvalidSocketId ); // 접두어 없는 이름은 몸 소켓뿐
    const float4x4 arrUnitWorld[2] = { float4x4::Identity, float4x4::createTranslation( float3{ 10.0f, 0.0f, 0.0f } ) };
    float4x4       muzzleWorld;
    SW_ASSERT_TRUE( rig.findSocketWorldTransform( hashed_string( "MainHand.Muzzle" ), posed, vector_reference<const float4x4>( arrUnitWorld ), muzzleWorld ) );
    SW_EXPECT_NEAR_EQUAL( 10.0f, muzzleWorld.getTranslation()._x, 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, muzzleWorld.getTranslation()._z, 1.0e-5f );

    // 무기를 바꿔 다시 지어도 같은 이름은 같은 번호 — 새 무기의 총구를 가리킨다.
    CharacterAppearanceSpec spec;
    SW_ASSERT_TRUE( data._database.getPresets().expand( hashed_string( "Hero" ), 0u, data._database.getSlotTable(), data._database.getSets(),
                                                        data._database.getSchemas(), spec ) );
    spec.findSlot( hashed_string( "MainHand" ) )->_itemId = hashed_string( "gun_long" );
    AppearanceResolver::resolve( data._database, spec, resolved );
    SW_ASSERT_TRUE( rig.rebuild( resolved, bindBones, source, &error ) );
    SW_EXPECT_EQUAL( muzzleId, rig.getTable().findSocket( hashed_string( "MainHand.Muzzle" ) ) );
    SW_ASSERT_TRUE( rig.findSocketWorldTransform( hashed_string( "MainHand.Muzzle" ), posed, vector_reference<const float4x4>( arrUnitWorld ), muzzleWorld ) );
    SW_EXPECT_NEAR_EQUAL( 0.9f, muzzleWorld.getTranslation()._z, 1.0e-5f );
}

/**
 * @brief [AppearanceSocketRigTest] 외형의 소켓 덮어쓰기는 몸 소켓을 옮기고(부모 본 · 오프셋), 몸 소켓 에셋이 없는 본을 가리키면 다시 짓기가 실패한다
 */
SW_TEST_CASE( AppearanceSocketRigTest, SocketOverridesMoveBodySocketsAndBadBonesFail )
{
    using Internal = AppearanceSocketRigTestInternal;
    Internal::Data data;
    SW_ASSERT_TRUE_MSG( data.load(), data._database.getReport().joined().c_str() );
    Internal::MemorySocketSource source;
    source.add( "mem/body.sockets.xml", Internal::kBodySocketXml );
    source.add( "mem/gun_short.sockets.xml", Internal::kShortSocketXml );

    ResolvedAppearance resolved;
    SW_ASSERT_TRUE( data.resolve( "LeftyHero", resolved ) );
    const CharacterBoneArray bindBones = Internal::makeBodyBones();
    AppearanceSocketRig      rig;
    string                   error;
    SW_ASSERT_TRUE_MSG( rig.rebuild( resolved, bindBones, source, &error ), error.c_str() );
    const int32 gunPart = Internal::findPart( resolved, "MainHand" );
    SW_ASSERT_TRUE( gunPart >= 0 );
    float4x4 inBody;
    uint32   holderUnit = AppearanceSocketRig::kNoUnit;
    SW_ASSERT_TRUE( rig.computePlacement( resolved._listPart[static_cast<size_t>( gunPart )]._placement, bindBones, inBody, holderUnit ) );
    SW_EXPECT_NEAR_EQUAL( -1.0f, inBody.getTranslation()._x, 1.0e-5f ); // 왼손
    SW_EXPECT_NEAR_EQUAL( 1.1f, inBody.getTranslation()._y, 1.0e-5f );

    Internal::MemorySocketSource badSource;
    badSource.add( "mem/body.sockets.xml", Internal::kBadBodySocketXml );
    badSource.add( "mem/gun_short.sockets.xml", Internal::kShortSocketXml );
    SW_ASSERT_TRUE( data.resolve( "Hero", resolved ) );
    error.clear();
    SW_EXPECT_FALSE( rig.rebuild( resolved, bindBones, badSource, &error ) );
    SW_EXPECT_TRUE( error.find( "hand.missing" ) != string::npos );
}

/**
 * @brief [CharacterPoseTest] 스켈레톤의 레퍼런스 포즈가 본 배열(이름 · 부모 · 로컬 · 모델)이 되고, 모델은 부모를 따라 쌓인다
 */
SW_TEST_CASE( CharacterPoseTest, BindBonesFollowTheReferencePose )
{
    Skeleton      skeleton;
    BoneTransform rootPose;
    rootPose._translation = float3{ 0.0f, 1.0f, 0.0f };
    BoneTransform handPose;
    handPose._translation = float3{ 0.5f, 0.0f, 0.0f };
    const int32 root      = skeleton.addBone( hashed_string( "root" ), -1, rootPose, float4x4::Identity );
    (void)skeleton.addBone( hashed_string( "hand" ), root, handPose, float4x4::Identity );

    CharacterBoneArray bones;
    (void)bones.addBone( hashed_string( "stale" ), -1, float4x4::Identity ); // 지운다
    CharacterPoseUtil::makeBindBones( skeleton, bones );
    SW_ASSERT_EQUAL( 2u, bones.getBoneCount() );
    SW_EXPECT_TRUE( bones._listName[1] == hashed_string( "hand" ) );
    SW_EXPECT_EQUAL( 0, bones._listParent[1] );
    SW_EXPECT_NEAR_EQUAL( 0.5f, bones._listModel[1].getTranslation()._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, bones._listModel[1].getTranslation()._y, 1.0e-6f );
}

/**
 * @brief [CharacterPoseTest] 유닛의 지금 모델 공간 행렬이 본 배열로 옮겨지고, 다른 리그의 본 배열을 받으면 그 유닛의 바인드 본으로 다시 짓는다
 */
SW_TEST_CASE( CharacterPoseTest, UnitPoseIsCopiedAndForeignBonesAreRebuilt )
{
    shared_ptr<Skeleton> skeleton = make_shared<Skeleton>();
    BoneTransform        rootPose;
    rootPose._translation = float3{ 0.0f, 1.0f, 0.0f };
    BoneTransform handPose;
    handPose._translation = float3{ 0.5f, 0.0f, 0.0f };
    const int32 root      = skeleton->addBone( hashed_string( "root" ), -1, rootPose, float4x4::Identity );
    (void)skeleton->addBone( hashed_string( "hand" ), root, handPose, float4x4::Identity );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Unit" ) );
    SW_ASSERT_NOT_NULL( pObject );
    SkeletalMeshComponent* pUnit = pObject->addComponent<SkeletalMeshComponent>();
    SW_ASSERT_NOT_NULL( pUnit );
    pUnit->setSkeleton( skeleton );

    // 다른 리그(이름이 다른 세 본) — 이 유닛의 두 본으로 다시 짓고 모델을 옮긴다.
    CharacterBoneArray bones;
    (void)bones.addBone( hashed_string( "pelvis" ), -1, float4x4::Identity );
    (void)bones.addBone( hashed_string( "spine" ), 0, float4x4::Identity );
    (void)bones.addBone( hashed_string( "neck" ), 1, float4x4::Identity );
    SW_ASSERT_TRUE( CharacterPoseUtil::copyUnitPose( *pUnit, bones ) );
    SW_ASSERT_EQUAL( 2u, bones.getBoneCount() );
    SW_EXPECT_TRUE( bones._listName[0] == hashed_string( "root" ) );

    // 같은 모양이면 다시 짓지 않고 모델 칸만 덮는다 — 엉뚱한 값이 유닛의 값으로 돌아온다.
    bones._listModel[1] = float4x4::createTranslation( float3{ 9.0f, 9.0f, 9.0f } );
    SW_ASSERT_TRUE( CharacterPoseUtil::copyUnitPose( *pUnit, bones ) );
    SW_EXPECT_NEAR_EQUAL( pUnit->getModelSpaceTransforms()[1].getTranslation()._x, bones._listModel[1].getTranslation()._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, bones._listModel[1].getTranslation()._x, 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, bones._listModel[1].getTranslation()._y, 1.0e-6f );
}
