/**
 * @file PairedAnimationTestRig.h
 * @brief 페어드 애니메이션 세션 테스트 틀입니다 — 케이스 도메인에 1 초 클립 둘을 쓰고, 애니메이터 둘(Attacker 앵커 · Victim)과 정의 하나로 세션을 시작합니다.
 * @details 역할 클립은 콘텐츠 경로 이름(`testproject/paired/<역할>`)이라 애니메이션 셋 없이 애니메이터가 클립 파일을 읽는다(`SkeletalAnimatorComponent::makeClipPath`).
 *          세션 갱신은 `update( manager, 0 )` 으로 시계를 멈춰 둔다 — 애니메이션 단계 없이 앵커 시각과 예측이 같아 시각 검사가 지나간다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Codec/AnimationCodec.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Tests/AnimationTestUtil.h"
#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSession.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::test
{
    /**
     * @class PairedAnimationTestRig
     * @brief 두 역할(둘 다 필수, 정렬 없음)의 세션을 시작해 둔 픽스처입니다.
     */
    class PairedAnimationTestRig
    {
    public:
        PairedAnimationTestRig()
            : _domain{}
            , _manager{}
            , _pAttacker{ nullptr }
            , _pVictim{ nullptr }
            , _session{}
        {
            writeClip( "attacker" );
            writeClip( "victim" );
            writeClip( "idle" ); // 역할이 나갈 때 재생하는 상태(없으면 `play` 가 오류를 남긴다)
            shared_ptr<PairedAnimationDef> def = make_shared<PairedAnimationDef>();
            SW_REQUIRE_TRUE( def->loadFromXMLText( R"(<PairedAnimation duration="1">
                <Role name="Attacker" anchor="true" required="true" clip="testproject/paired/attacker" enterBlend="0" exitState="testproject/paired/idle" exitBlend="0"/>
                <Role name="Victim" required="true" clip="testproject/paired/victim" align="none" enterBlend="0" exitState="testproject/paired/idle" exitBlend="0"/>
            </PairedAnimation>)",
                                                   "rig" ) );
            _pAttacker = _manager.createGameObject( hashed_string( "Attacker" ) )->addComponent<SkeletalAnimatorComponent>();
            _pVictim   = _manager.createGameObject( hashed_string( "Victim" ) )->addComponent<SkeletalAnimatorComponent>();
            SW_REQUIRE_TRUE( _pAttacker != nullptr && _pVictim != nullptr );
            _manager.mergePendingAdds();
            _session = make_unique<PairedAnimationSession>( 1u, std::move( def ) );
            const vector<PairedAnimationBinding> listBinding{
                PairedAnimationBinding{hashed_string( "Attacker" ), TComponentHandle<SkeletalAnimatorComponent>{ _pAttacker }},
                PairedAnimationBinding{  hashed_string( "Victim" ),   TComponentHandle<SkeletalAnimatorComponent>{ _pVictim }}
            };
            SW_REQUIRE_TRUE( _session->start( _manager, listBinding ) == PairedAnimationStartResult::Started );
        }
        PairedAnimationTestRig( const PairedAnimationTestRig& )            = delete;
        PairedAnimationTestRig& operator=( const PairedAnimationTestRig& ) = delete;

        GameObjectManager&         getManager() { return _manager; }
        PairedAnimationSession&    getSession() { return *_session; }
        SkeletalAnimatorComponent& getAttacker() { return *_pAttacker; }
        SkeletalAnimatorComponent& getVictim() { return *_pVictim; }
        /** @brief 시계를 멈춘 채 세션을 한 번 갱신합니다. */
        void update() { _session->update( _manager, 0.0f ); }

    private:
        /** @brief 사슬 스켈레톤의 1 초 클립(31 표본, 30 Hz)을 `testproject/paired/<name>.animclip` 에 씁니다. */
        void writeClip( const utf8* pName )
        {
            const IAnimationCodec* pCodec = AnimationCodecRegistry::findCodecByName( "raw" );
            SW_REQUIRE_TRUE( pCodec != nullptr );
            AnimationClip clip;
            SW_REQUIRE_TRUE( clip.compressFrom( makeChainRawClip( makeChainSkeleton( 2u ), 31u, 30.0f, 0.5f ), *pCodec, AnimationCodecSettings{}, nullptr ) );
            StringBuilder<constant::buffer::kMax512> pathText;
            pathText.appendFormat( "%#/%#%#", _domain.makeDiskFolder( "paired" ).str(), pName, AnimationClip::kExtension );
            const optional<AbsolutePath> path = AbsolutePath::tryMake( pathText.view() );
            SW_REQUIRE_TRUE( path.has_value() );
            SW_REQUIRE_TRUE( clip.saveToFile( *path ) );
        }

    private:
        ScopedScratchDomain                _domain; ///< 클립 파일이 사는 도메인(매니저 · 세션보다 오래 산다 — 먼저 선언)
        GameObjectManager                  _manager;
        SkeletalAnimatorComponent*         _pAttacker;
        SkeletalAnimatorComponent*         _pVictim;
        unique_ptr<PairedAnimationSession> _session;
    };
} // namespace sw::test
