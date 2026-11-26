// 사례 NamespaceIncludedInput.h 의 짝 — 이름공간 없이 적고 Batch/NamespaceWrapper.h 가 이름공간 안에서 include 한다. 사례로 돌지 않는다.
#pragma once

REFLECT()
struct NamespaceWrappedRecord
{
    REFLECT_BODY();
    PROPERTY()
    int32 _count{ 0 };
};
