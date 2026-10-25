#include "pch.h"

#include "Games/Empty/BenchScene.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/XorShift32.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/FacialAnimationComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/MeshInstanceBatch.h"
#include "Engine/Object/Component/3D/PointLightComponent.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/3D/SpotLightComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/ReflectedXMLFile.h"
#include "Engine/UI/Widgets/TextWidget.h"
#include "Engine/UI/World/WidgetComponent.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/MaterialTintCache.h"

#include "Games/Empty/BenchCombatComponent.h"
#include "Games/Empty/BenchMoverComponent.h"

namespace sw
{
    namespace
    {
        /** @brief 배치 값과 튜닝 값의 원본입니다(`BenchSceneSettings`). 코드에는 기본값이 없습니다. */
        constexpr const utf8* kBenchSettingsPath = "game/empty/data/benchscene.xml";
        /** @brief 스트레스가 보관할 머티리얼 인스턴스 상한입니다. 넘으면 새로 붙이는 대신 제거합니다. */
        constexpr size_t kChurnInstanceCap = 512;
        /** @brief 래그돌 군중이 쓰러진 수를 로그로 남기는 프레임 간격입니다. 소수로 정했으므로 `-gv_benchRagdollPeriod` 의 배수와 겹치지 않아, 같은 상태만 기록되는 일이 없습니다. */
        constexpr uint32 kRagdollLogInterval = 97;
        /** @brief 스트레스가 인스턴스에서 켜고 끄는 키워드입니다. 읽는 셰이더는 없고 퍼뮤테이션만 갈립니다. */
        constexpr const utf8* kChurnKeyword = "MATERIAL_BENCH_CHURN";
        /** @brief 스트레스가 흔드는 멀티컴파일 이름입니다. */
        constexpr const utf8* kChurnFogMultiCompile = "FogMode";
        /** @brief 그 멀티컴파일의 선택지입니다. 머티리얼 에셋의 _multiCompiles 와 같아야 합니다. */
        constexpr const utf8* kArrChurnFogOption[] = { "FOG_OFF", "FOG_LINEAR", "FOG_EXP" };
        /**
         * @brief 벤치가 섞어 쓸 도형입니다. `MeshUtil::createPrimitive` 가 아는 이름입니다.
         * @note 큐브가 **첫 번째**여야 합니다. `-gv_benchMeshShapes=1`(기본값)일 때 큐브만 그려야
         *       기존 측정과 스크린샷을 비교할 수 있습니다.
         */
        constexpr const utf8* kArrBenchShape[] = { "Cube", "Sphere", "Cylinder", "Capsule", "Cone" };
    } // namespace

