/**
 * @file ProcessSession.h
 * @brief 이 실행을 다른 실행과 가르는 세션 ID 입니다. 로그 파일 이름 · 크래시 파일 이름 · 텔레메트리가 같은 값을 써서 서로 짝지어집니다.
 * @details 프로세스 ID 처럼 실행마다 하나이고 한 번 정해지면 바뀌지 않는 값이라 프로세스 값입니다(크래시 처리기의 상태가 아니다 — 로그가 크래시 모듈에 기대지 않게 여기에 둔다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @struct ProcessSession @brief 이 실행의 세션 ID 입니다. */
    struct SW_API ProcessSession
    {
        /**
         * @brief 16 자리 16진수 세션 ID 입니다. 처음 부를 때 시각과 난수를 섞어 정하고, 그 뒤로는 같은 값입니다.
         * @details 처음 부르는 스레드가 여럿일 수 있다(로그 워커가 파일을 여는 순간과 크래시 보고 스레드가 덤프 경로를 만드는 순간). 함수 지역 정적 초기화(스레드 안전)로
         *          한 값만 보이게 한다. 크래시 처리기는 설치할 때 미리 불러 크래시 경로에서 만들지 않는다. 할당하지 않는다.
         */
        static const utf8* getID();
    };
} // namespace sw
