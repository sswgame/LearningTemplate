/**
 * @file AnimationWatches.cpp
 * @brief 애니메이션 시스템의 자동화 감시 값입니다(`Animation.*`) — 군중 공유(`AnimationCrowd`)가 유닛을 어떻게 나눴는지 시나리오가 단언합니다. 프로파일 카운터
 *        `GT.Animation.crowd*Units` 와 같은 값입니다. 감시 값 레지스트리(`Engine/Automation/Watch`)가 아래층이라 애니메이션이 자기 감시 값을 등록합니다.
 *        `Animation.BoneX` 는 대상 오브젝트의 본 하나의 월드 X 입니다(포즈 노드 `Mirror` 가 좌우를 뒤집었는지 본다). `Animation.MorphWeight` 는 모프 타깃 하나의 가중치입니다. `Animation.State` 는 대상 애니메이터의 현재 상태입니다.
 *        개발 명령 `face.expression` · `face.speak` 는 얼굴(`FacialAnimationComponent`)의 표정 · 말하기를, `anim.play <object> <state>` 는 시나리오가 쇼케이스 무대의 캐릭터를 걷게 한다(배포 구성은 등록하지 않는다 — `SW_DEV_COMMAND`).
 */
#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Engine/Animation/Component/FacialAnimationComponent.h"
#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct AnimationWatchesInternal
        {
            /** @brief 지난 프레임에 일한 유닛 가운데 군중 방식이 @p mode 인 수를 담습니다. 씬이 없으면 false 입니다. */
            [[nodiscard]] static bool readCrowdModeCount( const GameObjectManager* pManager, AnimationCrowdMode mode, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<AnimationSystem>().getCrowdModeCount( mode ) );
                return true;
            }

            [[nodiscard]] static bool readCrowdSharedMeshes( const GameObjectManager* pManager, float64& outValue )
            {
                return readCrowdModeCount( pManager, AnimationCrowdMode::Shared, outValue );
            }

            [[nodiscard]] static bool readCrowdSoloMeshes( const GameObjectManager* pManager, float64& outValue )
            {
                return readCrowdModeCount( pManager, AnimationCrowdMode::Solo, outValue );
            }

            [[nodiscard]] static bool readCrowdVertexAnimationMeshes( const GameObjectManager* pManager, float64& outValue )
            {
                return readCrowdModeCount( pManager, AnimationCrowdMode::VertexAnimation, outValue );
            }

            [[nodiscard]] static bool readActiveUnits( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<AnimationSystem>().getActiveMeshCount() );
                return true;
            }

            [[nodiscard]] static bool readPoseEvaluatedUnits( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<AnimationSystem>().getPoseEvaluatedMeshCount() );
                return true;
            }

            /** @brief 씬의 애니메이터마다 블렌드 스페이스가 지금 샘플하는 표본 수를 더합니다. 부하 시나리오가 파라미터가 표본 위가 아니라 삼각형 안에 있는지 본다. */
            [[nodiscard]] static bool readBlendSpaceActiveSamples( SceneIterationKey key, const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                uint32 active = 0;
                pManager->forEachComponentOfType<SkeletalAnimatorComponent>( key, [&active]( const SkeletalAnimatorComponent* pAnimator )
                { active += pAnimator->countActiveBlendSpaceSamples(); } );
                outValue = static_cast<float64>( active );
                return true;
            }

            /** @brief 씬에 등록된 스켈레탈 메시 수입니다. 부하 시나리오가 잴 캐릭터가 다 섰는지 본다. */
            [[nodiscard]] static bool readSkeletalMeshes( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<AnimationSystem>().getSkeletalMeshCount() );
                return true;
            }

            [[nodiscard]] static bool readEvaluatedBones( const GameObjectManager* pManager, float64& outValue )
            {
                if ( pManager == nullptr )
                    return false;
                outValue = static_cast<float64>( pManager->getSubsystem<AnimationSystem>().getPoseEvaluatedBoneCount() );
                return true;
            }

            /** @brief 질의 감시 값 `Animation.BoneX` 입니다. 대상 오브젝트의 스켈레탈 메시에서 본 `key` 의 월드 X(m)입니다(지난 포즈의 모델 공간 × 유닛의 월드). */
            [[nodiscard]] static bool readBoneX( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SkeletalMeshComponent* pMesh = query.findTargetComponent<SkeletalMeshComponent>( "SkeletalMeshComponent", outValue );
                if ( pMesh == nullptr )
                    return false;
                float4x4 boneModel;
                if ( pMesh->findBoneModelTransform( hashed_string( query._key ), boneModel ) == false )
                {
                    outValue.setText( query.describeMissing( "bone" ) );
                    return false;
                }
                // 행 벡터 규약 — 월드 = 모델 × 유닛 월드.
                outValue.setNumber( static_cast<float64>( ( boneModel * pMesh->getWorldMatrix() ).getTranslation()._x ) );
                return true;
            }

            /** @brief 질의 감시 값 `Animation.MorphWeight` 입니다. 대상 오브젝트가 그리는 메시의 모프 타깃 `key` 의 지난 프레임 가중치입니다(표정 · 비즘 · 커브가 더해진 값). */
            [[nodiscard]] static bool readMorphWeight( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SkeletalMeshComponent* pMesh = query.findTargetComponent<SkeletalMeshComponent>( "SkeletalMeshComponent", outValue );
                if ( pMesh == nullptr )
                    return false;
                const optional<uint32> targetIndex = pMesh->findMorphTargetIndex( hashed_string( query._key ) );
                if ( targetIndex.has_value() == false || *targetIndex >= pMesh->getMorphWeights().size() )
                {
                    outValue.setText( query.describeMissing( "morph target" ) );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( pMesh->getMorphWeights()[*targetIndex] ) );
                return true;
            }

            /** @brief 질의 감시 값 `Facial.Speaking` 입니다. 대상 오브젝트의 얼굴이 말하는 중이면 1 입니다. */
            [[nodiscard]] static bool readSpeaking( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const FacialAnimationComponent* pFace = query.findTargetComponent<FacialAnimationComponent>( "FacialAnimationComponent", outValue );
                if ( pFace == nullptr )
                    return false;
                outValue.setNumber( pFace->isSpeaking() ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 이름 붙은 오브젝트의 얼굴입니다. 없으면 nullptr 입니다. */
            [[nodiscard]] static FacialAnimationComponent* findFace( const string& objectName )
            {
                GameObject* pObject = engine::findActiveObject( hashed_string( objectName ) );
                return pObject != nullptr ? pObject->getComponent<FacialAnimationComponent>() : nullptr;
            }

            /** @brief 얼굴이 없다는 답입니다. */
            static string makeNoFaceReply( const string& objectName )
            {
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "no object named '%#' with a FacialAnimationComponent in the active scene", objectName );
                return string( reply.view() );
            }

            /** @brief `face.expression <object> <expression> <weight>` — 얼굴의 표정 가중치를 정합니다. */
            static bool runExpression( const vector<string>& listArgument, string& outReply )
            {
                float32 weight = 0.0f;
                if ( listArgument.size() != 3 || StringUtil::parseFloat( listArgument[2], weight ) == false )
                    return false;
                FacialAnimationComponent* pFace = findFace( listArgument[0] );
                if ( pFace == nullptr )
                {
                    outReply = makeNoFaceReply( listArgument[0] );
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> reply;
                if ( pFace->setExpressionWeight( hashed_string( listArgument[1] ), weight ) )
                    reply.appendFormat( "face.expression %# %# %#", listArgument[0], listArgument[1], weight );
                else
                    reply.appendFormat( "face.expression: '%#' has no expression '%#'", listArgument[0], listArgument[1] );
                outReply = string( reply.view() );
                return true;
            }

            /** @brief `face.speak <object> <audio>` — 얼굴이 음성(비즘 트랙, 없으면 진폭)을 말합니다. */
            static bool runSpeak( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                FacialAnimationComponent* pFace = findFace( listArgument[0] );
                if ( pFace == nullptr )
                {
                    outReply = makeNoFaceReply( listArgument[0] );
                    return false;
                }
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "face.speak %#: %#", listArgument[0], pFace->speak( AssetID{ listArgument[1] } ) ? "speaking" : "refused (see the log)" );
                outReply = string( reply.view() );
                return true;
            }

            /** @brief 질의 감시 값 `Animation.State` 입니다. 대상 오브젝트의 애니메이터가 지금 재생하는 상태(클립) 이름입니다. */
            [[nodiscard]] static bool readState( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const SkeletalAnimatorComponent* pAnimator = query.findTargetComponent<SkeletalAnimatorComponent>( "SkeletalAnimatorComponent", outValue );
                if ( pAnimator == nullptr )
                    return false;
                outValue.setText( pAnimator->getCurrentStateName().c_str() );
                return true;
            }

            /** @brief `anim.play <object> <state>` — 이름 붙은 오브젝트의 애니메이터로 상태(그래프가 없으면 클립)를 반복 재생합니다. */
            static bool runPlay( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 )
                    return false;
                GameObject*                pObject   = engine::findActiveObject( hashed_string( listArgument[0] ) );
                SkeletalAnimatorComponent* pAnimator = pObject != nullptr ? pObject->getComponent<SkeletalAnimatorComponent>() : nullptr;
                if ( pAnimator == nullptr )
                {
                    StringBuilder<constant::buffer::kMax256> builder;
                    builder.appendFormat( "no object named '%#' with a SkeletalAnimatorComponent in the active scene", listArgument[0] );
                    outReply = string( builder.view() );
                    return true;
                }
                if ( pAnimator->play( hashed_string( listArgument[1] ) ) == false )
                {
                    StringBuilder<constant::buffer::kMax256> builder;
                    builder.appendFormat( "anim.play: '%#' has no state or clip '%#'", listArgument[0], listArgument[1] );
                    outReply = string( builder.view() );
                    return true;
                }
                StringBuilder<constant::buffer::kMax256> builder;
                builder.appendFormat( "anim.play %# %#", listArgument[0], listArgument[1] );
                outReply = string( builder.view() );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_AUTOMATION_WATCH( animationCrowdSharedMeshes, "Animation.CrowdSharedMeshes", "Units that shared a crowd bucket's pose last frame", &AnimationWatchesInternal::readCrowdSharedMeshes );
    SW_AUTOMATION_WATCH( animationCrowdSoloMeshes, "Animation.CrowdSoloMeshes", "Crowd-sharing skeletalMeshes that evaluated alone last frame (a state that cannot be shared)",
                         &AnimationWatchesInternal::readCrowdSoloMeshes );
    SW_AUTOMATION_WATCH( animationCrowdVertexAnimationMeshes, "Animation.CrowdVertexAnimationMeshes", "Units drawn with vertex animation last frame (very far crowd skeletalMeshes)",
                         &AnimationWatchesInternal::readCrowdVertexAnimationMeshes );
    SW_AUTOMATION_WATCH( animationActiveUnits, "Animation.ActiveSkeletalMeshes", "Skeletal skeletalMeshes that worked last frame (not resting)", &AnimationWatchesInternal::readActiveUnits );
    SW_AUTOMATION_WATCH( animationPoseEvaluatedUnits, "Animation.PoseEvaluatedSkeletalMeshes", "Skeletal skeletalMeshes that built their own pose last frame (crowd-shared skeletalMeshes do not)",
                         &AnimationWatchesInternal::readPoseEvaluatedUnits );
    SW_AUTOMATION_QUERY_WATCH( animationBoneX, "Animation.BoneX", "World X of the bone named key on the target object's skeletal mesh (m)", &AnimationWatchesInternal::readBoneX, true, true );
    SW_AUTOMATION_QUERY_WATCH( animationMorphWeight, "Animation.MorphWeight", "Weight of the morph target named key on the target object's skeletal mesh", &AnimationWatchesInternal::readMorphWeight, true, true );
    SW_AUTOMATION_QUERY_WATCH( facialSpeaking, "Facial.Speaking", "1 while the target object's face speaks", &AnimationWatchesInternal::readSpeaking, true, false );
    SW_DEV_COMMAND( FaceExpression, "face.expression", "face.expression <object> <expression> <weight>", "Set an expression weight of a named object's face", &AnimationWatchesInternal::runExpression );
    SW_DEV_COMMAND( FaceSpeak, "face.speak", "face.speak <object> <audio>", "Make a named object's face speak a voice asset", &AnimationWatchesInternal::runSpeak );
    SW_AUTOMATION_QUERY_WATCH( animationState, "Animation.State", "State (or clip) the target object's skeletal animator plays now", &AnimationWatchesInternal::readState, true, false );
    SW_DEV_COMMAND( AnimationPlay, "anim.play", "anim.play <object> <state>", "Play a state (or a clip when there is no graph) on a named object's skeletal animator, looping",
                    &AnimationWatchesInternal::runPlay );
    SW_AUTOMATION_WATCH( animationEvaluatedBones, "Animation.EvaluatedBones", "Bones the skeletalMeshes that built their own pose evaluated last frame (bone LOD skips the rest)",
                         &AnimationWatchesInternal::readEvaluatedBones );
    SW_AUTOMATION_WATCH( animationSkeletalMeshes, "Animation.SkeletalMeshes", "Skeletal meshes registered with the scene's animation system", &AnimationWatchesInternal::readSkeletalMeshes );
    SW_AUTOMATION_SCENE_WATCH( animationBlendSpaceActiveSamples, "Animation.BlendSpaceActiveSamples",
                               "Blend space samples the playing states of the scene's animators sample now (a 2D triangulation uses at most three each)",
                               &AnimationWatchesInternal::readBlendSpaceActiveSamples );
} // namespace sw