    // ------------------------------------------------------------------------------
    // 벤치 스위치(`-gv_bench*`). 이 파일만 읽으므로 여기서 정의한다.
    // ------------------------------------------------------------------------------
    /**
     * @brief `-gv_benchMeshes=N`: 시작할 때 만들 벤치 큐브 수입니다. 0 이면 만들지 않습니다.
     * @details 이 값이 0 이 아니면 `EmptyGame` 은 `GameConfig` 의 시작 씬 대신 벤치 씬을 만듭니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchMeshes, 0, "시작 시 생성할 벤치 큐브 수 (0=사용 안 함)" );

    /**
     * @brief `-gv_benchCharacters=N`: 스킨드 캐릭터(KayKit 기사) N 명을 한 줄로 스폰하고, 짝수 번째는 Idle, 홀수 번째는 Walking_A 를 재생합니다.
     * @details GPU 스키닝(모프 풀의 스킨 구간, meshskin.hlsl)과 애니메이션 시스템을 실제 에셋으로 실행하는 스위치입니다. `-gv_screenshotFrame` 을 둘
     *          다르게 주고 찍으면 포즈가 달라야 합니다. 개발 빌드 전용입니다. 에셋이 Shooter3D 의 리소스에 있어서 Empty 게임의 배포 팩에는 없습니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchCharacters, 0, "벤치 스킨드 캐릭터 수 (0=사용 안 함, KayKit 기사 · Idle/Walking_A)" );

    /**
     * @brief `-gv_benchCharacterColumns=N`: 벤치 캐릭터를 한 줄이 아니라 N 열 격자로 배치합니다(0 이면 한 줄). 천 명을 한 줄로 세우면 길이가 1.6km 가 됩니다.
     * @details 카메라는 격자의 앞 위쪽에서 내려다봅니다. 앞줄은 크고 뒷줄은 작아서 애니메이션 LOD 의 단계가 고르게 나옵니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchCharacterColumns, 0, "벤치 캐릭터를 N 열 격자로 세운다 (0=한 줄)" );

    /**
     * @brief `-gv_benchCrowdShare=1`: 벤치 캐릭터의 군중 포즈 공유를 켭니다(`SkeletalMeshComponent::setShareCrowdPose`).
     * @details 끄면(기본값) 캐릭터마다 스킨 사본, 포즈, 팔레트를 따로 씁니다. 켜면 같은 상태와 위상의 캐릭터끼리 포즈 하나와 결과 구간 하나를 나눠 쓰고, 아주 먼 캐릭터는 VAT 로 그립니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchCrowdShare, 0, "벤치 캐릭터 군중 포즈 공유 (0=캐릭터마다 사본 · 1=그룹 공유 + 먼 캐릭터 VAT)" );

    /**
     * @brief `-gv_benchCharacterStagger=1`: 벤치 캐릭터의 시작 시각을 인덱스 해시로 흩어 놓습니다(군중이 한 프레임에 맞춰 움직이지 않게 합니다).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchCharacterStagger, 0, "벤치 캐릭터 시작 시각을 흩는다 (0=모두 0 초)" );

    /**
     * @brief `-gv_benchCharacterFollowers=K`: 벤치 캐릭터(몸)마다 리더 포즈로 몸을 따르는 부품 유닛 K 개를 더합니다(모듈 캐릭터의 옷 · 머리 모양).
     * @details 애니메이션 시스템의 의존 레벨(몸 = 레벨 0, 부품 = 레벨 1)과 연결 성분(캐릭터마다 K+1 개)을 재는 스위치입니다. 부품은 메시가 없고 몸과 같은 스켈레톤만 가져
     *          평가 비용만 더합니다. 대조군은 같은 총 유닛 수를 독립 캐릭터로 채웁니다(`-gv_benchCharacters` = N × (1 + K), 부품 없음).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchCharacterFollowers, 0, "벤치 캐릭터마다 리더 포즈 부품 유닛 수 (0=없음, 의존 레벨 · 성분 측정)" );

    /**
     * @brief `-gv_benchCharacterWeapon=1`: 벤치 캐릭터마다 후처리 리그(`knight_weapon.rig.xml`)가 달린 무기 유닛을 더합니다.
     * @details 무기의 리그 대상은 가장 깊은 부품(`-gv_benchCharacterFollowers` 가 0 이면 몸)이라, 부품이 있으면 레벨이 셋(몸 → 부품 → 무기)이 됩니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchCharacterWeapon, 0, "벤치 캐릭터마다 리그 무기 유닛 (0=없음, 가장 깊은 부품이 대상이라 레벨이 하나 깊어진다)" );

    /**
     * @brief `-gv_benchFaces=N`: 테스트 머리 N 개를 한 줄로 스폰합니다(얼굴 애니메이션 확인용). 모두 카메라를 바라보고(시선) 눈을 깜빡입니다.
     *        짝수 번째는 Talk 클립(모프 가중치 커브)을 재생하면서 Happy 표정을 0.6 으로 짓고, 홀수 번째는 테스트 음성을 말합니다(비즘 트랙).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchFaces, 0, "벤치 테스트 머리 수 (0=사용 안 함, 모프 · 표정 · 립싱크 · 깜빡임 · 시선)" );

    /**
     * @brief `-gv_benchCombat=1`: KayKit 스켈레톤 적이 걸으며(발소리 알림, 바닥 재질) 무기 레이캐스트를 맞고(히트 존, 움찔), 180 프레임에 치명적인 피격으로 래그돌이 되고
     *        오른손 소켓의 칼이 물리로 떨어진 뒤 900 프레임에 일어납니다(`BenchCombatComponent`). 로그 `[BenchCombat]` 이 피격, 발소리, 래그돌 상태를 기록합니다.
     * @details 개발 빌드 전용입니다. 에셋이 Shooter3D 의 리소스에 있어서 Empty 게임의 배포 팩에는 없습니다(`gv_benchCharacters` 와 같습니다).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchCombat, 0, "벤치 전투 연출 (0=사용 안 함, KayKit 스켈레톤 — 알림 · 히트 존 · 래그돌 · 칼 떨어뜨리기)" );

    /**
     * @brief `-gv_benchRagdolls=N`: 래그돌을 가진 KayKit 스켈레톤 N 명을 격자로 스폰하고, 프레임마다 일부를 쓰러뜨리거나 일으켜서 틱 멤버십과 애니메이션 유닛의 활성 상태가 계속 바뀌게 합니다.
     * @details 개발 빌드 전용입니다. IK 부하를 보려면 `-gv_benchRig=M` 을 함께 줍니다(기사들이 같은 씬에 섭니다). 캐릭터 한 명은 `-gv_benchRagdollPeriod` 프레임마다 한 번 상태가 바뀝니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchRagdolls, 0, "벤치 래그돌 군중 수 (0=사용 안 함, KayKit 스켈레톤 — 쓰러지고 일어나기를 반복)" );

    /** @brief `-gv_benchRagdollPeriod=P`: 군중의 각 캐릭터가 P 프레임마다 서 있으면 쓰러지고 쓰러져 있으면 일어납니다(시작 프레임은 인덱스로 흩어 놓습니다). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchRagdollPeriod, 60, "래그돌 군중이 상태를 바꾸는 주기 (프레임)" );

    /**
     * @brief `-gv_benchMaterialInstances=1`: 벤치 큐브마다 개별 MaterialInstance 를 줍니다.
     * @details 배치 키가 인스턴스 포인터를 포함하므로 배치가 1개에서 N개로 갈라집니다. 배치와 드로우
     *          경로에 실제 부하를 거는 유일한 방법입니다.
     * @note 기본은 꺼 둡니다. 배치가 하나인 기본 측정과 부하 형태가 다르기 때문입니다. 프레임마다 구조 버퍼 업로드가
     *       여러 번 실행되는 경로여서 DX12 업로드 얼로케이터의 펜스 구간 처리(`updateStructuredBuffer`)를 테스트하기에 좋습니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchMaterialInstances, 0, "벤치 큐브마다 MaterialInstance 부여 (배치를 큐브 수만큼 가른다)" );

    /**
     * @brief `-gv_benchMeshVariants=N`: 벤치가 쓸 **메시 종류 수**입니다. 배치 키에 메시가 들어가므로 곧 배치 수입니다.
     * @details 기본값 1 에서는 모든 큐브가 한 배치로 합쳐져서 드로우 경로(드로우별 상수와 바인딩)를 전혀 측정하지 못합니다.
     *          실제 씬은 늘 여러 메시를 쓰므로, 드로우 경로를 측정하거나 다중 배치 버그를 찾으려면 이 값을 올립니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchMeshVariants, 1, "벤치 메시 종류 수 (= 배치 수, 드로우 경로 측정용)" );

    /**
     * @brief `-gv_benchTransparent=<퍼센트>`: 벤치 큐브 중 이 비율을 투명으로 만듭니다(0 이면 전부 불투명).
     * @details 투명 오브젝트는 불투명 오브젝트와 다른 경로를 탑니다. 배치가 깊이 순서로 나뉘고, 컬링이 압축한 순서를
     *          instancesort 가 되돌리며, 블렌딩 PSO 를 씁니다. 벤치가 전부 불투명이면 그 경로를 한 번도
     *          실행하지 않습니다. 투명 큐브는 **소수의 머티리얼 인스턴스를 나눠 쓰므로** 한 배치에 투명
     *          인스턴스가 여럿 들어갑니다. 그래야 배치 안의 정렬이 실제로 검사됩니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchTransparent, 25, "벤치 큐브 중 투명으로 만들 비율 (퍼센트)" );

    /**
     * @brief `-gv_benchMaterialChurn=N`: 매 프레임 머티리얼 인스턴스 N 개의 **값**을 무작위로 바꿉니다.
     * @details 색과 러프니스를 흔듭니다. 정적인 벤치는 머티리얼 바이트가 한 번 업로드된 뒤 영원히 그대로여서
     *          "바뀐 것만 업로드하는" 경로(`GPUMaterialGroupBuffer::_lastBytes` 비교, 인스턴스 CB 재작성,
     *          구조 버퍼 재업로드)를 **한 번도 실행하지 않습니다.** 이 스위치가 그 경로를 매 프레임 실행합니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchMaterialChurn, 0, "프레임당 값을 무작위로 바꿀 머티리얼 인스턴스 수 (0=사용 안 함)" );

    /**
     * @brief `-gv_benchMaterialChurnAdd=N`: 매 프레임 인스턴스 N 개를 새로 붙이거나 제거합니다.
     * @details 값이 아니라 **집합**을 흔듭니다. 붙이면 배치 키가 갈리고 머티리얼 요소 테이블에 슬롯이 하나
     *          늘어납니다. 제거하면 참조가 사라져서 `freeUnusedMaterialElements` 가 그 슬롯을 회수하고 다음에
     *          재사용합니다. 슬롯을 **옮기지는 않으므로** 인스턴스에 적힌 materialIndex 는 그대로여야 합니다.
     *          제거한 인스턴스는 렌더 패킷이 아직 참조하고 있을 수 있습니다. 그래서 소유권이 마지막 참조와 함께
     *          실제로 사라지는지(상수 버퍼 해제 포함)도 함께 검사합니다.
     * @note 인스턴스 수는 512개에서 멈춘다. 그 뒤로는 새로 붙이는 대신 제거하는 쪽으로 기운다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchMaterialChurnAdd, 0, "프레임당 새로 붙이거나 떼어낼 머티리얼 인스턴스 수 (0=사용 안 함)" );

    /**
     * @brief `-gv_benchMaterialChurnKeyword=N`: N 프레임마다 키워드와 멀티컴파일을 하나 바꿉니다.
     * @warning **셰이더가 다시 컴파일됩니다.** 퍼뮤테이션 해시가 바뀌면 배치 키가 갈리고 PSO 가 새로
     *          만들어지므로 프레임 시간이 크게 튑니다. 성능 측정용이 아니라 **퍼뮤테이션 경로가 살아 있는지**
     *          확인하는 스위치입니다. 값은 크게 줍니다(예: 60 은 1초에 한 번).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchMaterialChurnKeyword, 0, "N 프레임마다 키워드·멀티컴파일을 흔듭니다 (0=사용 안 함)" );

    /**
     * @brief `-gv_benchMeshShapes=N`: 벤치가 섞어 쓸 도형 수입니다(1 은 큐브만, 최대 5).
     * @details 순서는 큐브, 구, 실린더, 캡슐, 원뿔입니다. 큐브 하나만 쓰면 삼각형이 12개뿐이고 면이
     *          축에 정렬되어 있어서 래스터화, 보간, 컬링을 거의 자극하지 못합니다. 곡면을 섞으면 배치마다
     *          **정점 수가 크게 달라져서** 간접 인자, 정점 버퍼 바인딩, 바운드 반경이 전부 다른 값을 씁니다.
     * @note 기본값이 1 인 이유는 **기존 측정과 스크린샷을 그대로 유지하기 위해서**입니다. 도형을 섞으면 이미지가
     *       달라지므로 큐브만 쓴 측정값과 직접 비교할 수 없습니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchMeshShapes, 1, "벤치가 섞어 쓸 도형 수 (1=큐브만 · 최대 5: 큐브·구·실린더·캡슐·원뿔)" );

    /**
     * @brief `-gv_benchMeshMorph=1`: 벤치 도형의 **정점**을 GPU 가 매 프레임 변형합니다.
     * @details 컴퓨트(meshmorph.hlsl)가 레스트 포즈를 읽어 결과 버퍼에 쓰고, 정점 셰이더가 그 결과를
     *          `SV_VertexID` 로 읽습니다. CPU 는 정점을 한 번도 다시 업로드하지 않습니다. `Mesh::setVertices` 로
     *          매 프레임 바꾸면 메시마다 정점 버퍼를 파괴하고 다시 만들어야 하고, 게임 스레드에서 부르는 호출이라
     *          OpenGL 에서는 컨텍스트도 없습니다.
     * @note 기본값이 0 인 이유는 **픽셀 비교 검증을 흔들지 않기 위해서**입니다. 켠 상태의 검증은
     *       `-gv_screenshotFrame` 으로 서로 다른 시각을 찍어 이미지가 실제로 달라지는지로 확인합니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchMeshMorph, 0, "벤치 도형의 정점을 GPU 가 매 프레임 변형합니다 (0=사용 안 함)" );

    /**
     * @brief `-gv_benchAnimate=0`: 벤치의 **모든** 시간 구동 변화를 멈춥니다(기본값 1 은 움직입니다).
     * @details **기하가 있는 씬을 픽셀로 비교하려면 이 스위치가 필요합니다.** 벤치는 두 가지를 시간에 따라
     *          움직입니다. 컴퓨트(instanceanimation)가 만드는 회전과, `update()` 의 사인파가 만드는 상하
     *          이동과 스케일입니다. 둘 다 실제 시계의 델타에서 나오므로 같은 `-gv_screenshotFrame` 으로 두 번
     *          찍어도 이미지가 다릅니다(같은 설정으로 두 번 실행하면 **1.5%** 어긋나서, 셰이더 변경으로 생기는 0.5% 차이를 가립니다).
     *          손대지 않은 대조군이 움직이면 하니스를 먼저 의심하세요. 회전만 멈추는 것으로는 부족합니다(사인파가 남아
     *          0.05% 흔들립니다).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchAnimate, 1, "벤치의 시간 구동 변화(회전·상하 이동·스케일) (0=멈춤, 픽셀 비교 검증용)" );

    /**
     * @brief `-gv_benchUiMarkers=K`: 앞쪽 큐브 K 개에 화면 마커(`WidgetComponent` Screen, 숫자 글자)를 붙입니다(데미지 숫자가 많이 뜨는 경우를 재현합니다).
     * @details 마커는 프레임마다 월드 좌표를 화면에 투영해 배치합니다. 위젯 벤치(`-gv_benchUiWidgets`, 엔진 `UIBenchScreen`)와 함께 `GT.UI.*` 로 측정합니다.
     *          큐브 수(`-gv_benchMeshes`)보다 크면 큐브 수만큼 붙입니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchUiMarkers, 0, "앞쪽 벤치 큐브 K 개에 화면 마커(숫자 글)를 붙인다 (0=사용 안 함)" );

    /**
     * @brief `-gv_benchMovePercent=<퍼센트>`: 프레임마다 위치와 스케일을 다시 쓰는 큐브의 비율입니다(기본값 100 은 전부).
     * @details "일부만 움직이는 씬"을 측정하기 위한 스위치입니다. 트랜스폼 플러시는 더티 루트만 순회하므로 그 이득은
     *          전부 움직이는 벤치에서는 보이지 않습니다. 10 이면 열 개 중 하나만 씁니다(index % 100 < percent).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchMovePercent, 100, "프레임마다 위치·스케일을 다시 쓰는 큐브의 비율 (퍼센트, 기본 100)" );

    /**
     * @brief `-gv_benchInstanced=1`: 큐브를 GameObject 와 SceneComponent 없이 메시 인스턴스 배치(언리얼의 ISM 에 해당)로 만듭니다.
     * @details 같은 격자, 같은 움직임, 같은 시드를 쓰므로 이미지는 같아야 합니다. 플러시와 배치 트랜스폼 쓰기가 사라지는 대신 월드 행렬을
     *          항목에 바로 적습니다. 큐브별 머티리얼 인스턴스와 투명 비율은 이 모드에서 지원하지 않습니다(배치 하나에 메시와 머티리얼이 하나씩이기 때문입니다).
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchInstanced, 0, "1 이면 큐브를 GameObject 없이 메시 인스턴스 배치로 만든다 (기본 0)" );

    /**
     * @brief `-gv_benchLights=N`: 격자 위에 점광과 스포트라이트를 N 개 흩뿌립니다(주 광원은 별개입니다).
     * @details 라이트 수를 늘리면 **픽셀당 루프가 길어집니다**. 디퍼드는 화면 픽셀 수만큼, 포워드는
     *          오버드로를 포함한 픽셀 수만큼 루프를 돕니다. 그래서 두 경로의 라이트 비용이 어떻게 달라지는지를
     *          이 스위치 하나로 측정합니다(`-gv_deferred` 와 함께 씁니다).
     * @note 홀수 번째는 스포트라이트입니다. 점광만 두면 원뿔 감쇠 경로가 한 번도 실행되지 않습니다.
     * @note 기본값이 0 인 이유는 **기존 측정과 스크린샷을 그대로 유지하기 위해서**입니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchLights, 0, "격자에 흩뿌릴 점광·스포트라이트 수 (주광은 별개)" );

    /**
     * @brief `-gv_benchLightRadius=<유닛>`: 벤치 라이트가 닿는 반경입니다(0 이면 격자 간격에서 정합니다).
     * @details 반경은 **비용을 정하는 값**입니다. 크면 한 픽셀에 닿는 라이트가 늘어 루프가 실제로 돌고,
     *          작으면 감쇠가 0 이라 일찍 빠져나갑니다. 타일 컬링의 효과를 측정하려면 이 값을 바꿔 봐야 합니다.
     */
    SW_TEST_GLOBAL_VARIABLE( float32, gv_benchLightRadius, 0.0f, "벤치 라이트 반경 (0=격자 간격에서 정한다)" );

