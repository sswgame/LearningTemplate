/**
 * @file TypeRegistrar.h
 * @brief 생성 코드가 정적 초기화로 타입 · 열거형 서술자를 사슬에 다는 등록자 둘입니다.
 * @details 생성 단위(`.gen.cpp`)는 서술자 표를 사슬에 달기만 한다 — 레지스트리(`TypeRegistry.h`)도 형식 정보(`TypeInfo.h`)도 읽지 않는다. 그 헤더를 고쳐도 생성 단위가
 *          다시 컴파일되지 않는다(언리얼 생성 코드가 `FRegisterCompiledInInfo` 만 보는 자리).
 */
#pragma once
#include "Core/Registry/RegistrationChain.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    struct EnumInfoDesc;
    struct TypeDesc;
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) TypeRegistrar: 정적 초기화로 Type 서술자를 사슬(`getChain`)에 단다. 올리는 쪽(`engine::registerModuleTypes`)이 이미지마다 걷어 간다.
    // ------------------------------------------------------------------------------
    struct SW_API TypeRegistrar
    {
        const TypeDesc& ( *_pGetDesc )(); ///< 타입 서술자(상수 표)를 돌려주는 생성 함수
        TypeRegistrar* _pNext;            ///< 같은 사슬의 다음(먼저 만든) registrar

        /** @brief 아직 걷히지 않은 registrar 사슬입니다(Engine 이미지가 가진다). */
        static RegistrationChain<TypeRegistrar>& getChain();

        explicit TypeRegistrar( const TypeDesc& ( *pGetDesc )() );
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) EnumRegistrar: 정적 초기화로 Enum 서술자를 사슬(`getChain`)에 단다(TypeRegistrar 와 같다).
    // ------------------------------------------------------------------------------
    struct SW_API EnumRegistrar
    {
        const EnumInfoDesc& ( *_pGetDesc )(); ///< 열거형 서술자(상수 표)를 돌려주는 생성 함수
        EnumRegistrar* _pNext;                ///< 같은 사슬의 다음(먼저 만든) registrar

        /** @brief 아직 걷히지 않은 enum registrar 사슬입니다(Engine 이미지가 가진다). */
        static RegistrationChain<EnumRegistrar>& getChain();

        explicit EnumRegistrar( const EnumInfoDesc& ( *pGetDesc )() );
    };
} // namespace sw
