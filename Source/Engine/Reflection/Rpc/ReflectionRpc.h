/**
 * @file ReflectionRpc.h
 * @brief FUNCTION 호출을 Binary 봉투로 로컬 pack/unpack (네트워크 전송은 별도)
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Utility/Task/TaskTypes.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) RpcEnvelope — 소켓 없음, pack/invoke만
	// ------------------------------------------------------------------------------
	struct SW_API RpcEnvelope
	{
		string				   _typeFqn; ///< 리플렉션 타입 FQN
		string				   _methodName;
		vector<uint8>		   _listArgBytes; ///< count + 인자별 (typeNameHash + size-prefixed binary)
		uint32				   _typeFqnHash{ 0 };
		uint32				   _methodHash{ 0 };
		uint8				   _netRole	  : 3; ///< FunctionNetRole
		uint8				   _bReliable : 1;
		[[maybe_unused]] uint8 _reserved  : 4;

		/** @brief 빈 봉투 (Reliable 꺼짐). */
		RpcEnvelope() noexcept
			: _netRole{ 0 }
			, _bReliable{ 0 }
			, _reserved{ 0 } {}
	};

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
		/** @brief typeFqn 메서드 인자를 봉투에 팩합니다 (파라미터 타입은 FunctionInfo). */
		static bool packCall( RpcEnvelope& out, const hashed_string& typeFqn, const hashed_string& methodName,
							  const TaskArgs& args );

		/** @brief 인자를 푼 뒤 TypeRegistry::invokeMethod로 로컬 호출합니다. */
		static TaskValue unpackAndInvoke( void* pInstance, const RpcEnvelope& envelope );

		/** @brief 테스트용 왕복: pack 후 즉시 invoke. */
		static bool packAndInvoke( void* pInstance, const hashed_string& typeFqn, const hashed_string& methodName,
								   const TaskArgs& args, TaskValue* pOutResult = nullptr );
	};
} // namespace sw
