/**
 * @file ModuleAPI/Service/IModuleStateStore.h
 * @brief 모듈이 리로드 · 인스턴스 재생성 사이에 데이터를 맡겼다 되찾는 호스트 칸입니다(유니티 `SessionState` 자리).
 * @details 모듈 안의 정적 상태와 인스턴스는 라이브러리를 내리면 사라진다. 넘겨야 할 것(편집기의 되돌리기 기록 · 선택 · 복사 버퍼, 키트의 실행 상태)은
 *          **바이트로** 맡긴다 — 코드(델리게이트 · vtable)는 옛 이미지와 함께 사라지므로 맡길 수 없고, 형의 모양은 새 빌드에서 바뀔 수 있어 형식의 판을 함께 쓴다.
 *          호스트(App)가 구현해 호스트 범위에 이 인터페이스 형으로 건다. 모듈은 `findSubsystem<IModuleStateStore>()` 로 찾는다(없으면 넘기지 않는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 이름 붙은 바이트 묶음을 맡았다 돌려주는 칸입니다. 키는 모듈이 정한다(`"<모듈>.<무엇>"`). 메인 스레드에서만 씁니다. */
    class IModuleStateStore
    {
    public:
        /** @brief @p key 로 @p bytes 를 맡깁니다. 앞에 맡긴 것은 버립니다. */
        virtual void store( string_view key, vector<uint8>&& bytes ) = 0;
        /** @brief @p key 로 맡긴 것을 꺼내 @p outBytes 에 옮기고 칸을 비웁니다. 없으면 false 입니다. */
        [[nodiscard]] virtual bool take( string_view key, vector<uint8>& outBytes ) = 0;

    protected:
        ~IModuleStateStore() = default;
    };
} // namespace sw
