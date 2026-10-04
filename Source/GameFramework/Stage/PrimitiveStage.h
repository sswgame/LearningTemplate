/**
 * @file PrimitiveStage.h
 * @brief 내장 도형 · 직접 지은 메시로 무대를 세우는 도우미 — 활성 씬 잡기, 세운 오브젝트 추적, 색 · 텍스처 머티리얼 인스턴스 캐시, 해, 카메라.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class CameraComponent;
    class GameObject;
    class GameObjectManager;
    class Material;
    class MaterialInstance;
    class Mesh;
    class MeshComponent;
    class Scene;

    /** @brief 메시 하나가 입는 모습 — 색, 있으면 텍스처, 있으면 씬 기본이 아닌 머티리얼(반투명 `engine/materials/glassmaterial.material` 등)입니다. */
    struct SW_GF_API PrimitiveLook
    {
        float4 _color{ 1.0f, 1.0f, 1.0f, 1.0f };
        string _texturePath{};  ///< `albedoMap` 으로 걸 텍스처(비면 없음)
        string _materialPath{}; ///< 비면 씬 기본 머티리얼(불투명 · 빛 받음)

        /** @brief 색만 다른 불투명 모습입니다. */
        static PrimitiveLook makeColor( const float4& color );
        /** @brief 반투명 유리 머티리얼의 모습입니다(알파가 색의 w). */
        static PrimitiveLook makeTranslucent( const float4& color );
    };
} // namespace sw

namespace sw
{
    /**
     * @class PrimitiveStage
     * @brief 시험 게임이 절차로 세우는 무대입니다. 세운 오브젝트를 핸들로 들고 있다가 한 번에 걷고, 활성 씬이 바뀌면(에디터가 다른 씬을 열면) 알아챕니다.
     * @details 같은 (머티리얼, 색, 텍스처)의 인스턴스는 하나를 나눠 씁니다 — 배치 키가 인스턴스라 오브젝트마다 만들면 드로우가 갈라집니다.
     *          모든 함수는 씬 틱 밖(게임 `onUpdate`)에서 부릅니다. 틱 안에서 세워야 하면 `GameObjectManager::executeOrDeferPostTick` 으로 미룹니다.
     *          언리얼로 치면 레벨 스크립트가 런타임에 스폰한 액터 목록 + 동적 머티리얼 인스턴스 캐시의 자리입니다.
     */
    class SW_GF_API PrimitiveStage
    {
    public:
        PrimitiveStage();
        ~PrimitiveStage();

        PrimitiveStage( const PrimitiveStage& )            = delete;
        PrimitiveStage& operator=( const PrimitiveStage& ) = delete;

        /** @brief 활성 씬(없으면 @p sceneName 의 새 빈 씬)을 잡고 기본 카메라를 둡니다. 씬 서비스가 아직 없으면 false 입니다. */
        [[nodiscard]] bool begin( string_view sceneName );
        /** @brief 세운 오브젝트를 모두 지우고 캐시를 비웁니다. */
        void clear();
        /** @brief 잡은 씬이 더는 활성 씬이 아니면 true 입니다 — 핸들이 옛 씬의 것이니 `forget` 하고 다시 세웁니다. */
        bool isSceneChanged() const;
        /** @brief 지우지 않고 핸들 · 캐시만 버립니다(씬이 이미 바뀌었다). */
        void forget();
        bool isActive() const { return _bActive != SW_FALSE; }

        /** @brief 메시 하나짜리 오브젝트를 세웁니다. @p mesh 가 비면 nullptr 입니다. */
        GameObject* createMeshObject( const utf8* pName, const shared_ptr<Mesh>& mesh, const PrimitiveLook& look, const float3& position,
                                      const float3& scale = float3{ 1.0f, 1.0f, 1.0f }, const float3& rotation = float3{ 0.0f, 0.0f, 0.0f } );
        /** @brief 내장 도형(`MeshUtil::acquirePrimitive` — "Cube" · "Sphere" · "Capsule" · "Cylinder" · "Cone" · "Plane")으로 세웁니다. */
        GameObject* createPrimitiveObject( const utf8* pName, string_view meshId, const PrimitiveLook& look, const float3& position,
                                           const float3& scale = float3{ 1.0f, 1.0f, 1.0f }, const float3& rotation = float3{ 0.0f, 0.0f, 0.0f } );
        /** @brief 해(방향광)를 세웁니다. @p euler 는 피치 · 요 · 롤(라디안), @p shadowExtent 는 그림자가 덮는 반지름입니다. */
        GameObject* createSun( const float3& euler, float32 intensity, float32 shadowExtent );
        /** @brief 게임이 직접 만든 오브젝트(스프라이트 · 빛 등)를 추적에 넣습니다 — `clear` 가 함께 지운다. */
        void adoptObject( const GameObject& object );
        /** @brief 오브젝트를 지우고 추적에서 뺍니다. */
        void destroyObject( GameObjectHandle handle );
        /** @brief 메시의 모습을 바꿉니다(캐시된 인스턴스로). */
        void setLook( MeshComponent& meshComponent, const PrimitiveLook& look );
        /** @brief 캐시된 인스턴스를 찾거나 만듭니다. 부모 머티리얼이 없으면 nullptr 입니다. */
        shared_ptr<MaterialInstance> acquireInstance( Material* pMaterial, const PrimitiveLook& look );

        /** @brief 씬의 모든 카메라를 @p position 에서 @p target 을 보게 둡니다(롤 없음). @p orthoHeight 가 0 보다 크면 직교 투영입니다. */
        void placeCameras( const float3& position, const float3& target, float32 orthoHeight = 0.0f, float32 farPlane = 300.0f );
        /** @brief 씬의 모든 카메라를 오일러 회전(피치 · 요 · 롤)으로 둡니다 — 1인칭 · 코스터 탑승 시점(롤이 있다). */
        void placeCamerasWithRotation( const float3& position, const float3& euler, float32 fieldOfViewY, float32 farPlane = 300.0f );

        Scene*             getScene() const;
        GameObjectManager* getObjectManager() const;
        GameObject*        resolveObject( GameObjectHandle handle ) const;
        MeshComponent*     findMesh( GameObjectHandle handle ) const;
        uint32             getObjectCount() const { return static_cast<uint32>( _listObject.size() ); }

    private:
        /** @brief 캐시 한 칸입니다. */
        struct InstanceEntry
        {
            shared_ptr<MaterialInstance> _instance{};
            Material*                    _pMaterial{ nullptr };
            float4                       _color{};
            string                       _texturePath{};
        };

        void applyLook( MeshComponent& meshComponent, const PrimitiveLook& look );

        vector<GameObjectHandle> _listObject;
        vector<InstanceEntry>    _listInstance;
        uint64                   _sceneGeneration;
        uint8                    _bActive;
    };
} // namespace sw
