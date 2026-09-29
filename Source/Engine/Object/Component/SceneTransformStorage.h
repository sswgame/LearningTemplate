/**
 * @file SceneTransformStorage.h
 * @brief 씬 컴포넌트 트랜스폼 값(로컬 TRS · 월드 행렬 · LWC)의 전역 저장소입니다. 값마다 연속 배열이고, 컴포넌트는 칸 번호와 페이지만 듭니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class SceneComponent;

    /**
     * @struct SceneTransformPage
     * @brief 트랜스폼 칸 256 개입니다. 값마다 배열 하나라, 같은 값을 연달아 도는 패스(틱 뒤 적용 · 플러시 · 렌더 수집)가 줄 단위로 읽습니다.
     * @details 한 칸의 값은 모든 배열의 같은 자리(`칸 번호 & kSlotMask`)에 있습니다. 페이지는 한 번 만들면 옮기지도 놓지도 않으므로(저장소가
     *          사라질 때까지), 컴포넌트는 자기 페이지 포인터를 들고 표를 거치지 않고 찾습니다.
     *
     *          예전에는 이 값들이 280 B 짜리 `SceneComponent` 안에 흩어져 있었습니다. 틱 뒤 적용 · 렌더 수집이 8000 개를 돌 때마다 객체마다 캐시
     *          줄 서넛을 건너다녔고, 행렬 하나를 읽으려고 컴포넌트 전체를 끌어왔습니다.
     */
    struct alignas( 64 ) SceneTransformPage
    {
        static constexpr uint32 kSlotShift = 8;
        static constexpr uint32 kSlotCount = 1u << kSlotShift;
        static constexpr uint32 kSlotMask  = kSlotCount - 1u;

        /** @brief `_arrFlag` 비트입니다. 게임 스레드가 구조를 바꿀 때(붙이기 · 떼기 · 등록)만 씁니다. */
        enum SlotFlag : uint8
        {
            kHasParent   = 1u << 0, ///< 부모에 붙어 있습니다
            kHasChildren = 1u << 1, ///< 자식이 하나 이상 있습니다
            kNotifyOwner = 1u << 2, ///< 월드가 바뀌면 소유 컴포넌트의 `onWorldTransformUpdated` 를 부릅니다
        };
        /** @brief `_arrPendingMask` 비트입니다. 틱 중에 쓴 로컬 값 가운데 어느 것이 적용을 기다리는지 나타냅니다. */
        enum PendingBit : uint8
        {
            kPendingPosition = 1u << 0,
            kPendingRotation = 1u << 1,
            kPendingScale    = 1u << 2,
        };

        float4x4        _arrWorldMatrix[kSlotCount];      ///< 계층을 합성한 월드 행렬
        double3         _arrWorldPositionLwc[kSlotCount]; ///< float64 로 누적한 월드 위치(LWC)
        float3          _arrLocalPosition[kSlotCount];    ///< 로컬 위치(PROPERTY `_localPosition`)
        float3          _arrLocalRotation[kSlotCount];    ///< 로컬 오일러 회전(PROPERTY `_localRotation`)
        float3          _arrLocalScale[kSlotCount];       ///< 로컬 스케일(PROPERTY `_localScale`)
        quaternion      _arrRotationQuat[kSlotCount];     ///< `_arrRotationSource` 로 만든 쿼터니언입니다(회전 변환 캐시)
        float3          _arrRotationSource[kSlotCount];   ///< `_arrRotationQuat` 을 만든 오일러 회전입니다
        float3          _arrPendingPosition[kSlotCount];  ///< 틱 중에 쓴 로컬 위치(적용 전)
        float3          _arrPendingRotation[kSlotCount];  ///< 틱 중에 쓴 로컬 회전(적용 전)
        float3          _arrPendingScale[kSlotCount];     ///< 틱 중에 쓴 로컬 스케일(적용 전)
        SceneComponent* _arrOwner[kSlotCount];            ///< 칸을 쓰는 컴포넌트. 빈 칸이면 nullptr 입니다
        uint32          _arrPrimitiveIndex[kSlotCount];   ///< 렌더 프리미티브 번호(`PrimitiveRegistry`). 없으면 `SceneTransformStorage::kNoPrimitive`
        uint8           _arrPendingMask[kSlotCount];      ///< `PendingBit` 조합. 칸의 주인 오브젝트를 틱하는 스레드만 씁니다
        uint8           _arrFlag[kSlotCount];             ///< `SlotFlag` 조합
    };

    /**
     * @class SceneTransformStorage
     * @brief 모든 씬 컴포넌트의 트랜스폼 값을 담는 전역 저장소입니다(유니티 `TransformHierarchy` 의 자리).
     * @details **컴포넌트는 만들어질 때 칸을 받고 없어질 때 놓습니다.** 씬에 붙지 않은 컴포넌트(스택에 만든 것 · 기본값 비교용)도 칸이
     *          있으므로, 등록 여부에 따라 값의 자리가 바뀌는 일이 없습니다. 리플렉션은 `SceneComponent` 의 값 접근자(PROPERTY 를 붙인 참조
     *          반환 메서드)로 칸을 찾으므로 직렬화 키(`_localPosition` …)와 씬 파일은 그대로입니다.
     *
     *          왜 씬(매니저)마다가 아니라 하나인가: 칸 번호가 컴포넌트의 수명 동안 바뀌지 않아야 렌더 등록부 · 틱 대기 목록이 번호만 들고
     *          있을 수 있습니다. 매니저마다 두면 씬에 붙기 전 · 떨어진 뒤의 컴포넌트를 위해 값을 옮겨야 하고, 옮기는 순간 번호가 바뀝니다.
     *
     *          언리얼의 `USceneComponent` 는 지금까지의 우리처럼 컴포넌트가 값을 직접 듭니다(대량 이동은 ISM · Mass 로 우회합니다). 게임
     *          오브젝트 모델은 그대로 두고 트랜스폼만 연속 배열로 옮기는 것은 유니티 쪽 모양이고, 회전 변환 캐시(`_arrRotationQuat`)는
     *          언리얼 `FRotationConversionCache` 를 따랐습니다 — 부모만 움직여 자식을 다시 합성할 때 삼각 함수 여섯 번을 건너뜁니다.
     *
     *          스레드 계약: 칸 받기 · 놓기는 뮤텍스 안입니다(씬 로드 워커와 게임 스레드가 동시에 만들 수 있습니다). 페이지 표는 고정 크기이고
     *          페이지 포인터는 release 로 발행하므로 `findPage` 는 락이 없습니다. 칸 **내용**의 동기화는 쓰는 쪽(씬의 틱 단계)이 맡습니다.
     */
    class SW_API SceneTransformStorage
    {
    public:
        /** @brief 칸이 없음을 나타냅니다. */
        static constexpr uint32 kInvalidSlot = 0xFFFFFFFFu;
        /** @brief 칸에 렌더 프리미티브가 없음을 나타냅니다(`SceneTransformPage::_arrPrimitiveIndex`). */
        static constexpr uint32 kNoPrimitive = 0xFFFFFFFFu;
        /**
         * @brief 페이지 표의 칸 수입니다. 페이지 16384 × 칸 256 = 씬 컴포넌트 약 400만 개까지입니다.
         * @details 오브젝트 표(`GameObjectManager` 의 id 표)가 약 100만 개에서 끝나므로 오브젝트마다 씬 컴포넌트 넷이 넘어야 닿습니다.
         */
        static constexpr uint32 kMaxPageCount = 16384;

        /** @brief 전역 저장소입니다. 처음 부를 때 만들어지고 Engine 이 내려갈 때 사라집니다. */
        static SceneTransformStorage& get();

        /**
         * @brief 빈 칸 하나를 받아 기본값(항등 로컬 · 항등 월드 · 소유자 알림 켬)으로 채웁니다. 어느 스레드에서 불러도 됩니다.
         * @param pOwner  칸을 쓸 컴포넌트
         * @param pOutPage 칸이 든 페이지. 컴포넌트가 들고 있다가 표 없이 찾습니다
         * @return 칸 번호
         */
        uint32 allocateSlot( SceneComponent* pOwner, SceneTransformPage*& pOutPage );
        /** @brief 칸을 놓습니다. 어느 스레드에서 불러도 됩니다. `kInvalidSlot` 이면 아무것도 하지 않습니다. */
        void freeSlot( uint32 slot );

        /** @brief 칸이 든 페이지입니다. 락이 없습니다. 받은 적 없는 번호면 nullptr 일 수 있습니다. */
        SceneTransformPage* findPage( uint32 slot ) const
        {
            const uint32 pageNumber = slot >> SceneTransformPage::kSlotShift;
            return pageNumber < kMaxPageCount ? _arrPage[pageNumber].load( std::memory_order_acquire ) : nullptr;
        }
        /** @brief 지금 쓰이는 칸 수입니다(테스트 · 진단용). */
        uint32 getLiveSlotCount() const;

        /**
         * @brief 칸의 로컬 값으로 월드 행렬 · LWC 를 합성합니다. 부모가 없으면 두 부모 포인터를 nullptr 로 줍니다.
         * @details 합성은 이 함수 한 곳입니다(컴포넌트의 `updateWorldTransformFromParent` · 틱 뒤 적용 · 플러시). 회전이 0 이면 회전
         *          행렬을 건너뛰고, 아니면 칸의 쿼터니언 캐시를 씁니다 — 오일러가 캐시를 만든 값과 다를 때만 삼각 함수로 다시 만듭니다.
         *          행렬 곱 순서는 DirectX 행-벡터 규격(Scale * Rotation * Translation, 그다음 부모)입니다.
         */
        static void composeWorld( SceneTransformPage& page, uint32 pageIndex, const float4x4* pParentWorld, const double3* pParentLwc );

        SceneTransformStorage( const SceneTransformStorage& )            = delete;
        SceneTransformStorage& operator=( const SceneTransformStorage& ) = delete;

    private:
        /** @brief 빈 저장소를 만듭니다. `get` 만 부릅니다. */
        SceneTransformStorage();
        /** @brief 페이지를 모두 놓습니다. */
        ~SceneTransformStorage();

        /** @brief 칸 하나를 기본값으로 채웁니다. */
        static void initializeSlot( SceneTransformPage& page, uint32 pageIndex, SceneComponent* pOwner );

        /** @brief 페이지 표입니다. 한 번 발행한 페이지는 저장소가 사라질 때까지 그 자리에 있습니다. */
        atomic<SceneTransformPage*> _arrPage[kMaxPageCount];
        /** @brief 놓인 칸입니다. 나중에 놓인 것부터 다시 씁니다. */
        vector<uint32> _listFreeSlot;
        /** @brief 한 번도 쓰지 않은 칸의 첫 번호입니다. */
        uint32 _nextSlot;
        /** @brief 지금 쓰이는 칸 수입니다. */
        uint32 _liveSlotCount;
        /** @brief 칸 받기 · 놓기 · 페이지 만들기를 보호합니다. */
        mutable mutex _mutex;
    };
} // namespace sw
