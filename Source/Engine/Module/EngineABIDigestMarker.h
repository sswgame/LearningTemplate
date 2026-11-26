/**
 * @file EngineABIDigestMarker.h
 * @brief 엔진 ABI 다이제스트 문자열의 머리(`swEngineABIDigest:`)입니다. 뒤에 생성기(`Scripts/generate/engine_abi_digest.py`)가 낸 SHA-1 16진 40 글자가 붙습니다.
 * @details 머리는 이 한 줄만이 정본이다 — Engine(`getEngineABIDigest`), 모듈 다이제스트 TU(`cmake/Modules/Build/EngineABIDigest.cmake`), 모듈 파일에서 다이제스트를 찾는
 *          쪽(`ModuleLibraryPatch::findEngineABIDigest`)이 모두 이것을 쓴다. 생성기는 지문(`SW_ENGINE_ABI_DIGEST`)만 낸다 — 예전에는 생성기가 머리까지
 *          적어, 약어 정리가 C++ 쪽 철자만 바꾸자 모든 모듈의 다이제스트를 찾지 못해 기동이 멈췄다. include 없는 잎 헤더다(문자열 붙이기에 쓰는 매크로).
 */
#pragma once

#define SW_ENGINE_ABI_DIGEST_MARKER "swEngineABIDigest:"