    /**
     * @brief `-gv_benchGround=1`: 격자 아래에 바닥 평면을 깝니다.
     * @details **그림자를 눈으로 확인하려면 그림자를 받는 면이 있어야 합니다.** 큐브만 떠 있으면 그림자는 다른
     *          큐브 위에만 생기는데, 큐브가 서로 떨어져 있어서 거의 보이지 않습니다. 그러면 그림자 투영이
     *          맞는지 이미지로 판단할 수 없습니다.
     * @note 기본값이 0 인 이유는 **기존 측정과 스크린샷을 그대로 유지하기 위해서**입니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchGround, 0, "격자 아래에 바닥 평면을 깝니다 (그림자를 받는 면)" );

    /**
     * @brief `-gv_benchTickMovers=N`: 큐브마다 틱하는 무버 컴포넌트를 N 개 붙이고, 첫 번째가 **틱 안에서** 위치와 스케일을 씁니다.
     * @details 0 이면 게임 스레드가 틱 밖에서 배치로 씁니다. 1 은 게임플레이의 일반적인 패턴(`onTick` 안의 세터,
     *          병렬 틱 중이라 지연 경로를 탑니다)을 측정하고, 2 이상은 한 오브젝트에 틱 컴포넌트가 여럿일 때의 틱 디스패치
     *          비용을 측정합니다(나머지 무버는 틱만 실행합니다). 두 경로의 이미지는 같습니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchTickMovers, 0, "큐브마다 틱 무버 컴포넌트 N 개 — 첫 번째가 틱 안에서 위치를 쓴다 (0=배치 쓰기)" );

    /**
     * @brief `-gv_benchSpawnChurn=N`: 프레임마다 큐브 N 개를 지우고 같은 위치에 새로 만듭니다(총알처럼 스폰이 잦은 게임의 패턴입니다).
     * @details 스폰과 파괴 경로(이름 유일화, 풀, 레지스트리)와 "틱 멤버십이 매 프레임 바뀌는" 틱 레지스트리를 측정합니다. 새 큐브는 같은 메시
     *          종류와 기본 머티리얼을 쓰고, 무버(`-gv_benchTickMovers`)도 함께 붙습니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchSpawnChurn, 0, "프레임마다 큐브 N 개를 지우고 같은 자리에 새로 만든다 (스폰·파괴·틱 레지스트리 측정)" );

    /**
     * @brief `-gv_benchViews=N`: 격자를 둘러보는 캡처 카메라(CCTV) N 개를 둡니다(렌더 텍스처 512², 프레임마다 그립니다).
     * @details 추가 뷰의 비용(컬링 항목, 풀, 그림자 패스, 기록)을 측정합니다. 한 프레임에 그리는 추가 뷰는 `gv_renderViewBudget`(뷰 수)과 `gv_renderViewMegapixelBudget`(픽셀 합)이 제한합니다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchViews, 0, "격자를 둘러보는 캡처 카메라(렌더 텍스처 512²) 수 (0=사용 안 함)" );

    BenchScene::BenchScene()
        : _settings{}
        , _listBenchMesh{}
        , _listInstanceBatch{}
        , _instanceCubeCount{ 0 }
        , _listMeshVariant{}
        , _churnCursor{ 0 }
        , _keyLight{}
        , _listBenchExtra{}
        , _glassMaterial{ nullptr }
        , _listChurnInstance{}
        , _churnRandom{ HashUtil::kGoldenRatio32 }
        , _churnFrame{ 0 }
        , _benchElapsed{ 0.0f }
        , _benchGridSide{ 0 }
        , _probeFrame{ 0 }
        , _bRefreshedCameras{ SW_FALSE }
        , _listRigBody{}
        , _listRagdoll{}
        , _ragdollHalfExtent{ 0.0f }
        , _ragdollFrame{ 0 }
        , _rigLookTarget{}
        , _framedInput{}
        , _framingMode{ FramingMode::Grid }
        , _reserved{ 0 }
    {
    }

    BenchScene::~BenchScene() = default;

    bool BenchScene::spawnFromGlobals()
    {
        // 커맨드라인은 게임에 열려 있지 않지만(CommandLineManager HostOnly), 이 스위치는
        // **이 모듈이 선언한다**(이 파일 위쪽). 그래서 이름으로 조회하지 않고 그대로 읽는다.
        // 주의: 문자열 조회는 이름을 잘못 쓰면 조용히 0 으로 읽힌다.
        const bool bNoBenchmarkRequested = gv_benchMeshes <= 0 && gv_benchCharacters <= 0 && gv_benchFaces <= 0 && getRigCharacterCount() == 0 && gv_benchCombat <= 0 && gv_benchRagdolls <= 0;
        if ( bNoBenchmarkRequested )
            return false;
        // 배치 값과 튜닝 값의 원본은 데이터입니다. 읽지 못해도 코드에 있는 값으로 대신 만들지 않습니다. 대신 만들면 기준 이미지와 다른 이미지가 나옵니다.
        _settings = BenchSceneSettings{};
        if ( ReflectedXMLFile::loadDesc( kBenchSettingsPath, _settings ) == false )
        {
            SW_LOG_ERROR( "[Bench] Failed to read %#, not setting up the bench.", kBenchSettingsPath );
            return false;
        }

        if ( gv_benchMeshes > 0 )
            spawn( static_cast<uint32>( gv_benchMeshes ) );
        if ( gv_benchCharacters > 0 )
            spawnCharacters( static_cast<uint32>( gv_benchCharacters ) );
        if ( gv_benchFaces > 0 )
            spawnFaces( static_cast<uint32>( gv_benchFaces ) );
        if ( getRigCharacterCount() > 0 )
            spawnRigCharacters( getRigCharacterCount() );
        if ( gv_benchCombat > 0 )
            spawnCombat();
        if ( gv_benchRagdolls > 0 )
            spawnRagdolls( static_cast<uint32>( gv_benchRagdolls ) );
        return isActive();
    }

    void BenchScene::despawn()
    {
        SceneManager*      pSceneManager = game::findSubsystem<SceneManager>();
        Scene*             pScene        = ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
        GameObjectManager* pObjects      = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
        // `[[maybe_unused]]` 인 이유: 이 수는 아래 `SW_LOG_INFO` 에만 쓰이는데 그 매크로는
        // Shipping 빌드에서 통째로 사라지므로, 붙이지 않으면 "set but not used" 경고가 납니다.
        [[maybe_unused]] uint32 removedCount = 0;
        if ( pObjects != nullptr )
        {
            // 핸들이 해석되면 그 소유 오브젝트를 지운다. 이미 없으면(씬이 바뀌었으면) 할 일이 없다.
            for ( const TComponentHandle<MeshComponent>& handle : _listBenchMesh )
            {
                if ( Component* pComp = pObjects->resolveComponent( handle ) )
                {
                    pObjects->destroyObject( pComp->getOwner() );
                    ++removedCount;
                }
            }
            if ( Component* pLight = pObjects->resolveComponent( _keyLight ) )
            {
                pObjects->destroyObject( pLight->getOwner() );
                ++removedCount;
            }

            // 흩뿌린 라이트와 바닥도 **벤치가 만든 오브젝트**이므로 같이 정리합니다.
            for ( const ComponentHandle& handle : _listBenchExtra )
            {
                if ( Component* pComp = pObjects->resolveComponent( handle ) )
                {
                    pObjects->destroyObject( pComp->getOwner() );
                    ++removedCount;
                }
            }
        }

        // **만든 수와 정리한 수가 같아야 합니다.** 리로드와 백엔드 교체는 despawn 과 spawn 을 한 쌍으로
        // 실행하는데, 정리되지 않은 오브젝트는 그때마다 한 벌씩 쌓여서 다음 측정을 오염시킵니다.
        SW_LOG_INFO( "[Bench] Cleaned up %# objects (cubes %#, others %#).",
                     removedCount, static_cast<uint32>( _listBenchMesh.size() ),
                     static_cast<uint32>( _listBenchExtra.size() ) );
        _listBenchMesh.clear();
        _listInstanceBatch.clear(); // 소멸자가 레지스트리에서 빠진다
        _instanceCubeCount = 0;
        _listBenchExtra.clear();
        _listRigBody.clear();
        _rigLookTarget = {};
        _listChurnInstance.clear();
        // 정리한 뒤에는 맞출 장면이 없습니다. 격자 모드로 되돌리고 입력도 지금 값으로 맞춰 두어 update 가 헛맞춤을 하지 않게 합니다.
        _framingMode = FramingMode::Grid;
        _framedInput = captureFramingInput( pScene );
        _keyLight    = {};
    }

    Scene* BenchScene::ensureBenchScene()
    {
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
            return nullptr;

        // createScene 은 Scene::initialize 를 부르지 않습니다. 기본 머티리얼 획득, 기본 카메라,
        // 메시 기본값 바인딩이 모두 그 안에 있습니다. 그래서 createScene 으로 만든 씬의 메시는
        // 머티리얼이 없어서, 배치는 만들어져도 화면에는 아무것도 나오지 않습니다.
        Scene* pScene = pSceneManager->getActiveScene();
        if ( pScene == nullptr )
            pScene = pSceneManager->createEmptyActiveScene( "BenchScene" );
        if ( pScene == nullptr )
        {
            SW_LOG_ERROR( "[Bench] Failed to create the scene." );
            return nullptr;
        }
        pScene->ensureDefaultCameras();
        return pScene;
    }

    bool BenchScene::createMeshVariants( uint32 variantCount, uint32 shapeCount )
    {
        // 같은 기하를 여러 객체로 만들면 배치만 갈립니다. 도형을 섞으면 배치마다 정점 수가 달라져서
        // 간접 인자, 정점 버퍼 바인딩, 바운드 반경이 전부 다른 값을 씁니다.
        _listMeshVariant.clear();
        _listMeshVariant.reserve( variantCount );
        for ( uint32 variantIndex = 0; variantIndex < variantCount; ++variantIndex )
        {
            const utf8*      pShapeID = kArrBenchShape[variantIndex % shapeCount];
            shared_ptr<Mesh> variant  = MeshUtil::createPrimitive( pShapeID, PrimitiveVertexColor::Diagnostic );
            if ( variant == nullptr )
            {
                SW_LOG_ERROR( "[Bench] Failed to create shape '%#'.", pShapeID );
                return false;
            }
            // GPU 모프는 옵트인입니다. 유니티의 vertexBufferTarget 옵트인에 해당합니다. 켠 메시만 풀에 들어갑니다.
            if ( gv_benchMeshMorph != 0 )
                variant->setGPUMorphEnabled( true );
            _listMeshVariant.push_back( std::move( variant ) );
        }
        return true;
    }

    void BenchScene::createTransparentMaterials( vector<shared_ptr<MaterialInstance>>& outList )
    {
        // 투명은 **별도 머티리얼 에셋**으로 만듭니다. 블렌드 모드가 머티리얼의 속성이고, 알파 사용 여부가
        // 셰이더 퍼뮤테이션(MATERIAL_BLEND_TRANSLUCENT)을 가르기 때문입니다. 주의: 메시에 플래그를 설정하면
        // 불투명으로 컴파일된 머티리얼을 블렌딩으로 그리는 어긋난 상태가 됩니다.
        //
        // 인스턴스는 **소수만 만들어 돌려 씁니다.** 큐브마다 하나씩 주면 배치가 인스턴스마다 갈라져서 한
        // 배치에 투명 인스턴스가 하나뿐이고, 그러면 배치 안의 정렬(instancesort 가 되돌리는 그 순서)이
        // 한 번도 검사되지 않습니다.
        constexpr uint32 kTransparentMaterialCount = 3;
        outList.assign( kTransparentMaterialCount, nullptr );
        if ( _glassMaterial == nullptr )
        {
            _glassMaterial = Material::create();
            if ( _glassMaterial->loadFromFile( _settings._glassMaterial.c_str() ) == false )
            {
                SW_LOG_WARNING( "[Bench] Failed to read the transparent material - skipping the transparent cubes." );
                _glassMaterial.reset();
            }
        }
        for ( uint32 slot = 0; slot < kTransparentMaterialCount && _glassMaterial != nullptr; ++slot )
        {
            shared_ptr<MaterialInstance> instance = MaterialInstance::create( _glassMaterial.get() );
            const float4                 tint     = makeBenchColor( slot * 977u + 13u );
            // 알파를 눈에 띄게 낮춥니다. 1.0 에 가까우면 블렌딩이 적용됐는지 이미지로 구분할 수 없습니다.
            const float32 alpha = _settings._transparentAlphaBase + _settings._transparentAlphaStep * static_cast<float32>( slot );
            instance->setVectorParameter( hashed_string( kMaterialColorParameter ), float4{ tint._x, tint._y, tint._z, alpha } );
            _listChurnInstance.push_back( instance );
            outList[slot] = std::move( instance );
        }
    }

    void BenchScene::spawnInstancedGrid( GameObjectManager* pObjects, Material* pSceneMaterial, uint32 meshCount, uint32 meshVariantCount, uint32 side, float32 origin )
    {
        // 씬 컴포넌트 없이 메시 종류마다 배치 하나를 만듭니다. 큐브 i 는 배치 i % 종류수 의 항목 i / 종류수 입니다. 격자와 시드는 컴포넌트
        // 경로와 같으므로 이미지가 같아야 합니다(픽셀 비교로 검증합니다). 큐브별 머티리얼 인스턴스와 투명 비율은 이 모드에 없습니다.
        _listInstanceBatch.clear();
        _listInstanceBatch.reserve( meshVariantCount );
        for ( uint32 variantIndex = 0; variantIndex < meshVariantCount; ++variantIndex )
        {
            const uint32 entryCount = ( meshCount - variantIndex + meshVariantCount - 1 ) / meshVariantCount;
            _listInstanceBatch.push_back( sw::make_shared<MeshInstanceBatch>( _listMeshVariant[variantIndex], pSceneMaterial, nullptr, entryCount, MeshInstanceWriter::CPU ) );
        }
        for ( uint32 index = 0; index < meshCount; ++index )
        {
            const uint32       col = index % side;
            const uint32       row = index / side;
            const float3       position{ origin + static_cast<float32>( col ) * _settings._cubeSpacing, 0.0f, origin + static_cast<float32>( row ) * _settings._cubeSpacing };
            MeshInstanceBatch& batch = *_listInstanceBatch[index % meshVariantCount];
            const uint32       entry = index / meshVariantCount;
            batch.setWorld( entry, float4x4::makeTrs( position, float3{ 0.0f, 0.0f, 0.0f }, float3{ 1.0f, 1.0f, 1.0f } ) );
            if ( gv_benchAnimate != 0 )
                batch.setSpinSeed( entry, index + 1u );
        }
        for ( const shared_ptr<MeshInstanceBatch>& batch : _listInstanceBatch )
        {
            pObjects->getPrimitiveRegistry().addInstanceBatch( batch.get() );
        }
        _instanceCubeCount = meshCount;
    }

    void BenchScene::spawnCubeGrid( GameObjectManager* pObjects, Material* pSceneMaterial, uint32 meshCount, uint32 side, float32 origin, uint32 transparentPercent,
                                    const vector<shared_ptr<MaterialInstance>>& listTransparentMaterial )
    {
        // 큐브별 머티리얼 인스턴스는 배치를 가르는 부하이므로 기본 측정에서는 꺼 둡니다(`gv_benchMaterialInstances`).
        const bool bPerCubeMaterial = ( gv_benchMaterialInstances != 0 );
        const bool bHasTransparent  = listTransparentMaterial.empty() == false && listTransparentMaterial[0] != nullptr;
        for ( uint32 index = 0; index < meshCount; ++index )
        {
            MeshComponent* pMesh = spawnCube( pObjects, pSceneMaterial, index, side, origin );
            if ( pMesh == nullptr )
                continue;
            _listBenchMesh.push_back( TComponentHandle<MeshComponent>{ pMesh } );

            // 큐브마다 자기 머티리얼 인스턴스를 줍니다. 색이 달라지는 것도 목적이지만, 배치 키가
            // 인스턴스 포인터를 포함하므로 배치가 1개에서 N개로 갈라집니다. 그래야 배치와 드로우 경로가
            // 실제 부하를 받습니다(전부 같은 인스턴스면 drawInstanced 한 번으로 끝납니다).
            // 기본 측정과 부하 형태가 다르므로 옵트인으로 둡니다.
            if ( bPerCubeMaterial && pSceneMaterial != nullptr )
            {
                shared_ptr<MaterialInstance> instance = MaterialInstance::create( pSceneMaterial );
                instance->setVectorParameter( hashed_string( kMaterialColorParameter ), makeBenchColor( index ) );
                _listChurnInstance.push_back( instance );
                pMesh->setMaterialInstance( std::move( instance ) );
            }
            // 일부를 투명으로 만듭니다. 블렌드 모드가 배치를 가르고, 컬링이 압축한 순서를 instancesort 가
            // 깊이순으로 되돌립니다. 투명 큐브끼리는 머티리얼 인스턴스를 나눠 쓰므로 한 배치에 여럿 들어갑니다.
            if ( isBenchTransparent( index, transparentPercent ) && bHasTransparent )
            {
                // 블렌드 모드는 머티리얼이 정하므로 메시에는 따로 설정하지 않습니다.
                pMesh->setMaterial( _glassMaterial.get() );
                pMesh->setMaterialInstance( listTransparentMaterial[index % listTransparentMaterial.size()] );
            }
        }
    }

    void BenchScene::finishGrid( Scene* pScene, uint32 side )
    {
        const float32 halfExtent = halfExtentOf( side, _settings._cubeSpacing );
        spawnLight( pScene, halfExtent );
        spawnBenchLights( pScene, halfExtent );
        spawnGround( pScene, halfExtent );
        _benchGridSide = side;
        applyFraming( pScene, FramingMode::Grid );
        spawnBenchViews( pScene, halfExtent );
    }

    void BenchScene::spawn( uint32 meshCount )
    {
        Scene* pScene = ensureBenchScene();
        if ( pScene == nullptr )
            return;

        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
            return;

        Material* pSceneMaterial = pScene->getMaterial();
        if ( pSceneMaterial == nullptr )
            SW_LOG_WARNING( "[Bench] The scene has no default material - cubes may be invisible." );

        // 메시 종류 수입니다. 배치 키에 메시가 들어가므로 종류 수가 곧 **배치 수**입니다. 1 이면 배치가 하나로 합쳐져서
        // 드로우 경로(드로우별 상수와 바인딩)를 전혀 측정하지 못합니다. 실제 씬은 늘 여러 메시를 씁니다.
        uint32 meshVariantCount = static_cast<uint32>( MathUtil::max( 1, gv_benchMeshVariants ) );
        meshVariantCount        = MathUtil::min( meshVariantCount, meshCount );

        // 도형을 몇 종 섞을지 정합니다. 1 이면 큐브만 씁니다(기존 측정과 스크린샷을 유지합니다).
        const uint32 shapeCount = static_cast<uint32>(
            MathUtil::clamp( gv_benchMeshShapes, 1, static_cast<int32>( SW_COUNT_OF( kArrBenchShape ) ) ) );
        if ( createMeshVariants( meshVariantCount, shapeCount ) == false )
            return;

        // 정사각에 가까운 격자로 흩어 놓습니다. 전부 같은 메시와 머티리얼이라 배치가 하나로 합쳐지는데,
        // 이것이 지금 렌더러의 기본 경로(배치당 drawInstanced 한 번)를 측정하는 조건입니다.
        const uint32  side   = static_cast<uint32>( MathUtil::sqrt( static_cast<float32>( meshCount ) ) ) + 1;
        const float32 origin = -0.5f * static_cast<float32>( side - 1 ) * _settings._cubeSpacing;

        _listBenchMesh.clear();
        _listBenchMesh.reserve( meshCount );
        _listBenchExtra.clear();

        const uint32 transparentPercent =
            ( gv_benchTransparent > 0 ) ? static_cast<uint32>( MathUtil::min( gv_benchTransparent, 100 ) ) : 0u;
        vector<shared_ptr<MaterialInstance>> listTransparentMaterial;
        if ( transparentPercent > 0 )
            createTransparentMaterials( listTransparentMaterial );

        if ( gv_benchInstanced != 0 )
        {
            if ( gv_benchMaterialInstances != 0 || transparentPercent > 0 )
                SW_LOG_WARNING( "[Bench] -gv_benchInstanced=1 does not support per-cube material instances or a transparent ratio - ignoring them." );
            spawnInstancedGrid( pObjects, pSceneMaterial, meshCount, meshVariantCount, side, origin );
            finishGrid( pScene, side );
            SW_LOG_INFO( "[Bench] In scene '%#', created %# cubes in %# instanced batches (%#x%# grid, no GameObject).",
                         pScene->getName(), meshCount, meshVariantCount, side, side );
            return;
        }

        spawnCubeGrid( pObjects, pSceneMaterial, meshCount, side, origin, transparentPercent, listTransparentMaterial );
        finishGrid( pScene, side );

        SW_LOG_INFO( "[Bench] %# mesh variants (= batch count), %# shapes. Change them with -gv_benchMeshVariants and -gv_benchMeshShapes.",
                     meshVariantCount, shapeCount );
        SW_LOG_INFO( "[Bench] In scene '%#', created %# cubes in a %#x%# grid.",
                     pScene->getName(), static_cast<uint32>( _listBenchMesh.size() ), side, side );
    }

    void BenchScene::spawnCharacters( uint32 characterCount )
    {
        Scene* pScene = ensureBenchScene();
        if ( pScene == nullptr )
            return;
        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
            return;

        const uint32  columns = ( gv_benchCharacterColumns > 0 ) ? static_cast<uint32>( gv_benchCharacterColumns ) : characterCount;
        const float32 origin  = -0.5f * static_cast<float32>( MathUtil::min( columns, characterCount ) - 1 ) * _settings._characterSpacing;
        for ( uint32 index = 0; index < characterCount; ++index )
        {
            StringBuilder<constant::kMaxBuffer64> nameBuilder;
            nameBuilder.append( "BenchCharacter_" ).append( index );
            GameObject* pObject = pObjects->createGameObject( hashed_string( nameBuilder.c_str(), nameBuilder.size() ) );
            if ( pObject == nullptr )
                continue;
            SkeletalMeshComponent*     pMesh     = pObject->addComponent<SkeletalMeshComponent>();
            SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
            if ( pMesh == nullptr || pAnimator == nullptr )
                continue;
            pMesh->setShareCrowdPose( gv_benchCrowdShare != 0 );
            pMesh->setMeshID( _settings._knightMesh.c_str() );
            pMesh->setMaterialPath( _settings._knightMaterial );
            pMesh->setSkeletonPath( _settings._knightSkeleton.c_str() );
            pMesh->resolveRenderAssets();
            // 기사는 +Z 방향을 봅니다. 카메라(-Z 쪽)를 보도록 돌립니다. 격자로 배치하면 열과 행으로 펼치고, 행은 +Z 쪽으로 멀어집니다.
            const uint32 column = index % columns;
            const uint32 row    = index / columns;
            pMesh->setLocalPosition( float3{ origin + static_cast<float32>( column ) * _settings._characterSpacing, 0.0f, static_cast<float32>( row ) * _settings._characterSpacing } );
            pMesh->setLocalRotation( float3{ 0.0f, MathUtil::kPi, 0.0f } );
            pAnimator->setClipFolder( _settings._knightClips.c_str() );
            pAnimator->setInitialState( ( index % 2u ) == 0 ? _settings._characterEvenClip.c_str() : _settings._characterOddClip.c_str() );
            if ( gv_benchCharacterStagger != 0 )
            {
                // 결정적 해시를 씁니다. 실행마다 같은 이미지여야 비교할 수 있습니다.
                const uint32 hash = ( index + 1u ) * 2654435761u;
                pAnimator->setInitialTime( static_cast<float32>( hash >> 16 ) / 65535.0f * _settings._characterStaggerSeconds );
            }
            _listBenchExtra.push_back( pMesh->getHandle() );
            spawnCharacterParts( *pObjects, index, pMesh );
        }

        const float32 halfExtent = computeCharacterHalfExtent( characterCount );
        spawnLight( pScene, halfExtent );
        spawnGround( pScene, halfExtent );
        applyFraming( pScene, FramingMode::Character );
        SW_LOG_INFO( "[Bench] In scene '%#', placed %# skinned characters (%#).", pScene->getName(), characterCount, _settings._knightMesh.c_str() );
    }

    void BenchScene::spawnCharacterParts( GameObjectManager& objects, uint32 characterIndex, SkeletalMeshComponent* pBody )
    {
        const uint32 followerCount = static_cast<uint32>( MathUtil::max( gv_benchCharacterFollowers, 0 ) );
        const bool   bWeapon       = gv_benchCharacterWeapon != 0;
        if ( pBody == nullptr || ( followerCount == 0 && bWeapon == false ) )
            return;
        SkeletalMeshComponent* pDeepest = pBody; // 무기의 리그 대상이 되는 가장 깊은 유닛
        for ( uint32 followerIndex = 0; followerIndex < followerCount; ++followerIndex )
        {
            StringBuilder<constant::kMaxBuffer64> nameBuilder;
            nameBuilder.append( "BenchCharacterPart_" ).append( characterIndex ).append( "_" ).append( followerIndex );
            GameObject*            pPart     = objects.createGameObject( hashed_string( nameBuilder.c_str(), nameBuilder.size() ) );
            SkeletalMeshComponent* pFollower = ( pPart != nullptr ) ? pPart->addComponent<SkeletalMeshComponent>() : nullptr;
            if ( pFollower == nullptr )
                continue;
            pFollower->setSkeletonPath( _settings._knightSkeleton.c_str() );
            pFollower->setLeaderPose( pBody );
            _listBenchExtra.push_back( pFollower->getHandle() );
            pDeepest = pFollower;
        }
        if ( bWeapon == false )
            return;
        StringBuilder<constant::kMaxBuffer64> weaponName;
        weaponName.append( "BenchCharacterWeapon_" ).append( characterIndex );
        GameObject*            pWeapon     = objects.createGameObject( hashed_string( weaponName.c_str(), weaponName.size() ) );
        SkeletalMeshComponent* pWeaponUnit = ( pWeapon != nullptr ) ? pWeapon->addComponent<SkeletalMeshComponent>() : nullptr;
        PoseModifierComponent* pWeaponRig  = ( pWeapon != nullptr ) ? pWeapon->addComponent<PoseModifierComponent>() : nullptr;
        if ( pWeaponUnit == nullptr || pWeaponRig == nullptr )
            return;
        pWeaponUnit->setSkeleton( createSingleBoneSkeleton() ); // 리그는 데이터 스켈레톤을 받은 유닛에만 묶인다
        pWeaponRig->setRigPath( _settings._rigWeaponRig.c_str() );
        pWeaponRig->bindUnit( "Body", pDeepest );
        _listBenchExtra.push_back( pWeaponUnit->getHandle() );
    }

    void BenchScene::spawnCombat()
    {
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
            return;
        Scene* pScene = pSceneManager->getActiveScene();
        if ( pScene == nullptr )
            pScene = pSceneManager->createEmptyActiveScene( "BenchScene" );
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        pScene->ensureDefaultCameras();
        vector<GameObjectHandle> listObject;
        BenchCombatComponent::spawnScene( *pScene, _settings, listObject );
        for ( const GameObjectHandle& handle : listObject )
        {
            GameObject* pObject = pScene->getObjectManager()->resolveGameObject( handle );
            Component*  pFirst  = ( pObject != nullptr && pObject->getComponents().empty() == false ) ? pObject->getComponents().front() : nullptr;
            if ( pFirst != nullptr )
                _listBenchExtra.push_back( pFirst->getHandle() );
        }
        spawnLight( pScene, _settings._combatHalfExtent );
        applyFraming( pScene, FramingMode::Combat );
        SW_LOG_INFO( "[Bench] In scene '%#', set up the combat staging (KayKit skeletons, swords, floor).", pScene->getName() );
    }

    void BenchScene::spawnRagdolls( uint32 ragdollCount )
    {
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
            return;
        Scene* pScene = pSceneManager->getActiveScene();
        if ( pScene == nullptr )
            pScene = pSceneManager->createEmptyActiveScene( "BenchScene" );
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        pScene->ensureDefaultCameras();
        GameObjectManager* pObjects = pScene->getObjectManager();

        const uint32  columns    = static_cast<uint32>( MathUtil::ceil( MathUtil::sqrt( static_cast<float32>( ragdollCount ) ) ) );
        const uint32  rows       = ( ragdollCount + columns - 1u ) / columns;
        const float32 spacing    = _settings._characterSpacing;
        const float32 originX    = -0.5f * static_cast<float32>( columns - 1u ) * spacing;
        const float32 halfExtent = MathUtil::max( _settings._characterMinHalfExtent, 0.5f * static_cast<float32>( MathUtil::max( columns, rows ) ) * spacing );

        // 바닥입니다. 래그돌이 쓰러져 닿는 정적 강체이고, 화면에 보이는 면은 spawnGround 가 만듭니다.
        if ( GameObject* pFloor = pObjects->createGameObject( hashed_string( "RagdollFloor" ) ) )
        {
            if ( RigidBodyComponent* pBody = pFloor->addComponent<RigidBodyComponent>() )
            {
                PhysicsShapeDesc3D box;
                box._halfExtents   = float3{ halfExtent + spacing, 0.5f, halfExtent + spacing };
                box._localPosition = float3{ 0.0f, -0.5f, halfExtent - spacing };
                pBody->setShape( box );
                pBody->setBodyType( PhysicsBodyType::Static );
                pBody->setLayer( hashed_string( "Static" ) );
                pBody->setMaterial( hashed_string( "Stone" ) );
                _listBenchExtra.push_back( pBody->getHandle() );
            }
        }

        for ( uint32 index = 0; index < ragdollCount; ++index )
        {
            StringBuilder<constant::kMaxBuffer64> nameBuilder;
            nameBuilder.append( "BenchRagdoll_" ).append( index );
            const float3 position{ originX + static_cast<float32>( index % columns ) * spacing, 0.0f, static_cast<float32>( index / columns ) * spacing };
            GameObject*  pEnemy = BenchCombatComponent::spawnEnemy( *pObjects, _settings, hashed_string( nameBuilder.c_str(), nameBuilder.size() ), position );
            if ( RagdollComponent* pRagdoll = pEnemy != nullptr ? pEnemy->getComponent<RagdollComponent>() : nullptr )
            {
                _listRagdoll.push_back( TComponentHandle<RagdollComponent>{ pRagdoll } );
                _listBenchExtra.push_back( pRagdoll->getHandle() );
            }
        }

        spawnLight( pScene, halfExtent );
        spawnGround( pScene, halfExtent );
        _ragdollHalfExtent = halfExtent;
        applyFraming( pScene, FramingMode::Ragdoll );
        SW_LOG_INFO( "[Bench] In scene '%#', placed a ragdoll crowd of %# in a %#x%# grid (period %# frames).", pScene->getName(), ragdollCount, columns, rows, gv_benchRagdollPeriod );
    }

    void BenchScene::updateRagdollChurn( GameObjectManager* pObjects )
    {
        if ( _listRagdoll.empty() )
            return;
        const uint32 period = static_cast<uint32>( MathUtil::max( gv_benchRagdollPeriod, 1 ) );
        const uint32 frame  = _ragdollFrame++;
        if ( frame % kRagdollLogInterval == kRagdollLogInterval - 1u )
        {
            // 상태가 실제로 오가는지 본다(기상이 계속 실패하면 모두 래그돌에 머문다).
            [[maybe_unused]] uint32 ragdollCount = 0;
            for ( const TComponentHandle<RagdollComponent>& handle : _listRagdoll )
            {
                const RagdollComponent* pRagdoll = pObjects->resolveComponent( handle );
                ragdollCount += ( pRagdoll != nullptr && pRagdoll->getState() == RagdollState::Ragdoll ) ? 1u : 0u;
            }
            SW_LOG_INFO( "[Bench] frame %# fallen %#/%#", frame + 1u, ragdollCount, static_cast<uint32>( _listRagdoll.size() ) );
        }
        for ( size_t index = 0; index < _listRagdoll.size(); ++index )
        {
            if ( ( frame + static_cast<uint32>( index ) ) % period != 0u )
                continue;
            RagdollComponent* pRagdoll = pObjects->resolveComponent( _listRagdoll[index] );
            if ( pRagdoll == nullptr )
                continue;
            // 서 있으면 쓰러뜨리고, 쓰러져 있으면 일으킨다(물리 자세를 읽기 전이라 실패하면 다음 주기에 다시). 일어나는 중이면 끝날 때까지 둔다.
            if ( pRagdoll->getState() == RagdollState::Animated )
                pRagdoll->startRagdoll( float3{}, invalid_index::kInt32 ); // 충격량 없이 쓰러진다
            else if ( pRagdoll->getState() == RagdollState::Ragdoll )
                (void)pRagdoll->getUp();
        }
    }

    void BenchScene::spawnFaces( uint32 faceCount )
    {
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        if ( pSceneManager == nullptr )
            return;
        Scene* pScene = pSceneManager->getActiveScene();
        if ( pScene == nullptr )
            pScene = pSceneManager->createEmptyActiveScene( "BenchScene" );
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
        {
            SW_LOG_ERROR( "[Bench] Failed to create the scene." );
            return;
        }
        pScene->ensureDefaultCameras();
        GameObjectManager* pObjects = pScene->getObjectManager();
        // 머리는 원점에 서서 +Z 방향을 봅니다. 카메라(-Z 쪽)를 보도록 돌리고, 눈은 카메라 높이의 앞쪽 지점을 바라봅니다.
        const float32 origin = -0.5f * static_cast<float32>( faceCount - 1 ) * _settings._faceSpacing;
        const float3  lookAt{ 0.0f, _settings._faceHeight, -computeFaceCameraDistance( faceCount ) };
        for ( uint32 index = 0; index < faceCount; ++index )
        {
            StringBuilder<constant::kMaxBuffer64> nameBuilder;
            nameBuilder.append( "BenchFace_" ).append( index );
            GameObject* pObject = pObjects->createGameObject( hashed_string( nameBuilder.c_str(), nameBuilder.size() ) );
            if ( pObject == nullptr )
                continue;
            SkeletalMeshComponent* pMesh = pObject->addComponent<SkeletalMeshComponent>();
            if ( pMesh == nullptr )
                continue;
            pMesh->setMeshID( _settings._faceMesh.c_str() );
            pMesh->setSkeletonPath( _settings._faceSkeleton.c_str() );
            pMesh->resolveRenderAssets();
            pMesh->setLocalPosition( float3{ origin + static_cast<float32>( index ) * _settings._faceSpacing, 0.0f, 0.0f } );
            pMesh->setLocalRotation( float3{ 0.0f, MathUtil::kPi, 0.0f } );
            const bool bTalkClip = ( index % 2 ) == 0;
            if ( bTalkClip )
            {
                SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
                if ( pAnimator != nullptr )
                {
                    pAnimator->setClipFolder( _settings._faceClips.c_str() );
                    pAnimator->setInitialState( _settings._faceTalkClip.c_str() );
                }
            }
            FacialAnimationComponent* pFace = pObject->addComponent<FacialAnimationComponent>();
            if ( pFace == nullptr )
                continue;
            pFace->setFacialRigPath( _settings._faceRig.c_str() );
            pFace->setLookAtTarget( lookAt );
            if ( bTalkClip )
                (void)pFace->setExpressionWeight( hashed_string( _settings._faceExpression.c_str() ), _settings._faceExpressionWeight );
            else
                (void)pFace->speak( _settings._faceVoice.c_str() );
            _listBenchExtra.push_back( pMesh->getHandle() );
        }

        spawnLight( pScene, _settings._faceLightHalfExtent );
        applyFraming( pScene, FramingMode::Face );
        SW_LOG_INFO( "[Bench] In scene '%#', spawned %# test heads (%#).", pScene->getName(), faceCount, _settings._faceMesh.c_str() );
    }

    float32 BenchScene::computeFaceCameraDistance( uint32 faceCount ) const
    {
        // 머리 줄(폭과 여유)이 화면 폭을 채우는 거리입니다. 기본 카메라 시야각과 16:9 비율에서 측정한 근삿값입니다.
        const float32 rowWidth = static_cast<float32>( MathUtil::max( faceCount, 1u ) ) * _settings._faceSpacing;
        return MathUtil::max( _settings._faceCameraMinDistance, rowWidth * _settings._faceCameraWidthMargin );
    }

    void BenchScene::frameFaceCameras( Scene* pScene )
    {
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        const float32 distance = computeFaceCameraDistance( static_cast<uint32>( MathUtil::max( gv_benchFaces, 1 ) ) );
        for ( CameraComponent* pCamera : pScene->getObjectManager()->getCameraRegistry().getAll() )
        {
            pCamera->setLocalPosition( float3{ 0.0f, _settings._faceHeight, -distance } );
            pCamera->lookAt( float3{ 0.0f, _settings._faceHeight, 0.0f } );
        }
    }

    float32 BenchScene::computeCharacterHalfExtent( uint32 characterCount ) const
    {
        const uint32 columns = ( gv_benchCharacterColumns > 0 ) ? MathUtil::min( static_cast<uint32>( gv_benchCharacterColumns ), characterCount ) : characterCount;
        return MathUtil::max( _settings._characterMinHalfExtent, 0.5f * static_cast<float32>( columns ) * _settings._characterSpacing );
    }

    void BenchScene::frameCharacterCameras( Scene* pScene, float32 halfExtent )
    {
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        // 캐릭터 키(~2 m)가 화면을 채우도록 허리 높이를 본다. 큐브 격자 맞춤(frameOneCamera)은 바닥 높이 원점을 본다.
        // 격자(`-gv_benchCharacterColumns`)로 배치했다면 앞 위쪽에서 격자 가운데를 내려다봅니다. 앞줄은 크고 뒷줄은 작게 보입니다.
        const uint32  characterCount = static_cast<uint32>( MathUtil::max( gv_benchCharacters, 1 ) );
        const uint32  columns        = ( gv_benchCharacterColumns > 0 ) ? static_cast<uint32>( gv_benchCharacterColumns ) : characterCount;
        const float32 depth          = static_cast<float32>( ( characterCount + columns - 1 ) / columns - 1 ) * _settings._characterSpacing;
        for ( CameraComponent* pCamera : pScene->getObjectManager()->getCameraRegistry().getAll() )
        {
            const float32 tanHalf  = MathUtil::tan( pCamera->getFieldOfViewY() * 0.5f );
            const float32 distance = ( tanHalf > MathUtil::kEpsilon ) ? ( halfExtent * _settings._characterFrameMargin / tanHalf ) : ( halfExtent * 3.0f );
            if ( depth <= 0.0f )
            {
                pCamera->setLocalPosition( float3{ 0.0f, _settings._characterEyeHeight, -distance } );
                pCamera->lookAt( float3{ 0.0f, _settings._characterLookHeight, 0.0f } );
                continue;
            }
            pCamera->setLocalPosition( float3{ 0.0f, _settings._crowdEyeHeightRatio * distance, -distance * _settings._crowdBackRatio } );
            pCamera->setFarPlane( MathUtil::max( pCamera->getFarPlane(), ( distance + depth ) * 2.0f ) );
            pCamera->lookAt( float3{ 0.0f, _settings._characterLookHeight, depth * _settings._crowdLookDepthRatio } );
        }
    }

    MeshComponent* BenchScene::spawnCube( GameObjectManager* pObjects, Material* pSceneMaterial, uint32 index, uint32 side, float32 origin )
    {
        if ( pObjects == nullptr || _listMeshVariant.empty() )
            return nullptr;

        StringBuilder<constant::kMaxBuffer64> nameBuilder;
        nameBuilder.append( "BenchMesh_" ).append( index );

        GameObject* pObject = pObjects->createGameObject( hashed_string( nameBuilder.c_str(), nameBuilder.size() ) );
        if ( pObject == nullptr )
            return nullptr;
        MeshComponent* pMesh = pObject->addComponent<MeshComponent>();
        if ( pMesh == nullptr )
            return nullptr;

        const uint32 col = index % side;
        const uint32 row = index / side;
        pMesh->setMesh( _listMeshVariant[index % static_cast<uint32>( _listMeshVariant.size() )] );
        // 기본 머티리얼은 씬 **로드** 경로(bindSceneMeshDefaults)에서만 붙습니다.
        // createScene 으로 직접 만든 씬은 그 단계를 거치지 않으므로 여기서 붙여 줍니다.
        // 붙이지 않으면 배치는 만들어져도 화면에는 아무것도 나오지 않습니다.
        if ( pSceneMaterial != nullptr )
            pMesh->setMaterial( pSceneMaterial );
        // 격자를 원점 기준으로 X/Z 양쪽에 펼칩니다. 카메라를 정면에서 뒤로 빼면 전부 화면에 들어옵니다.
        const float3 position{ origin + static_cast<float32>( col ) * _settings._cubeSpacing, 0.0f, origin + static_cast<float32>( row ) * _settings._cubeSpacing };
        pMesh->setLocalPosition( position );

        // GPU 가 이 큐브를 돌립니다. 시드가 각속도와 방향을 정하므로 큐브마다 속도가 다릅니다.
        // 0 은 "돌리지 않음"이라서 인덱스에 1 을 더합니다. CPU 는 회전을 계산하지 않습니다.
        // `-gv_benchAnimate=0` 이면 시드를 주지 않습니다. 각도가 벽시계 시간에서 나오기 때문에 같은 프레임을
        // 찍어도 이미지가 달라지므로, 픽셀 비교 검증에는 멈춰 있는 격자가 필요합니다.
        if ( gv_benchAnimate != 0 )
            pMesh->setGPUSpinSeed( index + 1u );
        pMesh->setVisible( true );

        const bool bNameplate     = attachNameplate( *pObject, index );
        const bool bNeedsUIMarker = bNameplate == false && gv_benchUiMarkers > 0 && index < static_cast<uint32>( gv_benchUiMarkers );
        if ( bNeedsUIMarker )
        {
            WidgetComponent* pMarker = pObject->addComponent<WidgetComponent>();
            if ( pMarker != nullptr )
            {
                unique_ptr<TextWidget> number = make_unique<TextWidget>();
                number->setText( to_string( 100u + index % 900u ) );
                pMarker->setSpace( WidgetSpace::Screen );
                pMarker->setWorldOffset( float3{ 0.0f, _settings._markerHeight, 0.0f } );
                pMarker->setContent( std::move( number ) );
            }
        }

        // 틱 무버입니다. 첫 번째 무버만 틱 안에서 위치와 스케일을 씁니다(게임플레이의 일반적인 패턴). 나머지는 틱만 실행되어,
        // 한 오브젝트에 틱 컴포넌트가 여럿일 때의 디스패치 비용을 측정합니다.
        const int32  moverCount  = gv_benchTickMovers;
        const uint32 movePercent = static_cast<uint32>( MathUtil::clamp( gv_benchMovePercent, 0, 100 ) );
        for ( int32 moverIndex = 0; moverIndex < moverCount; ++moverIndex )
        {
            BenchMoverComponent* pMover = pObject->addComponent<BenchMoverComponent>();
            if ( pMover == nullptr )
                break;
            pMover->setTarget( pMesh, position, static_cast<float32>( index ) * _settings._wavePhaseStep,
                               moverIndex == 0 && gv_benchAnimate != 0 && ( index % 100u ) < movePercent );
        }
        return pMesh;
    }

    void BenchScene::updateSpawnChurn( GameObjectManager* pObjects, Scene* pScene )
    {
        const uint32 churnCount      = ( gv_benchSpawnChurn > 0 ) ? static_cast<uint32>( gv_benchSpawnChurn ) : 0u;
        const uint32 cubeCount       = static_cast<uint32>( _listBenchMesh.size() );
        const bool   bNothingToChurn = churnCount == 0 || cubeCount == 0 || pObjects == nullptr || pScene == nullptr;
        if ( bNothingToChurn )
            return;

        const uint32  side   = MathUtil::max( _benchGridSide, 1u );
        const float32 origin = -0.5f * static_cast<float32>( side - 1 ) * _settings._cubeSpacing;
        for ( uint32 step = 0; step < churnCount; ++step )
        {
            const uint32 index = _churnCursor;
            _churnCursor       = ( _churnCursor + 1 ) % cubeCount;
            // 지우고 같은 위치에 새로 만듭니다. 이름도 같습니다(총알처럼 같은 이름으로 반복해서 만드는 패턴입니다). 죽은 오브젝트는 아직 지연 파괴 목록에
            // 있지만, 이름은 비어 있는 것으로 봅니다.
            if ( Component* pOld = pObjects->resolveComponent( _listBenchMesh[index] ) )
                pObjects->destroyObject( pOld->getOwner() );
            MeshComponent* pMesh  = spawnCube( pObjects, pScene->getMaterial(), index, side, origin );
            _listBenchMesh[index] = TComponentHandle<MeshComponent>{ pMesh };
        }
    }

    float4 BenchScene::makeBenchColor( uint32 index )
    {
        // 결정적 해시를 씁니다. 실행마다 같은 이미지가 나와야 스크린샷 비교가 의미를 갖습니다.
        uint32 hash = index * 2654435761u;
        hash ^= hash >> 15;
        const float32 r = static_cast<float32>( ( hash >> 0 ) & 0xFFu ) / 255.0f;
        const float32 g = static_cast<float32>( ( hash >> 8 ) & 0xFFu ) / 255.0f;
        const float32 b = static_cast<float32>( ( hash >> 16 ) & 0xFFu ) / 255.0f;
        // 너무 어두우면 조명을 확인하기 어려우므로 아래쪽 값을 끌어올립니다.
        return float4{ 0.35f + r * 0.65f, 0.35f + g * 0.65f, 0.35f + b * 0.65f, 1.0f };
    }

    bool BenchScene::isBenchTransparent( uint32 index, uint32 percent )
    {
        if ( percent == 0 )
            return false;
        // 격자 위치와 무관하게 흩어져야 합니다. 인덱스를 그대로 나누면 줄 단위로 뭉칩니다.
        uint32 hash = index * 2246822519u;
        hash ^= hash >> 13;
        hash *= 3266489917u;
        hash ^= hash >> 16;
        return ( hash % 100u ) < percent;
    }

    float32 BenchScene::halfExtentOf( uint32 side, float32 spacing )
    {
        return 0.5f * static_cast<float32>( side ) * spacing;
    }

    void BenchScene::spawnLight( Scene* pScene, float32 halfExtent )
    {
        if ( pScene == nullptr )
            return;
        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
            return;

        GameObject* pLightObject = pObjects->createGameObject( hashed_string( "BenchKeyLight" ) );
        if ( pLightObject == nullptr )
            return;
        DirectionalLightComponent* pLight = pLightObject->addComponent<DirectionalLightComponent>();
        if ( pLight == nullptr )
            return;
        _keyLight = TComponentHandle<DirectionalLightComponent>{ pLight };

        // 그림자 볼륨은 씬을 덮어야 한다. 엔진 기본값은 2 유닛이라 142 유닛 격자에서는
        // 그림자가 원점 근처 몇 개에만 걸린다.
        pLight->setShadowExtent( halfExtent * _settings._shadowExtentRatio );
        pLight->setShadowDistance( halfExtent * _settings._shadowDistanceRatio );
        pLight->setCastShadow( true );
        pLight->setIntensity( _settings._keyLightIntensity );

        SW_LOG_INFO( "[Bench] Created the main light (shadow volume radius %#).", static_cast<int32>( halfExtent * _settings._shadowExtentRatio ) );
    }

    void BenchScene::spawnBenchLights( Scene* pScene, float32 halfExtent )
    {
        const int32 lightCount = gv_benchLights;
        if ( pScene == nullptr || lightCount <= 0 )
            return;
        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
            return;

        // 반경은 비용을 정하는 값입니다. 기본값은 격자 간격의 몇 배로 두어 한 픽셀에 라이트가
        // 여럿 닿게 합니다. 그래야 루프가 실제로 돌아서 라이트 수에 따른 비용을 측정할 수 있습니다.
        const float32 radius = ( gv_benchLightRadius > 0.0f ) ? gv_benchLightRadius : ( _settings._cubeSpacing * _settings._lightRadiusSpacingRatio );

        for ( int32 lightIndex = 0; lightIndex < lightCount; ++lightIndex )
        {
            // 결정적 해시를 씁니다. 실행마다 같은 위치에 놓여야 스크린샷 비교가 성립합니다.
            uint32 hash = static_cast<uint32>( lightIndex ) * 2654435761u;
            hash ^= hash >> 15;
            hash *= 2246822519u;
            hash ^= hash >> 13;

            const float32 unitX = static_cast<float32>( ( hash >> 0 ) & 0x3FFu ) / 1023.0f;
            const float32 unitY = static_cast<float32>( ( hash >> 10 ) & 0x3FFu ) / 1023.0f;
            const float32 unitZ = static_cast<float32>( ( hash >> 20 ) & 0x3FFu ) / 1023.0f;

            const float3 position{ ( unitX * 2.0f - 1.0f ) * halfExtent,
                                   _settings._lightMinHeight + unitY * _settings._lightHeightRange,
                                   ( unitZ * 2.0f - 1.0f ) * halfExtent };
            const float3 color{ 0.3f + unitZ * 0.7f, 0.3f + unitX * 0.7f, 0.3f + unitY * 0.7f };

            GameObject* pLightObject = pObjects->createGameObject( hashed_string( "BenchLight" ) );
            if ( pLightObject == nullptr )
                continue;

            // 홀수 번째는 스포트라이트입니다. 점광만 두면 원뿔 감쇠 분기가 한 번도 실행되지 않습니다.
            if ( ( lightIndex & 1 ) != 0 )
            {
                SpotLightComponent* pSpot = pLightObject->addComponent<SpotLightComponent>();
                if ( pSpot == nullptr )
                    continue;
                pSpot->setColor( color );
                pSpot->setIntensity( _settings._spotLightIntensity );
                pSpot->setRadius( radius );
                pSpot->setOuterConeAngle( _settings._spotOuterConeAngle );
                pSpot->setInnerConeAngle( _settings._spotInnerConeAngle );
                pSpot->setLocalPosition( position ); // 회전은 주지 않는다 — 기본 방향이 아래다
                _listBenchExtra.push_back( pSpot->getHandle() );
                continue;
            }

            PointLightComponent* pPoint = pLightObject->addComponent<PointLightComponent>();
            if ( pPoint == nullptr )
                continue;
            pPoint->setColor( color );
            pPoint->setIntensity( _settings._pointLightIntensity );
            pPoint->setRadius( radius );
            pPoint->setLocalPosition( position );
            _listBenchExtra.push_back( pPoint->getHandle() );
        }

        SW_LOG_INFO( "[Bench] Scattered %# lights (radius %#, half are spot lights). Change the radius with -gv_benchLightRadius.",
                     lightCount, static_cast<int32>( radius ) );
    }

    void BenchScene::spawnGround( Scene* pScene, float32 halfExtent )
    {
        if ( pScene == nullptr || gv_benchGround == 0 )
            return;
        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
            return;

        GameObject* pGroundObject = pObjects->createGameObject( hashed_string( "BenchGround" ) );
        if ( pGroundObject == nullptr )
            return;
        MeshComponent* pMesh = pGroundObject->addComponent<MeshComponent>();
        if ( pMesh == nullptr )
            return;

        // 격자보다 넉넉히 크게 만듭니다. 그림자 볼륨 가장자리가 화면 안에 들어오면 "그림자가 잘렸다" 고 오해하기 때문입니다.
        const float32 size = halfExtent * _settings._groundSizeRatio;
        pMesh->setMesh( MeshUtil::createPlane( _settings._groundSegmentCount ) );
        pMesh->setLocalScale( float3{ size, 1.0f, size } );
        // 평면의 면은 로컬 y = 0 입니다. 큐브 아랫면(-0.5)보다 조금 더 아래로 내립니다.
        pMesh->setLocalPosition( float3{ 0.0f, _settings._groundHeight, 0.0f } );
        pMesh->setVisible( true );
        _listBenchExtra.push_back( pMesh->getHandle() );

        SW_LOG_INFO( "[Bench] Laid the floor plane (side %#).", static_cast<int32>( size ) );
    }

    void BenchScene::spawnBenchViews( Scene* pScene, float32 halfExtent )
    {
        if ( pScene == nullptr || gv_benchViews <= 0 )
            return;
        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
            return;
        // 격자 둘레의 원 위에서 가운데를 내려다봅니다. 뷰마다 다른 쪽을 보므로 컬링 결과가 서로 다릅니다.
        const uint32 viewCount = static_cast<uint32>( gv_benchViews );
        for ( uint32 viewIndex = 0; viewIndex < viewCount; ++viewIndex )
        {
            StringBuilder<constant::kMaxBuffer64> name;
            name.appendFormat( "BenchView%#", viewIndex );
            GameObject*      pObject = pObjects->createGameObject( hashed_string( name.c_str(), name.size() ) );
            CameraComponent* pCamera = pObject != nullptr ? pObject->addComponent<CameraComponent>() : nullptr;
            if ( pCamera == nullptr )
                continue;
            StringBuilder<constant::kMaxBuffer64> texture;
            texture.appendFormat( "rendertarget/bench_view%#", viewIndex );
            CameraRenderOutput output;
            output._target              = CameraOutputTarget::RenderTexture;
            output._renderTexture       = string( texture.c_str() );
            output._renderTextureWidth  = _settings._viewTextureSize;
            output._renderTextureHeight = _settings._viewTextureSize;
            pCamera->setRole( CameraRole::Capture );
            const float32 angle = 2.0f * MathUtil::kPi * static_cast<float32>( viewIndex ) / static_cast<float32>( viewCount );
            pCamera->setLocalPosition( float3{ MathUtil::cos( angle ) * halfExtent * _settings._viewRadiusRatio, halfExtent * _settings._viewHeightRatio,
                                               MathUtil::sin( angle ) * halfExtent * _settings._viewRadiusRatio } );
            pCamera->setFarPlane( MathUtil::max( pCamera->getFarPlane(), halfExtent * _settings._viewFarRatio ) );
            pCamera->lookAt( float3{ 0.0f, 0.0f, 0.0f } );
            pCamera->setRenderOutput( output );
            _listBenchExtra.push_back( pCamera->getHandle() );
        }
        SW_LOG_INFO( "[Bench] Placed %# capture cameras (render texture %#^2) around the grid.", viewCount, _settings._viewTextureSize );
    }

    void BenchScene::frameCameras( Scene* pScene, uint32 side, float32 spacing )
    {
        if ( pScene == nullptr )
            return;
        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
            return;

        // 씬의 **모든** 카메라를 맞춘다. 에디터 씬 뷰는 게임 카메라가 아니라 에디터 카메라로
        // 그리므로(App::getSceneViewCamera), 게임 카메라만 옮기면 씬 뷰에서는 아무것도 안 보인다.
        for ( CameraComponent* pCam : pObjects->getCameraRegistry().getAll() )
        {
            frameOneCamera( pCam, side, spacing );
        }
    }

    void BenchScene::frameOneCamera( CameraComponent* pCamera, uint32 side, float32 spacing )
    {
        if ( pCamera == nullptr )
            return;

        // 격자 한 변의 절반이 시야각 안에 들어오는 거리까지 카메라를 뒤로 뺍니다. 기본 카메라는 (0, 1.2, 3.2) 라서
        // 큐브 5000개(71x71 = 140 유닛)를 놓으면 화면에 몇 개밖에 걸리지 않습니다. 그러면 드로우는 실행되지만
        // 눈으로 확인할 수가 없습니다.
        const float32 halfExtent = halfExtentOf( side, spacing );
        const float32 fovY       = pCamera->getFieldOfViewY();
        const float32 tanHalf    = MathUtil::tan( fovY * 0.5f );
        const float32 distance   = ( tanHalf > MathUtil::kEpsilon ) ? ( halfExtent / tanHalf ) : ( halfExtent * 2.0f );

        // 격자는 카메라 축(Z)으로도 ±halfExtent 만큼 펼쳐져 있습니다. 중심까지의 거리만 쓰면 가까운 쪽이
        // 화면을 넘치고, 깊이까지 다 빼면 격자가 점처럼 작아집니다. 그래서 깊이의 절반만 더합니다.
        // 높이는 낮게 둡니다. 높이 올려 내려다보면 격자가 화면 아래쪽으로 쏠립니다.
        const float32 height = halfExtent * _settings._cameraHeightRatio;
        pCamera->setLocalPosition( float3{ 0.0f, height, -( distance + halfExtent * 0.5f ) } );
        pCamera->setFarPlane( MathUtil::max( pCamera->getFarPlane(), ( halfExtent + distance ) * 3.0f ) );
        pCamera->lookAt( float3{ 0.0f, 0.0f, 0.0f } );

        SW_LOG_INFO( "[Bench] Moved camera '%#' to fit the grid (radius %#) at z=%#.",
                     pCamera->getComponentName().c_str(), static_cast<int32>( halfExtent ),
                     static_cast<int32>( -( distance + halfExtent * 0.5f ) ) );
    }

    void BenchScene::refreshFraming()
    {
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        Scene*        pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
        if ( pScene == nullptr )
            return;
        applyFraming( pScene, _framingMode );
    }

    FramingInput BenchScene::captureFramingInput( const Scene* pScene ) const
    {
        // 모드마다 그 맞춤 함수가 읽는 값만 채웁니다(나머지는 0). 새 입력을 읽게 되면 FramingInput 에 필드를 더하고 여기서 채웁니다.
        FramingInput input;
        input.mode = _framingMode;
        if ( pScene != nullptr && pScene->getObjectManager() != nullptr )
            input.cameraCount = static_cast<uint32>( pScene->getObjectManager()->getCameraRegistry().getAll().size() );
        switch ( _framingMode )
        {
            case FramingMode::Grid:
            {
                input.gridSide = _benchGridSide;
                break;
            }
            case FramingMode::Character:
            case FramingMode::Combat:
            case FramingMode::Ragdoll:
            {
                // 줄 · 격자의 깊이를 gv_benchCharacters · gv_benchCharacterColumns 에서 계산합니다(frameCharacterCameras).
                input.characters = gv_benchCharacters;
                input.columns    = gv_benchCharacterColumns;
                break;
            }
            case FramingMode::Face:
            {
                input.faces = gv_benchFaces;
                break;
            }
            case FramingMode::Rig:
            {
                input.rigCount = static_cast<uint32>( _listRigBody.size() );
                input.rigView  = gv_benchRigView;
                break;
            }
        }
        return input;
    }

    void BenchScene::applyFraming( Scene* pScene, FramingMode requested )
    {
        _framingMode = selectFramingMode( _framingMode, requested );
        _framedInput = captureFramingInput( pScene );
        switch ( _framingMode )
        {
            case FramingMode::Grid:
            {
                frameCameras( pScene, _benchGridSide, _settings._cubeSpacing );
                break;
            }
            case FramingMode::Character:
            {
                frameCharacterCameras( pScene, computeCharacterHalfExtent( static_cast<uint32>( MathUtil::max( gv_benchCharacters, 1 ) ) ) );
                break;
            }
            case FramingMode::Combat:
            {
                frameCharacterCameras( pScene, _settings._combatHalfExtent );
                break;
            }
            case FramingMode::Ragdoll:
            {
                frameCharacterCameras( pScene, _ragdollHalfExtent );
                break;
            }
            case FramingMode::Face:
            {
                frameFaceCameras( pScene );
                break;
            }
            case FramingMode::Rig:
            {
                frameRigCameras( pScene );
                break;
            }
        }
    }

    void BenchScene::update( float32 deltaTime )
    {
        _benchElapsed += deltaTime;

        // 에디터 뷰포트 카메라는 게임 onInitialize 보다 늦게 만들어진다. 한 번만 다시 맞춘다.
        if ( _bRefreshedCameras == SW_FALSE && _benchElapsed > _settings._cameraRefreshDelay )
        {
            _bRefreshedCameras = SW_TRUE;
            refreshFraming();
        }

        // 큐브는 씬이 소유합니다. 여기에는 핸들만 있으므로 씬이 통째로 바뀌어도(상태 복원) 해제된 주소를
        // 건드릴 수 없습니다. 핸들로 찾은 결과가 nullptr 이면 그 큐브는 이미 없는 것입니다.
        SceneManager*      pSceneManager = game::findSubsystem<SceneManager>();
        Scene*             pScene        = ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
        GameObjectManager* pObjects      = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
        if ( pObjects == nullptr )
            return;

        // 시나리오가 `-gv_bench…` 를 도중에 바꾸면 카메라가 따라간다. 맞춤은 장면을 세울 때와 지연 뒤 두 번뿐이라 늦은 변경이 반영되지 않았다.
        if ( captureFramingInput( pScene ) != _framedInput )
            applyFraming( pScene, _framingMode );

        updateProbes( pObjects );
        updateMaterialChurn( pObjects );
        updateSpawnChurn( pObjects, pScene );
        updateRigCharacters( deltaTime );
        updateRagdollChurn( pObjects );

        // 멈춰 둔 격자는 프레임마다 같은 이미지가 나옵니다. 픽셀 비교 검증의 전제입니다.
        // 회전(컴퓨트)만 끄고 이 사인파를 남기면 여전히 흔들립니다.
        if ( gv_benchAnimate == 0 )
            return;

        // **쓰기를 모아서 배치로 넘깁니다**(유니티의 IJobParallelForTransform 에 해당합니다). 주의: 큐브마다 핸들로 대상을 찾고
        // 세터 둘을 부르면 세터 하나에 약 24ns, 8000개면 프레임당 약 380µs 가 들어 게임 스레드에서 가장 큰 항목이 됩니다.
        // 배치는 핸들 해석, 필드 쓰기, 더티 표시를 워커에 나눠 맡기고 세대를 한 번만 올립니다.
        const uint32  count       = _listInstanceBatch.empty() ? static_cast<uint32>( _listBenchMesh.size() ) : _instanceCubeCount;
        const uint32  side        = MathUtil::max( _benchGridSide, 1u );
        const float32 origin      = -0.5f * static_cast<float32>( side - 1 ) * _settings._cubeSpacing;
        const uint32  movePercent = static_cast<uint32>( MathUtil::clamp( gv_benchMovePercent, 0, 100 ) );
        if ( _listInstanceBatch.empty() == false )
        {
            // 인스턴스 배치에는 씬 컴포넌트도 배치 쓰기도 없습니다. 월드 행렬을 항목에 바로 적습니다(언리얼의 ISM 에 해당합니다). 회전은
            // 컴포넌트 경로와 같이 GPU 가 시드로 만듭니다.
            const uint32 variantCount = static_cast<uint32>( _listInstanceBatch.size() );
            for ( uint32 index = 0; index < count; ++index )
            {
                if ( ( index % 100u ) >= movePercent )
                    continue;
                const float32 phase = static_cast<float32>( index ) * _settings._wavePhaseStep;
                const float32 wave  = MathUtil::sin( _benchElapsed + phase );
                const float3  position{ origin + static_cast<float32>( index % side ) * _settings._cubeSpacing,
                                       wave * _settings._waveHeight,
                                       origin + static_cast<float32>( index / side ) * _settings._cubeSpacing };
                const float32 scale = _settings._waveScaleMin + _settings._waveScaleRange * MathUtil::abs( wave );
                _listInstanceBatch[index % variantCount]->setWorld(
                    index / variantCount, float4x4::makeTrs( position, float3{ 0.0f, 0.0f, 0.0f }, float3{ scale, scale, scale } ) );
            }
            return;
        }
        // 틱 무버가 있으면 큐브가 틱 안에서 스스로 위치를 씁니다. 여기서 또 쓰면 두 번 쓰게 됩니다.
        if ( gv_benchTickMovers > 0 )
            return;
        _listTransformWrite.clear();
        _listTransformWrite.reserve( count );
        for ( uint32 index = 0; index < count; ++index )
        {
            // 일부만 움직이는 씬입니다. 나머지는 쓰기 목록에 아예 넣지 않습니다(실제 게임에서 움직이지 않는 물체가 그렇습니다).
            if ( ( index % 100u ) >= movePercent )
                continue;
            // 인덱스마다 위상을 어긋나게 해서 전부 같은 값이 되지 않도록 합니다. 전부 같으면
            // 배치 키는 물론 트랜스폼까지 동일해져서 실제와 다른(너무 좋은) 결과가 나옵니다.
            const float32 phase = static_cast<float32>( index ) * _settings._wavePhaseStep;
            const float32 wave  = MathUtil::sin( _benchElapsed + phase );

            // 격자 위치는 생성할 때와 같은 식으로 계산합니다. 핸들로 대상을 찾아 읽지 않습니다(그 읽기가 배치 쓰기 비용의 3분의 1 정도입니다).
            _listTransformWrite.emplace_back();
            SceneTransformWrite& write = _listTransformWrite.back();
            write._handle              = _listBenchMesh[index].untyped();
            write._localPosition       = float3{ origin + static_cast<float32>( index % side ) * _settings._cubeSpacing,
                                           wave * _settings._waveHeight,
                                           origin + static_cast<float32>( index / side ) * _settings._cubeSpacing };
            write._bSetPosition        = SW_TRUE;
            // 회전은 **컴퓨트가 만듭니다**(instanceanimation.hlsl). 시드 해시가 인스턴스마다 속도와 방향을 다르게 정해 줍니다
            // (속도가 하나면 큐브가 몇천 개여도 한 덩어리처럼 보입니다). CPU 가 여기서 회전을 다시 쓰면 GPU 가 쓴 값을
            // 다음 업로드가 덮어써서 다시 균일해집니다. 그래서 회전은 CPU 가 건드리지 않습니다.

            // 스케일도 흔듭니다. 위치와 회전만 바꾸면 월드 행렬의 회전과 이동 성분만 갱신되므로
            // 스케일 경로(와 바운드 반지름을 쓰는 컬링)가 검증되지 않습니다.
            const float32 scale = _settings._waveScaleMin + _settings._waveScaleRange * MathUtil::abs( wave );
            write._localScale   = float3{ scale, scale, scale };
            write._bSetScale    = SW_TRUE;
        }
        pObjects->applyTransformBatch( _listTransformWrite.data(), static_cast<uint32>( _listTransformWrite.size() ) );
    }

    uint32 BenchScene::nextChurnRandom()
    {
        // xorshift32 입니다. 표준 난수를 쓰지 않는 이유는 **재현성**입니다. 같은 프레임 수만큼 실행하면 같은 순서가
        // 나와야 스크린샷과 로그를 실행 사이에 비교할 수 있습니다.
        _churnRandom = XorShift32::next( _churnRandom );
        return _churnRandom;
    }

    void BenchScene::releaseChurnInstance( const MaterialInstance* pInstance )
    {
        if ( pInstance == nullptr )
            return;
        for ( size_t slot = 0; slot < _listChurnInstance.size(); ++slot )
        {
            if ( _listChurnInstance[slot].get() != pInstance )
                continue;
            // swap-and-pop 으로 지웁니다. 무작위로 고르는 목록이라 순서는 의미가 없습니다.
            VectorUtil::removeAtSwap( _listChurnInstance, slot );
            return;
        }
    }

    void BenchScene::churnInstanceValue( MaterialInstance* pInstance )
    {
        if ( pInstance == nullptr )
            return;

        if ( ( nextChurnRandom() & 1u ) != 0u )
        {
            // 알파는 부모를 따라갑니다. 유리 머티리얼의 알파를 1 로 올리면 투명 경로가 이미지에서
            // 사라져서, 값을 흔든다는 사실 자체가 검증을 망가뜨립니다.
            const float4  tint   = makeBenchColor( nextChurnRandom() );
            const bool    bGlass = ( pInstance->getParent() != nullptr ) && ( pInstance->getParent() == _glassMaterial.get() );
            const float32 alpha  = bGlass ? ( 0.25f + 0.35f * static_cast<float32>( nextChurnRandom() & 0xFFu ) / 255.0f ) : 1.0f;
            pInstance->setVectorParameter( hashed_string( kMaterialColorParameter ), float4{ tint._x, tint._y, tint._z, alpha } );
        }
        else
        {
            const float32 roughness = static_cast<float32>( nextChurnRandom() & 0xFFFFu ) / 65535.0f;
            pInstance->setScalarParameter( hashed_string( "_roughness" ), roughness );
        }
    }

    void BenchScene::updateMaterialChurn( GameObjectManager* pObjects )
    {
        const uint32 valueChurn   = ( gv_benchMaterialChurn > 0 ) ? static_cast<uint32>( gv_benchMaterialChurn ) : 0u;
        const uint32 addChurn     = ( gv_benchMaterialChurnAdd > 0 ) ? static_cast<uint32>( gv_benchMaterialChurnAdd ) : 0u;
        const uint32 keywordEvery = ( gv_benchMaterialChurnKeyword > 0 ) ? static_cast<uint32>( gv_benchMaterialChurnKeyword ) : 0u;
        if ( ( valueChurn | addChurn | keywordEvery ) == 0u )
            return;

        const uint32 cubeCount = static_cast<uint32>( _listBenchMesh.size() );
        if ( pObjects == nullptr || cubeCount == 0 )
            return;

        ++_churnFrame;

        // 1) 값만 흔듭니다. 배치 구성은 그대로이고 머티리얼 바이트만 바뀝니다.
        for ( uint32 step = 0; step < valueChurn && _listChurnInstance.empty() == false; ++step )
        {
            churnInstanceValue( _listChurnInstance[nextChurnRandom() % _listChurnInstance.size()].get() );
        }

        // 2) 집합을 흔듭니다. 붙이면 배치가 갈리고 원소가 늘어나며, 제거하면 회수와 재사용이 실행됩니다.
        for ( uint32 step = 0; step < addChurn; ++step )
        {
            MeshComponent* pMesh = pObjects->resolveComponent( _listBenchMesh[nextChurnRandom() % cubeCount] );
            if ( pMesh == nullptr )
                continue;

            // 목록이 상한에 닿으면 제거하는 쪽으로 기웁니다. 상한이 없으면 큐브 수만큼 늘어나 이 스위치가
            // 사실상 -gv_benchMaterialInstances 와 같아지고, "붙였다 뗐다" 를 측정하지 못합니다.
            const bool bDrop = ( _listChurnInstance.size() >= kChurnInstanceCap ) || ( ( nextChurnRandom() & 3u ) == 0u );
            if ( bDrop )
            {
                const shared_ptr<MaterialInstance> dropped = pMesh->getMaterialInstance();
                if ( dropped == nullptr )
                    continue;
                // 큐브에서 분리하고 **이 목록에서도 해제합니다.** 목록에 남겨 두면 참조가 사라지지 않아
                // 회수 경로가 영영 실행되지 않습니다. 렌더 패킷이 아직 참조하고 있으면 거기서 마지막으로 해제됩니다.
                pMesh->setMaterialInstance( nullptr );
                releaseChurnInstance( dropped.get() );
                continue;
            }

            Material* pParent = pMesh->getMaterial();
            if ( pParent == nullptr )
                continue;
            // 부모는 **그 큐브가 쓰는 머티리얼**이어야 합니다. 불투명 큐브에 유리 머티리얼을 부모로 주면 블렌드
            // 모드가 뒤집혀 이미지가 달라지고, 스트레스 테스트가 아니라 버그가 됩니다.
            shared_ptr<MaterialInstance> instance = MaterialInstance::create( pParent );
            churnInstanceValue( instance.get() );
            _listChurnInstance.push_back( instance );
            pMesh->setMaterialInstance( std::move( instance ) );
        }

        // 3) 퍼뮤테이션을 흔듭니다. 셰이더가 다시 컴파일되므로 주기를 길게 줍니다.
        const bool bShouldChurnKeywords = keywordEvery > 0 && ( _churnFrame % keywordEvery ) == 0 && _listChurnInstance.empty() == false;
        if ( bShouldChurnKeywords )
        {
            MaterialInstance* pInstance = _listChurnInstance[nextChurnRandom() % _listChurnInstance.size()].get();
            if ( pInstance != nullptr )
            {
                if ( ( nextChurnRandom() & 1u ) != 0u )
                {
                    if ( ( nextChurnRandom() & 1u ) != 0u )
                        pInstance->enableKeyword( hashed_string( kChurnKeyword ) );
                    else
                        pInstance->disableKeyword( hashed_string( kChurnKeyword ) );
                }
                else
                {
                    pInstance->setMultiCompile( hashed_string( kChurnFogMultiCompile ),
                                                kArrChurnFogOption[nextChurnRandom() % SW_COUNT_OF( kArrChurnFogOption )] );
                }
            }
        }
    }
} // namespace sw
