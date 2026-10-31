/**
 * @file ScopedTestModule.h
 * @brief 리플렉션 단위 테스트가 손으로 레지스트리에 올린 타입 · 열거형을 케이스 끝에 내리는 범위 객체입니다.
 * @details 케이스는 섞인 순서로 돌고 `SW_REQUIRE_*` 가 중간에 끝낼 수 있다 — 끝의 `unregisterTypesByModule` 한 줄은 그때 건너뛰어 다음 케이스에 타입이 남는다.
 *          그래서 손 등록은 시험 모듈 이름(`_moduleName`)을 달고 이 객체로 감싼다.
 */
#pragma once
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw::test
{
    /** @brief 소멸할 때 @p pModuleName 모듈의 타입 · 열거형을 레지스트리에서 내립니다. */
    struct ScopedTestModule
    {
        explicit ScopedTestModule( const utf8* pModuleName )
            : _pModuleName( pModuleName )
        {
        }
        ~ScopedTestModule() { engine::getSubsystem<TypeRegistry>().unregisterTypesByModule( _pModuleName ); }

        ScopedTestModule( const ScopedTestModule& )            = delete;
        ScopedTestModule& operator=( const ScopedTestModule& ) = delete;

        const utf8* _pModuleName;
    };
} // namespace sw::test
