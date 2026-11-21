/**
 * @file SceneTransformPage.h
 * @brief 씬 컴포넌트 트랜스폼 값 페이지(`SceneTransformPage`)입니다. 컴포넌트가 자기 페이지 포인터로 값을 바로 읽는 인라인 접근자가 이 정의만 봅니다.
 * @details 저장소(`SceneTransformStorage.h` — 서브시스템 · 슬롯 표 · 데이터 저장소)와 나눠, `SceneComponent.h` 를 포함하는 TU 가 저장소 사슬을 읽지 않게 한다
 *          (헤더 팬인 다이어트).
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class SceneComponent;

    namespace SceneTransformPageInternal
    {
        inline constexpr size_t kCacheLineAlignment = 64; ///< 페이지는 캐시 라인에 맞춘다 — 값 배열을 라인 단위로 읽는다
    } // namespace SceneTransformPageInternal

    /**
     * @struct SceneTransformPage
     * @brief 트랜스폼 슬롯 256 개를 담은 페이지입니다. 값마다 배열 하나라, 같은 값을 연달아 도는 패스(틱 뒤 적용, 플러시, 렌더 수집)가 라인 단위로 읽습니다.
     * @details 슬롯 하나의 값은 모든 배열의 같은 위치(`슬롯 번호 & kSlotMask`)에 있다. 페이지는 한 번 만들면 옮기지도 해제하지도 않으므로(저장소가
     *          서비스 범위와 함께 소멸할 때까지), 컴포넌트는 자기 페이지 포인터를 가지고 테이블을 거치지 않고 찾는다.
     *
     *          값을 `SceneComponent` 안에 두지 않는 이유는 틱 뒤 적용과 렌더 수집이 8000 개를 돌 때 오브젝트마다 캐시 라인 서너 개를 건너다니고,
     *          행렬 하나를 읽으려고 컴포넌트 전체를 끌어오게 되기 때문이다.
     *
     *          **월드 변경 번호(`_arrWorldVersion`).** 트랜스폼을 따르는 소비자(물리 바디 · 내비 장애물)는 지난번에 본 번호와 정수 하나로 비교해,
     *          같으면 행렬을 읽거나 풀지 않는다(매 프레임 폴링 대신). 알림 리스너로 하지 않은 이유: 더티 트랜스폼마다 간접 호출이 붙고 큐브
     *          20,000 개 벤치의 `GT.Frame` 이 그 경로를 지난다(EnginePartition 6.3). 칸을 다시 받아도 번호를 0 으로 되돌리지 않는 것은, 옛 번호를
     *          든 소비자가 같은 칸의 새 주인을 "안 바뀜"으로 보지 않게 하려는 것이다.
     */
    struct alignas( SceneTransformPageInternal::kCacheLineAlignment ) SceneTransformPage
    {
        using Owner = SceneComponent; ///< `ComponentDataStore` 페이지 계약

        static constexpr uint32 kSlotShift = 8;
        static constexpr uint32 kSlotCount = 1u << kSlotShift;
        static constexpr uint32 kSlotMask  = kSlotCount - 1u;
        /** @brief 칸에 렌더 프리미티브가 없음을 나타냅니다(`_arrPrimitiveIndex`). */
        static constexpr uint32 kNoPrimitive = constant::invalid_index::kUint32;

        /** @brief `_arrFlag` 비트입니다. 게임 스레드가 구조를 바꿀 때(붙이기, 떼기, 등록)만 씁니다. */
        enum SlotFlag : uint8
        {
            kHasParent   = 1u << 0, ///< 부모에 붙어 있다
            kHasChildren = 1u << 1, ///< 자식이 하나 이상 있다
            kNotifyOwner = 1u << 2, ///< 월드가 바뀌면 소유 컴포넌트의 `onWorldTransformUpdated` 를 부른다
        };
        /**
         * @brief 로컬 값 하나를 가리키는 비트입니다. 세터, 틱 대기 표시(`_arrPendingMask`), 쓰기 건(`SceneTransformWrite`)이 같은 비트를 씁니다.
         * @details 세 값을 다루는 곳(세터 세 개, 틱 대기, 쓰기 큐 합치기, 배치 적용, 대기 적용)은 세 분기로 분기하지 않고 이 비트로 고른다.
         */
        enum LocalValueBit : uint8
        {
            kLocalPosition = 1u << 0,
            kLocalRotation = 1u << 1,
            kLocalScale    = 1u << 2,
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
        SceneComponent* _arrOwner[kSlotCount];            ///< 슬롯을 쓰는 컴포넌트다. 빈 슬롯이면 nullptr 이다
        uint32          _arrPrimitiveIndex[kSlotCount];   ///< 렌더 프리미티브 번호(`PrimitiveRegistry`). 없으면 `kNoPrimitive`
        uint32          _arrWorldVersion[kSlotCount];     ///< 월드 변경 번호. 월드를 쓰는 `composeWorld` · `initializeSlot` 만 올리고 0 으로 되돌리지 않는다(아래 설명)
        uint8           _arrPendingMask[kSlotCount];      ///< 적용을 기다리는 틱 대기 값(`LocalValueBit` 조합). 슬롯의 주인 오브젝트를 틱하는 스레드만 쓴다
        uint8           _arrFlag[kSlotCount];             ///< `SlotFlag` 조합

        /** @brief 받은 칸을 기본값(항등 로컬, 항등 월드, 소유자 알림 켬)으로 채웁니다(`ComponentDataStore` 페이지 계약). */
        void initializeSlot( uint32 pageIndex, SceneComponent* pOwner );
        /** @brief 놓은 칸이 누구의 것으로도 읽히지 않게 비웁니다. 틱 대기 표시도 내린다 — 파괴는 틱 밖이라 대기 목록에 남은 번호가 없다. */
        void releaseSlot( uint32 pageIndex );

        /** @brief 비트 하나가 가리키는 로컬 값의 위치입니다. */
        float3& getLocalValueRef( uint32 pageIndex, uint8 bit )
        {
            return ( bit == kLocalPosition ) ? _arrLocalPosition[pageIndex] : ( bit == kLocalRotation ) ? _arrLocalRotation[pageIndex]
                                                                                                        : _arrLocalScale[pageIndex];
        }
        /** @brief 비트 하나가 가리키는 틱 대기 값의 위치입니다. */
        float3& getPendingValueRef( uint32 pageIndex, uint8 bit )
        {
            return ( bit == kLocalPosition ) ? _arrPendingPosition[pageIndex] : ( bit == kLocalRotation ) ? _arrPendingRotation[pageIndex]
                                                                                                          : _arrPendingScale[pageIndex];
        }
        /**
         * @brief 로컬 값 하나를 씁니다. 현재 값과 거의 같으면 쓰지 않고 false 를 반환합니다.
         * @details **제곱 거리에는 제곱한 허용치를 쓴다.** `kEpsilon` 을 그대로 대면 실제 거리 1e-3 까지가 "안 움직였다"가 되는데, 비교
         *          기준이 매번 **현재 값**이라 그 아래 움직임은 쌓이지도 않는다. 한 프레임에 1e-3 보다 조금씩 가는 물체는 영원히 제자리에
         *          있게 된다. 세터, 틱 뒤 적용, 배치 쓰기가 모두 이 규칙 하나를 쓴다.
         */
        [[nodiscard]] bool writeLocalValue( uint32 pageIndex, uint8 bit, const float3& value )
        {
            float3& current = getLocalValueRef( pageIndex, bit );
            if ( float3::getDistanceSquared( current, value ) <= MathUtil::kEpsilonSquared )
                return false;
            current = value;
            return true;
        }
    };
} // namespace sw
