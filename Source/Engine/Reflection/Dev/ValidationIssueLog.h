/**
 * @file ValidationIssueLog.h
 * @brief 검증 결과를 출처(오브젝트)마다 모아 두는 표입니다. 개발 전용 소스라 배포 구성은 짓지 않는다(결과를 읽는 쪽이 에디터뿐이다).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    /**
     * @brief 검증 결과를 출처(오브젝트)마다 모아 두는 표입니다. 같은 출처를 다시 검증하면 그 출처의 결과를 바꿉니다.
     * @details 값 형식이다 — 월드(씬)가 하나씩 든다(`SceneValidationIssues`, `Engine/Object/GameObject/Serialization/Dev/SceneValidationIssues.h`). 월드가 둘이면
     *          (에디터 미리보기 창 · 씬 둘) 결과가 한 표에 섞이지 않는다(언리얼 Map Check 의 월드별 메시지 로그와 같다). 맵 검사 패널은 활성 씬의 표를 읽는다.
     *          출처의 결과는 출처와 함께 사라집니다 — `GameObject` 소멸자가 자기 매니저의 표에서 자기 ID 를 지운다. 여러 스레드(로드 워커)에서 쓸 수 있습니다.
     */
    class SW_API ValidationIssueLog
    {
    public:
        ValidationIssueLog()                                       = default;
        ValidationIssueLog( const ValidationIssueLog& )            = delete;
        ValidationIssueLog& operator=( const ValidationIssueLog& ) = delete;

        /** @brief @p sourceID 의 결과를 @p listIssue 로 바꿉니다(비었으면 그 출처를 지운다). */
        void replaceIssues( uint64 sourceID, const vector<ValidationIssue>& listIssue );
        /** @brief 출처 하나의 결과를 지웁니다(오브젝트가 사라졌다). 표가 비었으면 잠금 하나로 끝납니다. */
        void removeSource( uint64 sourceID );
        /** @brief 모든 결과를 출처 순서 없이 모읍니다. */
        void collectIssues( vector<ValidationIssue>& outListIssue ) const;
        /** @brief 모든 결과의 수입니다. */
        uint32 getIssueCount() const;
        /** @brief 모두 지웁니다. */
        void clear();
        /**
         * @brief 결과가 바뀔 때마다 하나씩 오르는 번호입니다. 맵 검사 패널 · 상태줄은 번호가 같으면 목록을 다시 모으지 않는다.
         * @details 같은 결과로 바꾸거나 없는 출처를 지우면 오르지 않는다(인스펙터 편집이 프레임마다 다시 검증한다).
         */
        uint32 getRevision() const { return _revision.load( std::memory_order_acquire ); }

    private:
        mutable mutex                      _mutex;
        vector<ValidationIssue> _listIssue SW_GUARDED_BY( _mutex );
        atomic<uint32>                     _revision{ 0 };
    };
} // namespace sw
