#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Network/Message/NetMessage.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/AnimationPlayer.h"
#include "Engine/Animation/Codec/Raw/RawAnimationCodec.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/CharacterDataCache.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSession.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSystem.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/SceneAnimation/AnimationSystem.h"
#include "Engine/Resource/ResourcePaths.h"

#include "EngineTest/AnimationTestUtil.h"

#include "GameFramework/Kits/Feature/Network/NetPairedAnimation/PairedAnimationReplication.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// PairedAnimationReplicationTest — 페어드 애니메이션 세션의 네트워크 복제(GF_NetPairedAnimation): 서버가 만든 상태 메시지로 클라이언트가 같은 세션을 이어 받고,
// 끝 메시지로 중단하며, 복제할 수 없는 세션 · 번호가 없는 오브젝트 · 깨진 본문을 거절한다. 세션 자체는 TestPairedAnimation.cpp.

namespace
{
    struct TestPairedAnimationReplicationInternal
    {
        /** @brief 이진으로 정확한 프레임 길이입니다. */
        static constexpr float32 kStep = 0.125f;

        /** @brief 앵커 `Leader`(1 초), 같이 도는 `Follower`(1 초), 0.5 초에 들어오는 선택 역할 `Prop`(0.5 초). 정렬은 하지 않는다(복원은 정렬하지 않는다). */
        static constexpr const utf8* kXML = R"(<PairedAnimation duration="1">
            <Role name="Leader" anchor="true" required="true" clip="Lead" enterBlend="0" exitState="Idle" exitBlend="0"/>
            <Role name="Follower" required="true" clip="Follow" align="none" enterBlend="0" exitState="Idle" exitBlend="0"/>
            <Role name="Prop" required="false" clip="Late" start="0.5" align="none" enterBlend="0" exitState="Idle" exitBlend="0"/>
        </PairedAnimation>)";

        [[nodiscard]] static bool writeClip( const string& folder, const utf8* pName, uint32 sampleCount, bool bLoop )
        {
            const Skeleton         skeleton = test::makeChainSkeleton( 3 );
            const AnimationRawClip raw      = test::makeChainRawClip( skeleton, sampleCount, 30.0f, 0.0f );
            AnimationClip          clip;
            clip.setName( hashed_string( pName ) );
            clip.setLooping( bLoop );
            if ( clip.compressFrom( raw, RawAnimationCodec::getInstance(), AnimationCodecSettings{}, nullptr ) == false )
                return false;
            return clip.saveToFile( FileUtil::joinPath( folder, StringUtil::toLower( pName ) + string( AnimationClip::kExtension ) ) );
        }

        /** @brief 클립 폴더(`Lead` · `Follow` 1 초, `Late` 0.5 초, 반복 `Idle`)와 에셋 파일을 씁니다. 에셋 경로를 돌려주고, 실패하면 비어 있습니다. */
        static string writeAssets( const utf8* pFolderName, string& outClipFolder )
        {
            outClipFolder     = test::makeTempPath( pFolderName );
            const bool bClips = writeClip( outClipFolder, "Lead", 31, false ) && writeClip( outClipFolder, "Follow", 31, false ) && writeClip( outClipFolder, "Late", 16, false ) &&
                                writeClip( outClipFolder, "Idle", 31, true );
            const string path     = FileUtil::joinPath( outClipFolder, "replicated.pairedanim.xml" );
            const bool   bWritten = bClips && FileUtil::writeTextFile( path, kXML );
            return bWritten ? path : string{};
        }

        static SkeletalAnimatorComponent* addParticipant( GameObjectManager& manager, const utf8* pName, const string& folder )
        {
            GameObject*                pObject   = manager.createGameObject( hashed_string( pName ) );
            SkeletalMeshComponent*     pUnit     = pObject != nullptr ? pObject->addComponent<SkeletalMeshComponent>() : nullptr;
            SkeletalAnimatorComponent* pAnimator = pObject != nullptr ? pObject->addComponent<SkeletalAnimatorComponent>() : nullptr;
            if ( pUnit == nullptr || pAnimator == nullptr )
                return nullptr;
            pUnit->setSkeleton( make_shared<Skeleton>( test::makeChainSkeleton( 3 ) ) );
            pAnimator->setClipFolder( folder );
            pAnimator->setInitialState( "Idle" );
            pAnimator->dispatchBeginPlay();
            return pAnimator;
        }

