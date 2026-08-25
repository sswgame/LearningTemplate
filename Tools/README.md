# Tools (호스트 빌드 도구)

본격적인 엔진 컴파일 전에 미리 실행되어 빌드를 돕거나, 외부 패키지 매니저(vcpkg) 등이 보관되는 장소입니다.

## 주요 구성요소
- **ReflectionParser**: libclang으로 `REFLECT` / `PROPERTY` 등을 스캔해 `*.gen.cpp`를 생성합니다. 초심자용 흐름·CLI·템플릿 설명은 [ReflectionParser/README.md](ReflectionParser/README.md). 엔진 본체를 빌드하려면 이 도구가 먼저 준비되어야 합니다.
- **_dependencyModuleList / vcpkg**: 서드파티 라이브러리나 툴 체인이 다운로드 되는 캐시/임시 폴더로 활용됩니다.
