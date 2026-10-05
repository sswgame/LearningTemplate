# ==============================================================================
# @file cmake/Modules/Toolchain/VcpkgTsan/x64-linux-tsan.cmake
# @brief ThreadSanitizer 구성(CI-Debug-TSAN) 전용 vcpkg overlay triplet — x64-linux 에 더해 스스로 동기화하는 포트를 TSan 으로 계측한다
#
# TSan 은 계측된 코드의 원자 연산 · futex 만 동기화로 본다(pthread 는 계측 없이도 가로챈다). 원자 연산으로 스스로 동기화하는 라이브러리를
# 계측 없이 링크하면 그 동기화는 안 보이고, 헤더 인라인 함수는 링커가 우리 TU 의 계측된 사본을 골라 라이브러리 안에서도 그것이 불리므로
# 접근만 보여 거짓 경쟁이 난다. 억제로 거르면 그 라이브러리의 잡 안에서 불리는 엔진 콜백(접촉 리스너 등)의 진짜 경쟁까지 가려진다.
#
# 계측 대상(아래 PORT 목록):
#   joltphysics — 잡 의존 · 장벽(JobSystemWithBarrier) · 잡 풀 · 임시 할당자를 원자 연산으로 잇는다. 접촉 콜백이 그 잡 스레드에서 불린다.
#   box2d       — 엔진은 워커 없이 한 스레드로 돌리지만(Box2DPhysicsBackend.h), 엔진 스레드끼리 월드를 동시에 만지는 경쟁은 라이브러리 안
#                 접근이라 계측해야 보인다.
# 나머지 포트는 계측하지 않는다: 스스로 스레드를 만들지 않고(동기화가 없으면 거짓 경쟁도 없다), 셰이더 컴파일러 · 압축처럼 시험이 오래
# 머무는 코드를 계측하면 TSan 의 5~15 배 감속이 시험 시간 한도에 닿는다. 그런 포트 **안**의 접근은 보이지 않는다(거짓 음성).
# 원자 연산으로 스스로 동기화하는 포트를 새로 들이면 이 목록에 더한다.
#
# 주의:
# - 이 파일은 `Toolchain/Vcpkg/` 밖에 둔다 — 그 폴더는 모든 CI 잡의 vcpkg 캐시 키(ci.yml `hashFiles`)에 들어, 여기를 고칠 때마다 다른 잡의
#   캐시까지 새로 저장된다. TSan 잡의 키만 이 폴더를 본다.
# - vcpkg 는 트리플릿 파일 내용을 ABI 해시에 넣는다(include 한 파일은 추적하지 않는다). 그래서 x64-linux.cmake 를 include 하지 않고 같은 설정을
#   여기 그대로 둔다 — 둘 중 하나를 바꾸면 다른 쪽도 맞춘다. 이 파일을 한 글자라도 바꾸면 모든 포트를 다시 짓는다(WSL 약 35 분).
# - 포트 컴파일러는 프리셋 환경의 CC/CXX(clang)가 정한다. 비어 있으면 vcpkg 가 시스템 GCC 로 짓는다(계측 런타임이 섞이고 DirectXTex 가 OpenMP 를 끌고 온다).
# ==============================================================================

set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# OpenGLRHI는 core 4.6 컨텍스트를 쓰므로 glad도 core로 생성한다.
set(GLAD_PROFILE core)

if(PORT MATCHES "^(joltphysics|box2d)$")
	set(VCPKG_C_FLAGS "-fsanitize=thread -fno-omit-frame-pointer")
	set(VCPKG_CXX_FLAGS "-fsanitize=thread -fno-omit-frame-pointer")
	set(VCPKG_LINKER_FLAGS "-fsanitize=thread")
endif()