        static void step( GameObjectManager& manager )
        {
            PairedAnimationSystem::getOrCreate( manager ).runBeforeTick( manager, kStep );
            manager.getSubsystem<AnimationSystem>().evaluate( kStep );
        }

        static float32 getCurrentTime( const SkeletalAnimatorComponent& animator ) { return animator.getGraphPlayer().getPlayer().getCurrentTime(); }
    };
} // namespace

/**
 * @brief [PairedAnimationReplicationTest] 상태 메시지 — 서버가 만든 상태(에셋 경로 · 이름순 오브젝트 번호 · 실제 진입 시각)로 클라이언트가 같은 세션을 이어 받고,
 *        한 프레임 뒤 두 씬의 시각이 같으며, 끝 메시지에 중단한다
 */
SW_TEST_CASE( PairedAnimationReplicationTest, StateMessageRestoresSessionAndEndAbortsIt )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    string       folder;
    const string assetPath = TestPairedAnimationReplicationInternal::writeAssets( "pairedreplication", folder );
    SW_ASSERT_FALSE( assetPath.empty() );

    GameObjectManager          serverManager;
    SkeletalAnimatorComponent* pServerLeader   = TestPairedAnimationReplicationInternal::addParticipant( serverManager, "Leader", folder );
    SkeletalAnimatorComponent* pServerFollower = TestPairedAnimationReplicationInternal::addParticipant( serverManager, "Follower", folder );
    SkeletalAnimatorComponent* pServerProp     = TestPairedAnimationReplicationInternal::addParticipant( serverManager, "Prop", folder );
    SW_ASSERT_TRUE( pServerLeader != nullptr && pServerFollower != nullptr && pServerProp != nullptr );
    PairedAnimationReplicationServer server;
    server.initialize( nullptr, &serverManager );
    SW_EXPECT_EQUAL( 3u, server.getObjectTable().getCount() );

    const shared_ptr<const PairedAnimationDef> def = PairedAnimationDefCache::acquire( assetPath );
    SW_ASSERT_NOT_NULL( def.get() );
    SW_EXPECT_TRUE( def->getSourcePath() == assetPath );
    PairedAnimationSystem& serverSystem = PairedAnimationSystem::getOrCreate( serverManager );
    uint32                 sessionID    = 0;
    SW_ASSERT_TRUE( serverSystem.startSession( serverManager, def,
                                               {
                                                   PairedAnimationBinding{  hashed_string( "Leader" ),   TComponentHandle<SkeletalAnimatorComponent>( pServerLeader )},
                                                   PairedAnimationBinding{hashed_string( "Follower" ), TComponentHandle<SkeletalAnimatorComponent>( pServerFollower )},
                                                   PairedAnimationBinding{    hashed_string( "Prop" ),     TComponentHandle<SkeletalAnimatorComponent>( pServerProp )},
    },
                                               sessionID ) == PairedAnimationStartResult::Started );
    for ( uint32 frameIndex = 0; frameIndex < 5; ++frameIndex )
    {
        TestPairedAnimationReplicationInternal::step( serverManager );
    }
    const PairedAnimationSession* pServerSession = serverSystem.findSession( sessionID );
    SW_ASSERT_NOT_NULL( pServerSession );

    PairedAnimationNetState state{};
    SW_ASSERT_TRUE( server.makeSessionState( *pServerSession, state ) );
    SW_EXPECT_TRUE( state._assetPath == assetPath );
    // 이름순 번호 — Follower 1, Leader 2, Prop 3. 역할 순서(Leader, Follower, Prop)로 싣는다.
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), state._listRoleNetID.size() );
    SW_EXPECT_EQUAL( 2u, state._listRoleNetID[0] );
    SW_EXPECT_EQUAL( 1u, state._listRoleNetID[1] );
    SW_EXPECT_EQUAL( 3u, state._listRoleNetID[2] );
    SW_EXPECT_EQUAL( sessionID, state._snapshot._sessionID );

    NetMessageWriter message;
    SW_ASSERT_TRUE( state.write( message.begin( NetPairedAnimationMessage::kSessionState ) ) );

    // 클라이언트 씬은 같은 이름의 오브젝트를 다른 순서로 만든다 — 번호는 이름순이라 같다.
    GameObjectManager          clientManager;
    SkeletalAnimatorComponent* pClientProp     = TestPairedAnimationReplicationInternal::addParticipant( clientManager, "Prop", folder );
    SkeletalAnimatorComponent* pClientLeader   = TestPairedAnimationReplicationInternal::addParticipant( clientManager, "Leader", folder );
    SkeletalAnimatorComponent* pClientFollower = TestPairedAnimationReplicationInternal::addParticipant( clientManager, "Follower", folder );
    SW_ASSERT_TRUE( pClientLeader != nullptr && pClientFollower != nullptr && pClientProp != nullptr );
    PairedAnimationReplicationClient client;
    client.initialize( &clientManager );
    SW_EXPECT_TRUE( client.handleMessage( 0, message.getBytes() ) == NetHandleResult::Handled );
    SW_EXPECT_EQUAL( 1u, client.getStats()._appliedCount );
    PairedAnimationSystem*        pClientSystem  = PairedAnimationSystem::find( clientManager );
    const PairedAnimationSession* pClientSession = pClientSystem != nullptr ? pClientSystem->findSession( sessionID ) : nullptr;
    SW_ASSERT_NOT_NULL( pClientSession );
    SW_EXPECT_TRUE( pClientSession->hasParticipant( TComponentHandle<SkeletalAnimatorComponent>( pClientFollower ) ) );

    // 클라이언트가 한 프레임 가면 서버가 방금 평가한 시각에 닿는다.
    TestPairedAnimationReplicationInternal::step( clientManager );
    SW_EXPECT_NEAR_EQUAL( TestPairedAnimationReplicationInternal::getCurrentTime( *pServerLeader ), TestPairedAnimationReplicationInternal::getCurrentTime( *pClientLeader ), 1e-5f );
    SW_EXPECT_NEAR_EQUAL( TestPairedAnimationReplicationInternal::getCurrentTime( *pServerProp ), TestPairedAnimationReplicationInternal::getCurrentTime( *pClientProp ), 1e-5f );

    // 같은 상태를 다시 받으면(보정 · 롤백) 옛 세션을 놓고 다시 복원한다.
    SW_EXPECT_TRUE( client.applyState( state ) == PairedAnimationStartResult::Started );
    SW_EXPECT_EQUAL( 2u, client.getStats()._appliedCount );

    // 끝 메시지 — 재생 중인 클라이언트 세션을 중단한다.
    BitWriter& endWriter = message.begin( NetPairedAnimationMessage::kSessionEnd );
    endWriter.writeVarUint( sessionID );
    SW_EXPECT_TRUE( client.handleMessage( 0, message.getBytes() ) == NetHandleResult::Handled );
    SW_EXPECT_EQUAL( 1u, client.getStats()._endAppliedCount );
    pClientSession = pClientSystem->findSession( sessionID );
    SW_ASSERT_NOT_NULL( pClientSession );
    SW_EXPECT_TRUE( pClientSession->getState() == PairedAnimationSessionState::Aborted );
    SW_EXPECT_TRUE( pClientLeader->getCurrentStateName() == hashed_string( "Idle" ) );
}

