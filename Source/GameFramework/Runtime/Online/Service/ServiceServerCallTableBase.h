/**
 * @file ServiceServerCallTableBase.h
 * @brief 서버 호출 표(`OnlineServerCallTable`)의 템플릿이 아닌 몫입니다. 표준 지표, 서비스 이름, 호출 id, 로그를 맡습니다.
 * @details 모르는 호출 id 의 완료는 경고 하나(`ServiceServerCallTable.UnknownCall`)로 같고, 종료 때 남은 호출은 오류 하나(`ServiceServerCallTable.AbandonedCalls`)로 남깁니다.
 *          gRPC 비동기 서버의 호출 태그 표, 언리얼 HTTP 서버의 요청 핸들과 같은 자리입니다(기억에 의존).
 */
#pragma once
#include "Core/Container/IDAllocator.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Observability/ServiceMetrics.h"

namespace sw
{
    class MetricRegistry;

    /** @class ServiceServerCallTableBase @brief 표의 템플릿이 아닌 몫(지표, 서비스 이름, 호출 id, 로그)입니다. */
    class SW_GF_API ServiceServerCallTableBase
    {
    public:
        /** @brief 모르는 호출 id 의 완료를 경고로 남깁니다. 호출 표 앞에 자기 표를 둔 키트(GM)도 같은 경고를 씁니다. */
        void warnUnknownCall( uint64 callID ) const;

    protected:
        ServiceServerCallTableBase();

        uint64       allocateCallID() { return _callIDAllocator.allocate(); }
        void         reportAbandoned( size_t callCount ) const;
        static int64 nowNanoseconds();

    protected:
        ServiceMetrics      _metrics;
        string              _serviceName;
        IDAllocator<uint64> _callIDAllocator;
    };
} // namespace sw
