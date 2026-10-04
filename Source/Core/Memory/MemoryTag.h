/**
 * @file MemoryTag.h
 * @brief 할당을 "무엇에 쓰는 메모리인가" 로 나누는 태그와, 태그를 거는 스코프입니다(UE LLM 의 태그와 같은 역할).
 * @details 태그는 스레드 로컬 값 하나입니다. `Memory::allocate` 가 그 값을 할당 헤더에 적고 `MemoryProfiler` 가 태그별로 셉니다.
 *          해제는 헤더에 적힌 태그로 빼므로 어느 스레드 · 어느 스코프에서 풀어도 같은 줄에서 빠집니다.
 *
 *          - 거는 자리는 하위 시스템의 **진입점**입니다(기동 단계 · 에셋 종류별 로드 · 씬 로드 · 렌더러 · 모듈 호출). 안쪽 함수마다 걸지 않습니다.
 *          - 태스크는 만든 쪽의 태그를 노드에 담아 실행하는 동안 그 태그를 씁니다(`TaskManager`). 워커에서 하는 로드도 같은 줄에 잡힙니다.
 *          - 엔진이 띄우는 스레드(파일 감시 · 모듈 빌드 · 파일 대화상자)는 띄운 쪽의 태그를 인자로 받아 스레드 첫 줄에서 겁니다. 새 스레드는 Unknown 에서 시작합니다.
 *          - 스코프는 배포본(`SW_SHIPPING`)이 아닌 모든 구성에서 일합니다(TLS 값 하나를 쓰고 되돌린다). 배포본에는 할당 헤더도 프로파일러도 없어 비용이 0 입니다.
 *          - 명시 태그: 스코프 밖에서 용도가 정해진 버퍼는 `Memory::allocate( size, tag )` 로 그 자리에서 태그를 줍니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @brief 할당의 용도입니다. 모듈이 아니라 "무엇을 담는가" 로 나눕니다.
     * @details 새 줄을 더하면 `MemoryProfiler::getMemoryTagName` 의 이름 표에도 한 줄을 더합니다(줄 수는 static_assert 가 봅니다).
     */
    enum class MemoryTag : uint8
    {
        Unknown = 0, ///< 태그를 걸지 않은 곳. 이 줄이 크면 진입점이 빠진 것이다
        EngineMisc,  ///< 엔진 서비스의 나머지(로거 · 명령줄 · 전역 변수 · 이벤트 · 설정 · 입력 · 현지화)
        Task,        ///< 태스크 매니저 · 워커 큐 · 노드 풀
        Reflection,  ///< 타입 등록부 · TypeInfo · 프로퍼티 표
        Asset,       ///< 에셋 데이터베이스 · 팩 색인 · 스트리밍 큐(에셋 본문은 종류별 줄로 간다)
        Scene,       ///< 씬 · GameObject · 컴포넌트 저장소 · 트랜스폼 계층 · 프리팹 인스턴스
        Texture,     ///< 텍스처 디코드 · 캐시
        Mesh,        ///< 메시 정점 · 인덱스 · 메시 생성
        Material,    ///< 머티리얼 · 머티리얼 인스턴스 · 캐시
        Shader,      ///< 셰이더 캐시 · 컴파일 결과 · 리플렉션 매니페스트 · 바인딩 레이아웃
        Animation,   ///< 애니메이션 클립 · 스켈레톤 · 그래프 · 스프라이트 클립
        Audio,       ///< 오디오 시스템 · 사운드 데이터
        Physics,     ///< 물리 월드 · 바디 · 충돌 구조
        RenderCpu,   ///< 렌더러의 CPU 측(FrameRenderer · GpuScene · 렌더 그래프 · PSO 캐시 · RHI 디바이스 · 렌더 스레드)
        UI,          ///< 런타임 UI(HUD · 위젯 · 대사 상자) — 에디터 UI 는 Editor
        Script,      ///< 데이터로 짠 실행 그래프(대사 그래프 · 시퀀스 · 이후의 스크립트 VM)
        Editor,      ///< 에디터 모듈
        Game,        ///< 게임 모듈
        MaxTags
    };

    /** @brief 태그 줄 수입니다(`MaxTags` 를 뺀 값의 개수). */
    inline constexpr uint32 kMemoryTagCount = static_cast<uint32>( MemoryTag::MaxTags );

    /** @brief 태그 스코프가 일하는 구성인지입니다. 아니면 `ScopedMemoryTag` 와 `SW_MEMORY_SCOPE` 는 아무 일도 하지 않습니다. */
#if !defined( SW_SHIPPING )
    inline constexpr bool kMemoryTagScopesEnabled = true;
#else
    inline constexpr bool kMemoryTagScopesEnabled = false;
#endif
} // namespace sw
