# Scripts

호스트/환경/파일 조작용 Python 진입점입니다. CMake는 타겟·플래그·RPATH만 담당하고, 여기서 **결과**(경로, 성공/실패)만 소비합니다.

| 디렉터리 | 역할 |
|----------|------|
| `setup/` | 환경 오케스트레이터, Ninja, Linux 홈 설정, parser 시드, `find/*` 탐색기 |
| `vcpkg/` | vcpkg 루트 탐색, Linux Vulkan loader 심볼릭 링크 |
| `generate/` | changelog / Doxygen |
| `ConfigHelper.py` | 프로젝트 루트, `engine_config` / `search_paths` I/O |

## 설정

| 파일 | 용도 |
|------|------|
| `Config/engine_config.json` | 캐시된 절대경로 (로컬, gitignore) |
| `Config/search_paths.json` | 탐색 후보·다운로드 URL·vcpkg 상대 기본값 |
| `Config/search_paths.defaults.json` | search_paths 시드 (커밋) |

## 수동 실행 예

```bash
python3 Scripts/setup/SetupEnvironment.py
python3 Scripts/vcpkg/FindVcpkg.py
python3 Scripts/vcpkg/FindVcpkg.py --install   # opt-in bootstrap
python3 Scripts/vcpkg/FixVcpkgVulkanLoader.py --vcpkg-installed-dir build/vcpkg_installed --triplet x64-linux
```
