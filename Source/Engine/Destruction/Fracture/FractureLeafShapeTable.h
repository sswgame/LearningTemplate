/**
 * @file FractureLeafShapeTable.h
 * @brief 파괴 잎 셰이프(조각의 볼록 껍질)를 에셋 · 배율 · 껍질 축소 · 재질이 같은 파괴물끼리 나눠 쓰는 표입니다.
 * @details 같은 벽 수백 장이 한 에셋을 쓰면 잎 셰이프도 한 벌이다 — 파괴물마다 따로 지으면 짓는 시간과 메모리가 파괴물 수에 비례한다. 언리얼 `UBodySetup` 이 볼록 껍질을
 *          에셋에 하나만 들고 바디가 나눠 쓰는 것, 유니티 `MeshCollider` 가 구운 메시를 나눠 쓰는 것과 같다. 물리 씬의 셰이프 핸들은 씬의 것이라 표는 씬마다 하나다(씬 서비스).
 *          소유는 나눠 받은 파괴물에 있다 — 마지막 파괴물이 놓으면 묶음이 셰이프를 지운다. 이미 선 바디는 백엔드가 참조를 세므로 그대로 산다.
 *          게임 스레드의 물리 프레임(`FixedStepGroup::FrameBegin`)에서만 쓴다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/Cache/WeakInternTable.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Physics/IPhysicsSceneFwd.h"
#include "Engine/Physics/PhysicsTypes.h"

namespace sw
{
    class GameObjectManager;
    struct FractureAsset;

    /** @brief 파괴 조각의 충돌 껍질을 에셋에서 만드는 계산입니다. */
    struct FractureLeafHullUtil
    {
        static constexpr uint32 kMaxPolygonPoint2D = 8; ///< 2D 물리 다각형의 최대 점 수다.

        /** @brief 잎 껍질 점을 오브젝트 원점 기준으로 만듭니다. (무게 중심 + 축소한 껍질) × 배율입니다. */
        static void makeLeafHull( const FractureAsset& asset, uint32 leaf, float32 scale, float32 shrink, vector<float3>& outListPoint );
        /** @brief 2D 볼록 껍질(반시계, 모노톤 체인)을 만들고, 점이 많으면 넓이를 가장 적게 잃는 점부터 빼서 @p maxPoint 개로 줄입니다. */
        static void makeConvexPolygon( const vector<float3>& listPoint, uint32 maxPoint, vector<float2>& outListPolygon );
    };
} // namespace sw

namespace sw
{
    /**
     * @class FractureLeafShapeSet
     * @brief 에셋 하나의 잎 셰이프 한 벌입니다. 셰이프는 처음 쓰는 자리나 미리 짓기(`warm`)에서 하나씩 만들고, 묶음이 사라질 때 모두 지웁니다.
     */
    class SW_API FractureLeafShapeSet
    {
    public:
        FractureLeafShapeSet( IPhysicsScene3D& scene, shared_ptr<const FractureAsset> asset, float32 scale, float32 shrink, const hashed_string& material );
        FractureLeafShapeSet( IPhysicsScene2D& scene, shared_ptr<const FractureAsset> asset, float32 scale, float32 shrink, const hashed_string& material );
        ~FractureLeafShapeSet();

        FractureLeafShapeSet( const FractureLeafShapeSet& )            = delete;
        FractureLeafShapeSet& operator=( const FractureLeafShapeSet& ) = delete;

        /** @brief 잎 @p leaf 의 셰이프입니다. 없으면 만듭니다. 물리가 거절한 껍질이면 무효 핸들입니다(물리가 오류를 남긴다). */
        PhysicsShapeHandle getOrCreateShape( uint32 leaf );
        /**
         * @brief 아직 없는 잎 셰이프를 잎 순서대로 만듭니다. 이미 있는 잎(먼저 깨져 만든 것)은 건너뜁니다.
         * @param deadlineNanoseconds 하나를 만든 뒤 이 시각(`MonotonicClock::nowNanoseconds`)이 지났으면 멈춥니다. 0 이면 끝까지 만듭니다.
         * @return 이번에 만든 수입니다.
         */
        uint32 warm( int64 deadlineNanoseconds );

        /** @brief 아직 만들지 않은 잎 셰이프 수입니다. */
        uint32 getPendingCount() const { return _pendingCount; }
        /** @brief 이 묶음의 재질 이름입니다. */
        const hashed_string& getMaterial() const { return _material; }

    private:
        FractureLeafShapeSet( IPhysicsScene3D* pScene3D, IPhysicsScene2D* pScene2D, shared_ptr<const FractureAsset> asset, float32 scale, float32 shrink, const hashed_string& material );

    private:
        IPhysicsScene3D*                _pScene3D; ///< 둘 중 하나만 있다
        IPhysicsScene2D*                _pScene2D;
        shared_ptr<const FractureAsset> _asset;
        vector<PhysicsShapeHandle>      _listShape; ///< 잎마다 하나다
        hashed_string                   _material;
        float32                         _scale;
        float32                         _shrink;
        uint32                          _pendingCount;
        uint32                          _nextWarmLeaf; ///< 다음에 미리 만들 잎이다. 앞쪽 잎은 이미 시도했다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class FractureLeafShapeTable
     * @brief 씬 하나의 잎 셰이프 묶음 표입니다. 규칙은 파일 머리말을 보세요.
     */
    class SW_API FractureLeafShapeTable final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "FractureLeafShapeTable";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        explicit FractureLeafShapeTable( GameObjectManager& manager );
        ~FractureLeafShapeTable() override = default;

        FractureLeafShapeTable( const FractureLeafShapeTable& )            = delete;
        FractureLeafShapeTable& operator=( const FractureLeafShapeTable& ) = delete;

        /** @brief 프레임마다 할 일이 없습니다. */
        void update( float32 deltaTime ) override { (void)deltaTime; }
        /** @brief 표를 비웁니다. 쥔 파괴물의 묶음은 그대로 살고 놓을 때 사라집니다. */
        void onSceneCleared() override { _table.clear(); }

        /** @brief 그 조건의 묶음을 나눠 받습니다. 처음이면 만듭니다(셰이프는 아직 없다). 받은 쪽은 물리 씬보다 먼저 놓습니다. */
        shared_ptr<FractureLeafShapeSet> acquire( IPhysicsScene3D& scene, const shared_ptr<const FractureAsset>& asset, float32 scale, float32 shrink, const hashed_string& material );
        /** @copydoc acquire */
        shared_ptr<FractureLeafShapeSet> acquire( IPhysicsScene2D& scene, const shared_ptr<const FractureAsset>& asset, float32 scale, float32 shrink, const hashed_string& material );

        /** @brief 지금 누군가 쥔 묶음 수입니다. */
        size_t countLive() const { return _table.countLive(); }

    private:
        /** @brief 묶음의 조건입니다. 에셋은 묶음이 쥐고 있어 주소가 다른 에셋에 다시 쓰이지 않는다(새로 읽은 에셋은 다른 주소 = 다른 묶음). */
        struct Key
        {
            const void*          _pScene;
            const FractureAsset* _pAsset;
            hashed_string        _material;
            float32              _scale;
            float32              _shrink;

            bool operator==( const Key& other ) const
            {
                return _pScene == other._pScene && _pAsset == other._pAsset && _material == other._material && _scale == other._scale && _shrink == other._shrink;
            }
        };

        struct KeyHash
        {
            size_t operator()( const Key& key ) const;
        };

    private:
        WeakInternTable<Key, FractureLeafShapeSet, KeyHash> _table;
    };
} // namespace sw