/**
 * @brief [PairedAnimationReplicationTest] 거절 — 경로 없는 에셋의 세션은 서버가 상태를 만들지 않고, 번호의 오브젝트가 없는 상태 · 역할 수가 다른 상태는 클라이언트가 복원하지 않으며, 깨진 본문은 Malformed 이다
 */
SW_TEST_CASE( PairedAnimationReplicationTest, RefusesUnreplicableSessionsAndBadState )
{
    SW_ASSERT_TRUE( ResourcePaths::initialize() );
    string       folder;
    const string assetPath = TestPairedAnimationReplicationInternal::writeAssets( "pairedreplicationrefuse", folder );
    SW_ASSERT_FALSE( assetPath.empty() );

    GameObjectManager          manager;
    SkeletalAnimatorComponent* pLeader   = TestPairedAnimationReplicationInternal::addParticipant( manager, "Leader", folder );
    SkeletalAnimatorComponent* pFollower = TestPairedAnimationReplicationInternal::addParticipant( manager, "Follower", folder );
    SW_ASSERT_TRUE( pLeader != nullptr && pFollower != nullptr );
    PairedAnimationReplicationServer server;
    server.initialize( nullptr, &manager );

    // 텍스트에서 읽은 에셋은 경로가 없다 — 클라이언트가 같은 에셋을 받을 수 없으니 복제하지 않는다.
    shared_ptr<PairedAnimationDef> textDef = make_shared<PairedAnimationDef>();
    SW_ASSERT_TRUE( textDef->loadFromXMLText( TestPairedAnimationReplicationInternal::kXML, "text.pairedanim.xml" ) );
    SW_EXPECT_TRUE( textDef->getSourcePath().empty() );
    PairedAnimationSystem& system    = PairedAnimationSystem::getOrCreate( manager );
    uint32                 sessionID = 0;
    SW_ASSERT_TRUE( system.startSession( manager, textDef,
                                         {
                                             PairedAnimationBinding{  hashed_string( "Leader" ),   TComponentHandle<SkeletalAnimatorComponent>( pLeader )},
                                             PairedAnimationBinding{hashed_string( "Follower" ), TComponentHandle<SkeletalAnimatorComponent>( pFollower )},
    },
                                         sessionID ) == PairedAnimationStartResult::Started );
    const PairedAnimationSession* pSession = system.findSession( sessionID );
    SW_ASSERT_NOT_NULL( pSession );
    PairedAnimationNetState unused{};
    SW_EXPECT_FALSE( server.makeSessionState( *pSession, unused ) );

    // 번호표에 없는 오브젝트 · 역할 수가 다른 상태.
    PairedAnimationReplicationClient client;
    client.initialize( &manager );
    PairedAnimationNetState state{};
    state._assetPath     = assetPath;
    state._listRoleNetID = { 7u, 1u, 0u };
    state._snapshot      = PairedAnimationSessionSnapshot::makeAtElapsedTime( *textDef, 40, 0.25f );
    SW_EXPECT_TRUE( client.applyState( state ) == PairedAnimationStartResult::ParticipantMissing );
    state._listRoleNetID = { 2u, 1u };
    SW_EXPECT_TRUE( client.applyState( state ) == PairedAnimationStartResult::InvalidSnapshot );
    state._assetPath     = FileUtil::joinPath( folder, "missing.pairedanim.xml" );
    state._listRoleNetID = { 2u, 1u, 0u };
    SW_EXPECT_TRUE( client.applyState( state ) == PairedAnimationStartResult::AssetNotLoaded );
    SW_EXPECT_EQUAL( 3u, client.getStats()._rejectedCount );
    SW_EXPECT_TRUE( system.findSession( 40 ) == nullptr );

    // 깨진 본문 — 상태를 반쯤 자른 메시지.
    state._assetPath = assetPath;
    NetMessageWriter message;
    SW_ASSERT_TRUE( state.write( message.begin( NetPairedAnimationMessage::kSessionState ) ) );
    vector<uint8> truncated = message.getBytes();
    truncated.resize( truncated.size() / 2 );
    SW_EXPECT_TRUE( client.handleMessage( 0, truncated ) == NetHandleResult::Malformed );
}
