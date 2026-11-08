/**
 * @file MeshLODSystem.h
 * @brief 메시 LOD 고르기입니다 — LOD 단계가 여럿인 메시(`Mesh::getLODs`)를 그리는 컴포넌트마다 화면 크기로 단계를 정합니다. 씬 서비스(`ISceneService`)라 매니저마다 하나 있고,
 *        `SceneFrameStep::FlushEnd` 단계(그리기 전, 트랜스폼을 펼친 뒤)에서 돕니다. 얻기는 `manager.getSubsystem<MeshLODSystem>()` 입니다.
 * @details 뷰는 렌더 패킷 빌더가 프레임마다 넣는다(주 시점과 이번 프레임에 그리는 추가 뷰 — 애니메이션 LOD 와 같은 목록, `RenderPacketSceneCollector::updateLODViews`).
 *          대기 뷰(컷 프리롤의 카메라, `CameraManagerComponent`)가 있으면 그 시점의 화면 크기도 본다 — 컷 첫 프레임에 그 시점의 단계로 바로 그린다.
 *          단계를 바꾸면 컴포넌트가 렌더 상태를 더티로 표시하고, 렌더 수집이 그 단계의 정점 범위로 배치를 나눈다(`MeshComponent::findDrawSection`).
 *          지금은 컴포넌트마다 CPU 에서 고른다 — 인스턴스마다 GPU 컬링이 고르는 경로(`gpu_cull.hlsl` 의 간접 인자)와 단계 사이 디더 크로스페이드는 계획
 *          PreloadAndLOD P9 의 남은 일이다(셰이더).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/LOD/LODView.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    class GameObjectManager;
    class Mesh;
    class MeshComponent;
    class ScenePrimitive;

    /** @class MeshLODSystem @brief 씬 하나의 메시 LOD 고르기입니다. 파일 머리말 참고. */
    class SW_API MeshLODSystem final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "MeshLODSystem";
        /** @brief 고정 단계가 없음(화면 크기로 고른다)을 뜻하는 `setForcedLOD` 값입니다. */
        static constexpr int32 kPickByScreenSize = -1;
        const utf8*            getSubsystemName() const override { return kSubsystemName; }

        explicit MeshLODSystem( GameObjectManager& manager );
        ~MeshLODSystem() override                        = default;
        MeshLODSystem( const MeshLODSystem& )            = delete;
        MeshLODSystem& operator=( const MeshLODSystem& ) = delete;

        /** @brief 프레임 단계 `FlushEnd` 에서 LOD 목록(`PrimitiveRegistry::getLODPrimitives`)의 단계를 고릅니다. */
        void update( float32 deltaTime ) override;
        /** @brief 씬을 비울 때 놓을 것이 없습니다 — 목록은 컴포넌트가 떨어질 때 빠지고, 뷰는 다음 씬에서도 씁니다. */
        void onSceneCleared() override {}

        /** @brief 이번 프레임에 그린 뷰들입니다(렌더 패킷 빌더, 게임 스레드). 뷰를 받기 전에는 고르지 않는다(모두 LOD 0). */
        void setViews( const vector<LODView>& listView );
        /** @brief 곧 볼 시점(대기 뷰)을 넣습니다. 풀 때까지 그 시점의 화면 크기도 본다. */
        void setPendingView( const LODView& view );
        void clearPendingView();
        bool hasPendingView() const { return _bHasPendingView == SW_TRUE; }
        /**
         * @brief 이번 프레임의 단계를 GPU 컬링이 아니라 CPU(`update`)가 골라야 하면 true 입니다 — 단계를 고정했거나(`setForcedLOD`), 대기 뷰(컷 준비)가 있거나, 뷰를 아직 받지 못했다.
         * @details GPU 인스턴스 LOD(`GPUSceneBuilder` 의 LOD 그룹)는 렌더가 그린 뷰의 화면 크기로만 고른다. 고정 단계 · 대기 뷰 · 뷰 없음은 그 뷰들이 모르는 입력이라 CPU 선택이 맡는다.
         */
        bool needsCPUSelection() const;
        /**
         * @brief @p mesh 를 스킨 칸 @p skinSlot 으로 그리는 것의 단계를 GPU 컬링이 고를 수 있으면 true 입니다. GPU 그룹(`GPUSceneBuilder::applyGPULODGroup`)과 CPU 선택(`update`)이 같은 규칙을 쓴다.
         * @details 스킨 칸이 있으면(칸마다 그리는 단계 하나만 스키닝한다 — `GPUMeshMorphPool`) 구간 메시면(구간마다 단계 범위 — `MeshComponent::findDrawSection`) 컴포넌트가 고른 단계를 그려야 하므로
         *          늘 CPU 가 고른다(언리얼도 스켈레탈 메시 LOD 는 컴포넌트가 CPU 에서 고르고 GPU 인스턴스 LOD 는 정적 메시만이다).
         */
        static bool isGPUSelectable( const Mesh& mesh, uint32 skinSlot );
        /**
         * @brief GPU 컬링이 단계를 고르는 중임을 알립니다(`GPUSceneBuilder` 가 빌드마다 정한다). 켜 있고 `needsCPUSelection` 이 아니면 `update` 는 고르지 않는다 — 단계를 고르는 쪽이 둘이면
         *        컴포넌트마다 프레임마다 `setLODIndex` 가 렌더 상태를 더럽혀 부분 수집이 헛돈다.
         */
        void setGPUSelectionActive( bool bActive ) { _bGPUSelectionActive = bActive ? SW_TRUE : SW_FALSE; }
        /** @brief 렌더가 뷰를 한 번이라도 넣었으면 true 입니다(넣기 전에는 화면 크기를 모른다). */
        bool areViewsSet() const { return _bViewsSet == SW_TRUE; }
        /** @brief 컴포넌트의 경계 구를 모든 뷰(대기 뷰 포함)에서 본 가장 큰 화면 크기입니다. 어느 뷰에도 없으면 0 입니다(텍스처 밉 요청 우선순위도 이것을 쓴다). */
        float32 computeScreenSize( const MeshComponent& component ) const;
        /**
         * @brief 이 씬의 모든 메시를 화면 크기와 상관없이 단계 @p lodIndex 로 그립니다(`kPickByScreenSize` 면 화면 크기로 고른다). 메시 미리보기 창 · 개발 도구(`-gv_meshLodForce`)가 단계를 고정해 비교한다.
         * @details 메시의 단계 수를 넘는 값은 가장 낮은 단계로 그려진다(`MeshComponent::setLODIndex`).
         */
        void  setForcedLOD( int32 lodIndex ) { _forcedLOD = lodIndex; }
        int32 getForcedLOD() const { return _forcedLOD; }

        /**
         * @brief 화면 크기 @p screenSize 에서 지금 단계 @p currentLOD 다음에 그릴 단계입니다. 거친 쪽은 바로, 고운 쪽은 여유(`MeshLODSettings::_refineHysteresis` — 문턱에서 카메라가 흔들려 단계가 프레임마다 오가지 않게)를 두고 바꾼다.
         * @details 메시 하나의 판정이라 테스트가 뷰 없이 직접 부른다.
         */
        static uint32 selectNextLOD( const Mesh& mesh, float32 screenSize, uint32 currentLOD );
        /**
         * @brief 컴포넌트 하나의 LOD 판정을 한 줄로 설명합니다(LOD 판정 확인 — 개발 명령 `lod.explain` · 감시 값 `Mesh.LOD`).
         * @details `LOD <지금>/<단계 수> screen <화면 크기> thresholds [<단계별 임계값>] by <고른 쪽>` — 고른 쪽은 forced(고정 단계) · views-not-set(뷰 없음, 원본) ·
         *          gpu(GPU 컬링이 인스턴스마다 고른다 — 컴포넌트 단계는 쓰지 않는다) · cpu(화면 크기 + 고운 쪽 여유) · section(주 메시를 따른다) · single(단계가 하나)다.
         *          보기 모드 LODLevel 이 색으로 보이는 것을 숫자로 확인한다.
         */
        string explainLOD( const MeshComponent& component ) const;

    private:
        /** @brief `_listPickedLOD` 칸의 "단계를 건드리지 않는다" 입니다(구간 컴포넌트 · GPU 가 고르는 것 · 뷰 없음). */
        static constexpr uint32 kKeepLOD = constant::invalid_index::kUint32;

        /** @brief 컴포넌트 하나에 이번 프레임 줄 단계입니다(`kKeepLOD` 면 그대로). 읽기만 하므로 워커가 부른다(`update` 의 병렬 본문). */
        uint32 pickLOD( const MeshComponent& component, int32 forcedLOD, bool bGPUPicks ) const;

    private:
        GameObjectManager&      _manager;
        vector<LODView>         _listView;
        vector<ScenePrimitive*> _listPrimitive; ///< 이번 프레임 LOD 목록의 사본(재사용). 레지스트리의 집합은 번호로 집을 수 없어 워커가 칸 번호로 나눠 읽게 옮겨 둔다
        vector<uint32>          _listPickedLOD; ///< LOD 목록과 같은 순서로 이번 프레임에 고른 단계(재사용). 워커는 자기 칸에만 쓰고 적용은 게임 스레드가 차례로 한다
        LODView                 _pendingView;
        int32                   _forcedLOD;           ///< 씬 단위로 고정한 단계(`kPickByScreenSize` = 화면 크기로 고른다)
        uint8                   _bViewsSet;           ///< 뷰를 한 번이라도 받았다(받지 않으면 고르지 않는다)
        uint8                   _bHasPendingView;     ///< 대기 뷰가 있다
        uint8                   _bGPUSelectionActive; ///< GPU 컬링이 단계를 고르는 중이다(`setGPUSelectionActive`)
    };
} // namespace sw
