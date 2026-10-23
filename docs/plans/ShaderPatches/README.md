# 셰이더 패치 (통합이 적용한다)

병렬 작업자는 빌드된 `App` 이 없어 셰이더 소스를 커밋하지 못합니다. 커밋 훅(`Scripts/lint/PreCommitLint.py`)이 셰이더를 고친 커밋마다 `App --cook-shaders` 로 전 백엔드를 굽고
`App --verify-shaders` 로 바이너리가 소스와 맞는지 보기 때문입니다(App 이 없으면 건너뛰지 않고 실패합니다). 그래서 셰이더 소스의 변경을 이 폴더의 **패치 파일**로 남깁니다.
패치는 훅이 보는 `Resource/<도메인>/shaders/` 밖에 있어 훅에 걸리지 않습니다. 훅을 우회한 것이 아닙니다.

## 적용 순서

번호 순서입니다. 같은 파일을 고친 패치는 앞 패치 위에 쌓이도록 만들었습니다(작업자 워크트리에서 단계마다 따로 뽑았습니다).

```
git apply --ignore-whitespace docs/plans/ShaderPatches/01-VfxGpuLights-L3.patch
...
```

적용한 뒤 빌드(`cmake --build --preset Ninja-Debug`)하고 `App --cook-shaders` 로 네 백엔드를 구워 소스와 바이너리를 함께 커밋합니다. 적용이 끝나면 이 폴더의 패치 파일을 지웁니다.
저장소는 CRLF 이고 패치는 LF 입니다(`*.patch` 는 줄 끝 변환을 하지 않습니다). `git apply` 가 줄 끝 때문에 실패하면 `--ignore-whitespace` 를 줍니다.
새 파일을 만드는 패치도 같은 방법으로 적용됩니다.

## 목록

패치마다 어느 계획 문서의 어느 단위인지와 함께 적용해야 하는 C++ 변경이 있는지를 그 계획 문서가 적습니다.
