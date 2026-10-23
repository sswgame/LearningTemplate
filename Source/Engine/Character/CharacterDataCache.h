/**
 * @file CharacterDataCache.h
 * @brief 캐릭터 데이터 에셋을 경로별로 나눠 주는 캐시들입니다. 소켓 에셋(`*.sockets.xml`), 알림 테이블(`*.notifies.xml`), 페어드 애니메이션(`*.pairedanim.xml`),
 *        물리 에셋(`*.physics.xml`), 피팅 표(`*.fit.xml`), 부품 피팅 데이터(`*.partfit.xml`)를 다룹니다.
 * @details 구현은 `SharedAssetCache` 하나이고(약한 참조, 같은 경로는 같은 객체), 이 파일은 종류마다 인스턴스화 선언만 둡니다(쓰는 쪽은 `SharedAssetCache<SocketSet>` 처럼
 *          템플릿 이름으로 부릅니다). 에셋 매니저의 캐시
 *          레지스트리에 등록되어 있어 파일을 고치면 다시 읽는다(`reload`). 다시 읽은 횟수(`getReloadCount`)가 오르면 쓰는 쪽(소켓 컴포넌트, 알림 디스패치,
 *          래그돌, 페어드 애니메이션)이 자기가 만든 것을 다시 만든다.
 */
#pragma once
#include "Core/Common/Macros.h"

#include "Engine/Resource/Cache/SharedAssetCache.h"

namespace sw
{
    struct PhysicsAsset;

    class AnimationNotifyTable;
    class BodyShapeSet;
    class FitPartData;
    class FitTables;
    class PairedAnimationDef;
    class SocketSet;

    /** @brief 소켓 에셋을 엔진 기본 소켓 종류 테이블로 읽어 나눠 줍니다("SocketSet"). 모르는 필드나 종류는 오류를 남기고 nullptr 입니다. 제자리에서 다시 읽습니다. */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<SocketSet>;
    /** @brief 알림 테이블을 기본 처리기 레지스트리로 읽어 나눠 줍니다("AnimationNotifyTable"). 모르는 처리기나 인자는 오류입니다. 제자리에서 다시 읽습니다. */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<AnimationNotifyTable>;
    /**
     * @brief 페어드 애니메이션 에셋을 나눠 줍니다("PairedAnimationDef").
     * @details 다시 읽기는 제자리에서 바꾸지 않고 표에서 떼어 낸다(`SharedAssetReloadMode::Detach`) — 돌고 있는 세션은 역할 인덱스로 옛 에셋을 읽으므로 끝까지
     *          옛 객체를 쥔다. 쓰는 쪽(`PairedAnimationComponent`)은 다시 읽은 횟수가 바뀌면 다음 시작 전에 다시 `acquire` 한다.
     */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<PairedAnimationDef>;
    /** @brief 물리 에셋을 나눠 줍니다("PhysicsAsset"). 제자리에서 다시 읽고, 래그돌은 다시 읽은 횟수가 바뀌면 다시 짓습니다. */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<PhysicsAsset>;
    /**
     * @brief 피팅 표(`*.fit.xml`)를 엔진 기본 연산 레지스트리로 읽어 나눠 줍니다("FitTables"). 제자리에서 다시 읽고, 외형 컴포넌트는 다시 읽은 횟수가 바뀌면 다시 조립합니다.
     * @details 사용자 연산을 등록한 풀이기의 표는 이 캐시가 아니라 그 풀이기의 레지스트리로 직접 읽는다(캐시는 경로만으로 나눠 주므로 레지스트리를 고를 수 없다).
     */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<FitTables>;
    /** @brief 부품 피팅 데이터(`*.partfit.xml`)를 나눠 줍니다("FitPartData"). 표와 따로 읽고 이름은 조립이 대조합니다(`FitPartData::validateNames`). */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<FitPartData>;
    /** @brief 체형 축 데이터(`*.bodyshape.xml`)를 나눠 줍니다("BodyShapeSet"). 제자리에서 다시 읽고, 외형 컴포넌트는 다시 읽은 횟수가 바뀌면 다시 조립합니다. */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<BodyShapeSet>;
} // namespace sw
