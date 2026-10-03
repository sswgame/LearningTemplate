/**
 * @file ReflectionRpc.h
 * @brief FUNCTION 호출을 Binary 봉투에 담아 로컬에서 pack/unpack 합니다(네트워크 전송은 별도).
 *
 * @note 구현은 `Engine/Serialization/Core/SerializeReflectionRpc.cpp` 에 있습니다. 인자 마샬링은
 *       BinarySerializer 의 규약이고, Reflection 이 Serialization 을 참조하면 둘이 서로를
 *       참조하는 순환이 됩니다.
 */
#pragma once
#include "Core/Task/TaskTypes.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) RpcEnvelope — 소켓 없음, pack/invoke만
    // ------------------------------------------------------------------------------
    struct SW_API RpcEnvelope
    {
        string                 _typeFqn; ///< 리플렉션 타입 FQN
        string                 _methodName;
        vector<uint8>          _argumentBytes; ///< count + 인자별 (typeNameHash + size-prefixed binary)
        uint32                 _typeFqnHash{ 0 };
        uint32                 _methodHash{ 0 };
        uint8                  _netRole   : 3; ///< FunctionNetRole
        uint8                  _bReliable : 1;
        [[maybe_unused]] uint8 _reserved  : 4;

        /** @brief 빈 봉투를 만듭니다(Reliable 꺼짐). */
        RpcEnvelope() noexcept
            : _netRole{ 0 }
            , _bReliable{ SW_FALSE }
            , _reserved{ 0 } {}
    };
} // namespace sw

namespace sw
{
    /**
     * @class ReflectionRpc
     * @brief 리플렉션 메서드 호출을 봉투로 싸고 로컬에서 풉니다
     */
    class SW_API ReflectionRpc
    {
    public:
        // ------------------------------------------------------------------------------
        // 2) pack · invoke — FunctionInfo 파라미터 타입, TypeRegistry::invokeMethod
        // ------------------------------------------------------------------------------
        /** @brief typeFqn 메서드의 인자를 봉투에 담습니다(매개변수 타입은 FunctionInfo 에서 얻습니다). */
        static bool packCall( RpcEnvelope& out, const hashed_string& typeFqn, const hashed_string& methodName,
                              const TaskArgs& args );

        /**
         * @brief 인자를 푼 뒤 TypeRegistry::invokeMethod 로 로컬 호출합니다.
         * @param instanceType `pInstance` 의 실제 타입입니다. 봉투의 타입이 이것이거나 이것의 부모여야 부릅니다.
         * @details 봉투를 믿지 않습니다 — 인스턴스가 봉투가 적은 타입인지 보고(다른 타입을 적은 봉투 = 타입 혼동), RPC 로 표시되지 않은
         *          메서드(`FUNCTION()` 의 NetRole 이 Local)는 부르지 않습니다.
         */
        static TaskValue unpackAndInvoke( void* pInstance, const TypeInfo& instanceType, const RpcEnvelope& envelope );
    };
} // namespace sw
