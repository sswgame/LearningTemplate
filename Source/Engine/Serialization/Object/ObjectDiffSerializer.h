/**
 * @file ObjectDiffSerializer.h
 * @brief CDO(클래스 기본 객체)와 비교한 객체 델타(diff) 직렬화 · 역직렬화입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    struct TypeInfo;

    /**
     * @class ObjectDiffSerializer
     * @brief CDO 기본값과 다른 프로퍼티만 델타로 씁니다
     */
    class SW_API ObjectDiffSerializer
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) Diff: CDO 대비 변경분만. 레이아웃은 nameHash + size + payload
        // ------------------------------------------------------------------------------
        /** @brief CDO 와 변경된 객체를 비교해 델타 바이너리를 뽑습니다. */
        static bool serializeDiff( vector<uint8>& outDiffBytes, const void* pCdoInstance, const void* pModifiedInstance,
                                   const TypeInfo& typeInfo );

        /**
         * @brief serializeDiff 가 만든 델타 바이너리를 적용합니다.
         * @details 레이아웃: nameHash + size + payload 입니다(typeHash 없음. 현재 PropertyInfo 로 타입을 해석합니다).
         *          BinarySerializer 의 프로퍼티 레코드(nameHash + typeHash + size + payload)와 다릅니다.
         *          모르는 프로퍼티 해시가 나오면 적용에 실패합니다(false).
         */
        static bool deserializeDiff( void* pTargetInstance, const TypeInfo& typeInfo, const uint8* pDiffData, size_t diffSize );
    };

} // namespace sw
