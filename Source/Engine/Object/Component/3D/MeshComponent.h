/**
 * @file MeshComponent.h
 * @brief 메시를 그리는 SceneComponent 입니다(3D 렌더링). 프리미티브 레지스트리를 거쳐 GPUScene 으로 들어갑니다.
 * @details 레지스트리 번호는 코어 기반 `ScenePrimitive` 가 가집니다. 레지스트리는 그 기반만 알고, 빌더가 이 타입으로 `static_cast` 합니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Binding/GPUSpriteInstanceData.h"
#include "Engine/Object/GameObject/Registry/ScenePrimitive.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
    class Material;
    class MaterialInstance;
    class Mesh;
    class PrimitiveRegistry;

    enum class InstanceHighlightStyle : uint8; // 정의는 GPUInstanceHighlight.h — 값으로만 받는다

    /**
     * @class MeshComponent
     * @brief GameObject 에 붙어 그려지는 메시입니다(월드 트랜스폼은 SceneComponent 에서 얻습니다).
     */
    REFLECT( Category = "Rendering 3D", DisplayName = "Mesh Component", Tooltip = "3D Static Mesh Renderer", Meta = "Icon=Cube" )
    class SW_API MeshComponent : public ScenePrimitive
    {
    public:
        REFLECT_BODY();

        /** @brief 메시와 머티리얼 없이 만듭니다. */
        MeshComponent();
        /** @brief 기본 소멸자입니다(메시와 머티리얼 인스턴스의 shared_ptr 을 해제합니다). */
        virtual ~MeshComponent() override = default;

        /** @brief 플레이가 시작될 때 렌더 에셋을 해석합니다. */
        void onBeginPlay() override;

        /**
         * @brief 렌더 에셋(메시, 머티리얼)을 해석합니다. 시작(`onBeginPlay`), 씬 초기화, 머티리얼 참조 변경이 부릅니다.
         * @details 파생 타입이 내용을 덧붙인다(스프라이트는 텍스처 인스턴스). 로드는 값만 채우므로 이 함수가 값을 리소스로 바꾸는 곳이다.
         */
        virtual void resolveRenderAssets();
        /** @brief 읽은 메시 id 와 머티리얼 참조를 리소스로 해석합니다(`resolveRenderAssets`). 편집 중 되돌리기나 프리팹 드래그로 다시 만든 메시가 그려지게 합니다. */
        void onPostLoad() override { resolveRenderAssets(); }
        /**
         * @brief `_meshID` 를 메시로 해석합니다. `.mesh` 경로이면 메시 캐시에 로드를 요청하고(`MeshCache::request`), 아니면 내장 도형을 씁니다.
         * @details 현재 메시가 현재 id 로 잡은 것이면 그대로 둔다(`_resolvedMeshID`. 머티리얼의 `_acquiredMaterialPath` 와 같은 규칙이다).
         *          "메시가 있으면 그대로"로 판정하면 id 를 바꿔도(인스펙터, 붙여넣기, 되돌리기) 이전 메시를 그린다. 비어 있으면 타입의
         *          기본(`getDefaultMeshID`)을 쓴다. 메시는 단위 큐브, 스프라이트는 사각형이다.
         *
         *          에셋이 아직 읽는 중이면 **메시 없이 둔다**(그리지 않는다 — 기본 메시로 대신 그리지 않는다). 도착하면 씬 프레임의 `AssetArrival` 단계가
         *          메시를 넣고 렌더 에셋을 다시 해석한다(`resolveRenderAssets` — 파생 타입의 스킨 사본도 그때 만든다). 읽지 못한 에셋은 메시 없이 남는다.
         */
        void resolveRuntimeMesh();
        /** @brief 메시 에셋을 아직 읽는 중이면 true 입니다(그때 `getMesh()` 는 nullptr 이다). */
        bool isMeshLoading() const { return _meshLoad.isLoading(); }
        /** @brief 저장되는 메시 id 를 바꾸고 곧바로 해석합니다(프리미티브 이름 또는 `.mesh` 에셋 경로). */
        void setMeshID( string_view meshID );
        /** @brief 저장되는 메시 id 입니다(프리미티브 이름 또는 `.mesh` 에셋 경로). 비어 있으면 타입의 기본입니다. */
        const string& getMeshID() const { return _meshID; }

        /** @brief 메시를 설정합니다. 저장되지 않는 런타임 지정이며, 메시 id 가 바뀔 때까지 유지됩니다. */
        void setMesh( shared_ptr<Mesh> mesh );
        /** @brief 메시를 반환합니다. */
        const shared_ptr<Mesh>& getMesh() const { return _mesh; }
        /** @brief 원시 메시 포인터를 반환합니다. */
        Mesh* getRawMesh() const { return _mesh.get(); }

        /** @brief 머티리얼을 설정합니다. **저장되지 않는** 런타임 지정이며, 저장되는 참조는 `setMaterialPath` 로 정합니다. */
        void setMaterial( Material* pMaterial );
        /** @brief 머티리얼을 반환합니다. 없으면 렌더러가 씬 기본 머티리얼을 씁니다. */
        Material* getMaterial() const { return _pMaterial; }

        /**
         * @brief 머티리얼 에셋 경로를 바꾸고 그 머티리얼을 잡습니다. 저장되는 참조이고, 빈 경로는 씬 기본 머티리얼입니다.
         * @details 언리얼의 `UMeshComponent::OverrideMaterials`, 유니티의 `Renderer.sharedMaterials` 에 해당한다. 런타임 지정(`setMaterial`)은
         *          저장되지 않으므로 씬이나 프리팹에 남길 머티리얼은 이 경로로 정한다.
         */
        void setMaterialPath( string_view path );
        /** @brief 저장되는 머티리얼 에셋 경로입니다. 비어 있으면 씬 기본 머티리얼입니다. */
        hashed_string getMaterialPath() const { return _materialPath; }
        /**
         * @brief `_materialPath`(비어 있으면 타입의 기본 `getDefaultMaterialPath`)의 머티리얼을 캐시에서 잡아 등록합니다. 이미 그 경로를 잡았으면 아무것도 하지 않습니다.
         * @details 로드는 값만 채우므로 시작(`onBeginPlay`), 씬 초기화, 프로퍼티 편집, 세터가 이 함수를 부른다(`resolveRuntimeMesh` 와 같은 역할이다).
         *          컴포넌트는 디바이스를 모르므로 디바이스 없이 잡고 GPU 업로드를 캐시에 맡긴다(`MaterialCache::requestInitialize`).
         *          새 것을 등록한 **뒤에** 이전 것을 해제한다. 해제하는 순간 캐시가 지울 수 있기 때문이다. 경로가 비면 런타임 지정(`setMaterial`)은 건드리지
         *          않는다(잡아 둔 것이 있을 때만 해제하고 비운다).
         */
        void resolveMaterialAsset();

        /** @brief 머티리얼 인스턴스를 설정합니다. */
        void setMaterialInstance( shared_ptr<MaterialInstance> instance );
        /** @brief 머티리얼 인스턴스를 반환합니다. */
        const shared_ptr<MaterialInstance>& getMaterialInstance() const { return _materialInstance; }
        /** @brief 원시 머티리얼 인스턴스 포인터를 반환합니다. */
        MaterialInstance* getRawMaterialInstance() const { return _materialInstance.get(); }

        /** @brief 블렌드 모드를 설정합니다. */
        void setBlendMode( RHIBlendMode mode );
        /** @brief 블렌드 모드를 반환합니다. */
        RHIBlendMode getBlendMode() const { return _blendMode; }

        /**
         * @brief GPU 회전 애니메이션 시드를 설정합니다(0 이면 애니메이션 없음).
         * @details 0 이 아니면 GPUScene 인스턴스에 실려 `instance_animation.hlsl` 이 이 값을 해시해
         *          **인스턴스마다 다른 각속도**로 회전을 적용한다. CPU 는 매 프레임 트랜스폼을 다시 쓰지 않아도
         *          되고, 회전은 전적으로 컴퓨트가 만든다. 시드가 다르면 속도도 다르므로 보통 인덱스 + 1 을 준다.
         */
        void setGPUSpinSeed( uint32 seed );
        /** @brief setGPUSpinSeed 로 정한 값입니다(0 이면 GPU 회전 없음). */
        uint32 getGPUSpinSeed() const { return _gpuSpinSeed; }
        /**
         * @brief 정점 애니메이션(VAT) 시간 오프셋(초)을 정합니다. 메시에 VAT 가 걸려 있을 때만 셰이더가 읽습니다(`GPUInstance::_vertexAnimationPhase`).
         * @details 셰이더는 VAT 시계에 이 값을 더한 시점의 프레임을 그린다. 군중 시스템이 VAT 로 넘길 때 한 번 기록한다(저장하지 않는 런타임 값이다).
         */
        void setVertexAnimationPhase( float32 phaseSeconds );
        /** @brief setVertexAnimationPhase 로 정한 값입니다. */
        float32 getVertexAnimationPhase() const { return _vertexAnimationPhase; }

        /**
         * @brief GPU 인스턴스에 실을 스프라이트 프레임(UV 사각형)과 색입니다. 빌더가 `GPUInstance::_sprite` 로 옮깁니다.
         * @details 기본값(텍스처 전체, 흰색)이면 아무 일도 하지 않는다. 읽는 셰이더는 sprite_2d.hlsl 이고, 값을 정하는 쪽은 파생 타입
         *          (`SpriteComponent`)이다. 그래서 세터는 protected 이다(읽는 셰이더가 없는 메시에 색을 줄 수 있는 것처럼 보이지 않게 하려는 것이다).
         */
        const GPUSpriteInstanceData& getSpriteInstanceData() const { return _spriteInstanceData; }
        /**
         * @brief 2D 픽셀 스냅 단위(에셋 픽셀 하나의 월드 길이 = 1 / PPU, 0 이면 끔)를 정합니다. 값이 달라졌을 때만 렌더 상태를 더티로 표시합니다.
         * @details 픽셀 퍼펙트 카메라가 줌이 바뀔 때 씬의 모든 메시에 알리고, 새로 등록되는 메시는 카메라 레지스트리의 값을 읽는다. 읽는 셰이더는 sprite_2d 이다.
         */
        void setPixelSnapUnit( float32 unit );

        /**
         * @brief 투명 큐의 정렬 키입니다(`Render2DSettings::makeSortKey`: 정렬 레이어와 레이어 안 순서). 0 은 `Default` 레이어, 순서 0 입니다.
         * @details 투명 물체는 이 키가 작은 것부터 그려지고, 키가 같을 때만 깊이로 구분한다(유니티의 `Renderer.sortingLayerID` 와 `sortingOrder`).
         *          저장하지 않는다. 저장되는 이름과 순서는 파생 타입(`SpriteComponent`)이 가지고 있다가 여기로 변환해 넣는다. 불투명은 이 키를 보지 않는다.
         */
        void setSortKey( uint32 sortKey );
        /** @brief 투명 큐의 정렬 키를 반환합니다. 0 은 기본(`Default` 레이어, 순서 0)입니다. */
        uint32 getSortKey() const { return _sortKey; }

        /**
         * @brief 강조(상호작용 외곽선 · 감각 모드)를 정합니다. 빌더가 `GPUInstance::_highlight` 로 옮기고 강조 패스(highlight_outline.hlsl)가 그립니다.
         * @details 배치 키가 아니라 인스턴스 내용이라 켜고 꺼도 배치를 다시 나누지 않는다. 저장하지 않는 런타임 값이다 — 상호작용 쪽(`InteractionHighlightComponent`)이 매 프레임 정한다.
         *          불투명 배치만 그린다(반투명 메시는 강조 패스가 보지 않는다).
         */
        void setHighlight( InstanceHighlightStyle style, const float3& color );
        /** @brief `setHighlight` 로 정한 값(`GPUInstanceHighlight::pack`, 0 이면 없음)입니다. */
        uint32 getHighlight() const { return _highlight; }

        /** @brief 바운드 반지름(메시 공간)의 최솟값을 설정합니다. 메시가 아는 경계보다 작으면 메시의 값이 쓰이므로 키우기만 합니다. */
        void setBoundsRadius( float32 radius );
        /** @brief 바운드 반지름(메시 공간)입니다. 설정한 값과 메시의 경계(`Mesh::getBoundingRadius`) 중 큰 쪽입니다. 월드 반지름은 `getWorldBounds` 가 반환합니다. */
        float32 getBoundsRadius() const;
        /** @brief 월드 위치를 중심으로, 메시 반지름에 월드 행렬의 최대 축 스케일을 곱한 구를 반환합니다. GPU 컬링도 같은 값을 씁니다. */
        bool getWorldBounds( float3& outCenter, float32& outRadius ) const override;
        /** @brief 메시 공간의 경계(`Mesh::getLocalBoundsMin/Max`, 메시가 없으면 단위 상자)를 월드로 옮긴 상자를 반환합니다. */
        bool getWorldBox( AABB& outBox ) const override;

        /**
         * @brief 그릴 메시 구간(`MeshSection`)의 이름을 정합니다. 비면 메시 전체를 그립니다. 저장됩니다(PROPERTY).
         * @details 한 메시의 구간마다 컴포넌트가 하나다(머티리얼이 구간마다 다르다). 스킨드 메시는 구간 컴포넌트(`MeshSectionComponent`)가 같은 메시 객체를 그려
         *          포즈 · 스킨 · 모프가 한 벌이다. 메시에 없는 이름이면 그리지 않고 해석할 때 오류를 남긴다.
         */
        void          setSectionName( hashed_string sectionName );
        hashed_string getSectionName() const { return _sectionName; }
        /**
         * @brief 그릴 정점 범위입니다. 구간 이름이 비면 (0, 0)(메시 전체 = LOD 0)이고 true, 메시에 그 구간이 없거나 구간이 비었으면 false(그리지 않는다)입니다.
         *        LOD 단계(`setLODIndex`)가 1 이상이면 그 단계의 범위(`Mesh::getLODs`, 그릴 정점 흐름 기준)입니다.
         * @details 렌더 수집(`GPUSceneBuilder::fillCandidateFromPrimitive`)이 워커에서 부르므로 로그를 남기지 않는다 — 오류는 `resolveRenderAssets` 가 남긴다.
         */
        [[nodiscard]] bool findDrawSection( uint32& outFirstVertex, uint32& outVertexCount ) const;
        /**
         * @brief 그릴 LOD 단계를 정합니다(0 = 원본). 메시의 단계 수를 넘으면 가장 낮은 단계입니다. 바뀌면 렌더 상태를 더티로 표시합니다(저장하지 않는 런타임 값).
         * @details 단계는 메시 LOD 고르기(`MeshLODSystem`)가 화면 크기로 정한다. 단계가 하나뿐인 메시에는 뜻이 없다.
         */
        void setLODIndex( uint32 lodIndex );
        /** @brief 지금 그리는 LOD 단계입니다(0 = 원본). */
        uint32 getLODIndex() const { return _lodIndex; }
        /**
         * @brief 1인칭 시점인지를 정합니다(저장하지 않는 런타임 값). 구간의 시점 가시성(`MeshPersonVisibility`)이 이 값으로 보이고 숨습니다.
         * @details VRM firstPerson 의 자리다(1인칭 카메라에 머리 · 머리카락이 걸리지 않게). 시점은 컴포넌트 단위라 모든 뷰가 같은 판정을 쓴다 — 분할 화면에서
         *          다른 플레이어가 보는 뷰는 따로 고르지 않는다(언리얼 OwnerNoSee 처럼 뷰마다 가르려면 뷰의 소유자 칸이 필요하다).
         */
        void setFirstPersonView( bool bFirstPerson );
        bool isFirstPersonView() const { return _bFirstPersonView == SW_TRUE; }
        /** @brief 오브젝트의 모든 메시 컴포넌트에 1인칭 시점을 정합니다(카메라가 1인칭으로 들어가고 나올 때 부른다). */
        static void setFirstPersonViewOnObject( const GameObject& owner, bool bFirstPerson );
        /** @brief 그리는 구간이 지금 시점(1인칭 · 3인칭)에서 보이는지입니다. 구간이 없으면 늘 보입니다. */
        bool isVisibleInCurrentPerson() const;
        /**
         * @brief 등장 비율(0 ~ 1)입니다. 1 이면 다 보이고, 0 이면 그리지 않습니다. 그 사이는 디더로 그 비율만큼의 픽셀만 그립니다(`GPUInstance::_appearance`).
         * @details 비동기로 읽은 메시가 도착하면 0 에서 `_appearSeconds` 동안 1 로 오른다(한 프레임에 튀지 않게). 캐릭터는 애니메이션이 준비될 때까지 0 에 머문다
         *          (`pollReadyToAppear`). 바로 보이는 메시(내장 도형, 이미 읽은 에셋)는 1 그대로다.
         */
        float32 getAppearance() const { return _appearance; }
        /** @brief 0 에서(진행 중이면 지금 값에서) `_appearSeconds` 동안 서서히 나타납니다 — 텔레포트처럼 그 자리에 새로 나타나는 경우에 부릅니다. */
        void beginAppearance();
        /** @brief 지금 값에서 `_appearSeconds` 동안 서서히 사라집니다. 0 이 되면 그리지 않습니다 — 지우기 전에 부르고 `isFullyDisappeared` 뒤에 지웁니다. */
        void beginDisappearance();
        /**
         * @brief 순간이동(`SceneComponent::teleportTo`)했을 때 `_bAppearOnTeleport` 면 새 자리에서 다시 나타납니다. 틱 중(워커)에도 불리므로 표시만 하고, 씬 프레임의
         *        도착 단계가 등장을 시작한다.
         */
        void onTeleported() override;
        /** @brief 사라지기가 끝났으면(등장 비율 0, 진행 없음) true 입니다. */
        bool isFullyDisappeared() const { return _appearance <= 0.0f && _appearanceRate == 0.0f && _bAwaitingAppearance == SW_FALSE; }
        /** @brief 등장 · 사라지기 시간(초) 기본값입니다. 컴포넌트마다 `_appearSeconds` 로 바꿉니다(0 이면 바로). */
        static constexpr float32 kDefaultAppearSeconds = 0.4f;

        /** @brief 가시 여부를 설정합니다. */
        void setVisible( bool bVisible );
        /** @brief 가시 여부를 반환합니다. */
        bool isVisible() const { return _bVisible == SW_TRUE; }

        /**
         * @brief 렌더 스냅샷이 다시 읽어야 할 상태로 표시합니다.
         * @details 세터, PROPERTY 편집, 월드 트랜스폼 갱신이 모두 이 함수로 모인다. 렌더러가 매 프레임
         *          전부 훑어 "뭐가 바뀌었나"를 되묻는 대신, 바꾼 쪽이 알린다.
         */
        void markRenderStateDirty();
        /** @brief 프리미티브 레지스트리에 자기를 등록합니다. 여기서 타입이 한 번 확정됩니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 프리미티브 레지스트리에서 자기를 제거하고, 잡아 둔 머티리얼 참조를 해제합니다. 여러 번 불러도 안전합니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 인스펙터나 직렬화가 PROPERTY 를 바꾸면 렌더 상태를 더티로 표시합니다. */
        void onPropertyChanged( hashed_string propertyName ) override;
        /**
         * @brief 쓰지 않습니다(`final`). 메시는 월드가 바뀌면 트랜스폼 슬롯에 적힌 프리미티브 번호로 레지스트리에 바로 더티를 기록합니다.
         * @details 생성자에서 이 훅의 알림을 끈다. 틱 뒤 적용이 움직인 메시마다 컴포넌트를 일일이 거치지 않게 하려는 것이다. 파생 타입이
         *          덮어써도 불리지 않으므로 아예 막는다.
         */
        void onWorldTransformUpdated() final {}
        /** @brief 소유 오브젝트가 켜지거나 꺼지면 렌더 상태를 더티로 표시합니다(스냅샷 포함 여부가 바뀝니다). */
        void onOwnerActiveInHierarchyChanged() override;

    protected:
        /** @brief `_meshID` 가 비었을 때의 메시 id 입니다. 빈 문자열이면 단위 큐브입니다(`MeshUtil::acquirePrimitive`). */
        virtual string_view getDefaultMeshID() const { return {}; }
        /** @brief `_materialPath` 가 비었을 때의 머티리얼 경로입니다. 비어 있으면 씬 기본 머티리얼이며, 렌더러가 고릅니다. */
        virtual hashed_string getDefaultMaterialPath() const { return {}; }
        /** @brief 스프라이트 프레임과 색을 바꿉니다. 값이 달라졌을 때만 렌더 상태를 더티로 표시하므로, 같은 프레임을 다시 넣는 애니메이터는 비용이 없습니다. */
        void setSpriteInstanceData( const GPUSpriteInstanceData& data );
        /** @brief 구간 이름이 지금 메시에 있는지 보고, 없으면 오류를 남깁니다(그리지 않는다 — `findDrawSection`). 메시가 바뀌거나 이름이 바뀔 때 부릅니다. */
        void validateSectionName() const;
        /** @brief 이 컴포넌트의 메시를 따라 그리는 구간 컴포넌트(`MeshSectionComponent`)에 메시가 바뀌었다고 알립니다. */
        void notifyMeshFollowers();
        /** @brief 메시를 바꾸되 따라 그리는 구간 컴포넌트에 알리지 않습니다 — 구간 컴포넌트가 자기 원본을 따라갈 때 쓴다(서로 알리며 돌지 않게). */
        void assignMeshWithoutFollowers( shared_ptr<Mesh> mesh );
        /**
         * @brief `_mesh` 를 바꾼 뒤 부릅니다 — 구간 이름을 확인하고, LOD 단계를 원본으로 돌리고, 메시의 단계가 여럿이면 LOD 목록에 걸고(아니면 뗀다), 렌더 상태를 더티로 표시합니다.
         * @details 메시를 바꾸는 길(해석 · 도착 · 런타임 지정 · 구간 따라가기)이 모두 이 함수로 모인다 — 한 길이라도 빠지면 LOD 목록에 남은 메시가 옛 단계를 그린다.
         */
        void onMeshAssigned();
        /**
         * @brief 나타날 준비가 됐는지 묻습니다(씬 프레임마다, 게임 스레드, 등장을 미루는 동안). 기본은 늘 준비됨입니다.
         * @details 캐릭터(스켈레탈 메시)는 붙은 애니메이터의 클립이 다 읽혔을 때 준비된다 — 애니메이션 없이 기본 자세로 한 프레임이라도 보이지 않게.
         *          묻는 김에 기다리던 쪽을 마무리해도 된다(애니메이터가 그래프를 짓고 시작 상태를 재생한다).
         */
        virtual bool pollReadyToAppear() { return true; }
        /** @brief 등장 비율을 0 으로 두고 `pollReadyToAppear` 가 true 가 될 때까지 기다렸다가 나타납니다(파생 타입이 준비가 늦어질 때 부릅니다). */
        void holdAppearanceUntilReady();

    private:
        /** @brief 메시 로드 · 등장 대기 · 등장 진행 동안 씬 프레임마다 불립니다(`PrimitiveRegistry::addAssetWaiter`). 아직 할 일이 있으면 true 입니다. */
        static bool updateArrival( Component& component, float32 deltaSeconds );
        /** @brief 끝난 메시 로드의 결과를 메시로 삼고 핸들을 놓습니다. 실패면 메시 없이 둔다. */
        void applyMeshLoad();
        /** @brief 등장(@p direction 이 양수) 또는 사라지기(음수)를 시작합니다. `_appearSeconds` 가 0 이면 바로 끝 값입니다. */
        void startAppearanceRamp( float32 direction );
        /** @brief 등장 대기와 진행을 한 프레임 나아갑니다. 아직 할 일이 있으면 true 입니다. */
        bool advanceAppearance( float32 deltaSeconds );

    private:
        shared_ptr<Mesh>             _mesh;
        MeshHandle                   _meshLoad; ///< 읽는 중인 메시 에셋 로드다(저장하지 않는다). 끝나면 `applyMeshLoad` 가 놓는다
        Material*                    _pMaterial;
        shared_ptr<MaterialInstance> _materialInstance;
        /** @brief 저장되는 메시 id 입니다. */
        PROPERTY( Category = "Rendering", DisplayName = "Mesh Asset", AssetPath, AssetType = "Mesh", Tooltip = "Mesh asset name or path" )
        string _meshID;
        /** @brief 저장되는 머티리얼 참조입니다. */
        PROPERTY( Category = "Rendering", DisplayName = "Material", AssetPath, AssetType = "Material", Tooltip = "Material asset; empty uses the scene default" )
        hashed_string _materialPath;
        hashed_string _acquiredMaterialPath;  ///< 캐시에서 잡아 둔 경로다(저장하지 않는다). 인스펙터가 `_materialPath` 를 먼저 고쳐 써도 이 경로로 해제한다
        hashed_string _requestedMaterialPath; ///< 마지막으로 해석한 요청 경로다(저장하지 않는다). 누락 머티리얼로 대체한 뒤에도 같은 요청을 다시 시도하거나 경고하지 않는다
        hashed_string _resolvedMeshID;        ///< `_mesh` 가 어느 메시 id 의 것인지 나타낸다(저장하지 않는다). 현재 id 와 다르면 다시 잡는다
        PROPERTY( Category = "Rendering", DisplayName = "Bounds Radius", Tooltip = "Bounding sphere radius", Min = 0.0, Units = m )
        float32 _boundsRadius;
        PROPERTY( Category = "Rendering", DisplayName = "Blend Mode", Tooltip = "RHI blend mode for rasterization" )
        RHIBlendMode _blendMode;
        PROPERTY( Category = "Rendering", DisplayName = "GPU Spin Seed", Tooltip = "Non-zero makes the GPU spin this instance; the seed picks its speed" )
        uint32 _gpuSpinSeed;
        PROPERTY( Category = "Rendering", DisplayName = "Section", Tooltip = "Mesh section to draw (material section of an imported model); empty draws the whole mesh" )
        hashed_string _sectionName;
        /** @brief GPU 인스턴스의 스프라이트 슬롯 데이터입니다. 저장하지 않으며, 파생 타입의 저장되는 값(프레임, 색)에서 만듭니다. */
        GPUSpriteInstanceData _spriteInstanceData;
        /** @brief 정점 애니메이션 시각 오프셋(초)입니다. 저장하지 않습니다(setVertexAnimationPhase). */
        float32 _vertexAnimationPhase;
        /** @brief 투명 큐의 정렬 키입니다(0 = 기본). 저장하지 않습니다. */
        uint32 _sortKey;
        /** @brief 강조(`GPUInstanceHighlight::pack`, 0 = 없음)입니다. 저장하지 않습니다. */
        uint32 _highlight;
        PROPERTY( Category = "Rendering", DisplayName = "Appear Seconds", Tooltip = "Dither fade-in time after the mesh finishes loading (and for beginAppearance/beginDisappearance); 0 shows it at once", Min = 0.0, Max = 5.0, Units = s )
        float32 _appearSeconds;
        float32 _appearance;     ///< 등장 비율(0 ~ 1, 저장하지 않음). `getAppearance`
        float32 _appearanceRate; ///< 등장 비율의 초당 변화(양수 등장 · 음수 사라지기 · 0 멈춤, 저장하지 않음)
        /** @brief 순간이동이 표시한 등장 요청입니다(`onTeleported`, 워커가 쓴다). 도착 단계가 꺼내 `beginAppearance` 한다. 비트필드에 두면 이웃 비트와 레이스다. */
        atomic<bool> _bTeleportAppearPending;
        /** @brief 그리는 LOD 단계입니다(0 = 원본, `setLODIndex`). 저장하지 않습니다. 게임 스레드만 쓴다(렌더 수집은 그 프레임의 더티 뒤에 읽는다). */
        uint8 _lodIndex;
        /** @brief 등록할 때 받은 레지스트리입니다. 더티 표시는 소유자를 거치지 않고 여기로 바로 갑니다. */
        PrimitiveRegistry* _pPrimitiveRegistry;
        // "더티" 비트는 여기 없다. 레지스트리의 원자 플래그 하나가 기준이다. 더티를 이 비트필드에 두면 워커의 더티 쓰기가
        // `_bVisible` 과 같은 바이트를 읽고 고쳐 쓰는 것이라, 이웃 비트를 만지는 스레드와 레이스가 된다.
        /** @brief 그리는지 여부입니다. 저장됩니다(PROPERTY). 숨긴 메시가 Stop, 되돌리기, 씬 다시 열기 뒤에도 숨은 채로 남습니다. */
        PROPERTY( Category = "Rendering", DisplayName = "Visible", Tooltip = "Draw this mesh" )
        uint8 _bVisible            : 1;
        uint8 _bFirstPersonView    : 1; ///< 1인칭 시점인지(저장하지 않음, `setFirstPersonView`) — 게임 스레드만 쓴다
        uint8 _bAwaitingAppearance : 1; ///< 등장을 미루는 중이다(`holdAppearanceUntilReady`, 저장하지 않음)
        PROPERTY( Category = "Rendering", DisplayName = "Appear On Teleport", Tooltip = "Dither in again after teleportTo (respawn); off for network corrections and camera cuts" )
        uint8 _bAppearOnTeleport : 1;
        uint8 _reserved          : 4;
    };
} // namespace sw
