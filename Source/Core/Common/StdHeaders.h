/**
 * @file StdHeaders.h
 * @brief Core 가 쓰는 C/C++ 표준 라이브러리 헤더 묶음입니다.
 * @details 서드파티 헤더(imgui, vulkan, glad 등)는 여기에 넣지 말고 쓰는 곳에서 include 합니다.
 *
 * @warning 모든 TU 가 읽는 헤더입니다(CoreMinimal · pch.h). Core 헤더가 쓰는 것만 둡니다.
 *          스트림(<fstream> · <sstream> · <iostream> · <ostream>) · <random> · <regex> · <future> · <variant> · <condition_variable> ·
 *          <csignal> · <queue> · <stack> · <bitset> 처럼 몇 파일만 쓰는 무거운 헤더는 쓰는 파일이 직접 include 합니다
 *          (PCH 를 끈 콜드 빌드에서 TU 마다 파싱 시간이 붙는다 — docs/plans/BuildSpeed.md 2 단계).
 *          스트림 연산자 선언은 <iosfwd> 의 전방 선언으로 충분합니다.
 */
#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cinttypes>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <deque>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iosfwd>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
