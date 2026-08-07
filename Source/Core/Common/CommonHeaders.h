#pragma once

/**
 * @file CommonHeaders.h
 * @brief 엔진 전체에서 널리 사용되는 C/C++ 표준 라이브러리 헤더들을 모아둔 파일입니다.
 * @details 이 헤더는 Precompiled Header(PCH)에 주로 포함되어 빌드 타임을 줄이는 용도로 사용됩니다.
 */

// ============================================================================
// [C 표준 라이브러리]
// ============================================================================
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cctype>
#include <cwctype>
#include <climits>
#include <cmath>
#include <limits>

// ============================================================================
// [C++ STL 컨테이너 및 문자열]
// ============================================================================
#include <array>
#include <bitset>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <variant>
#include <any>
#include <memory>
#include <queue>
#include <stack>

// ============================================================================
// [입출력 및 파일 시스템]
// ============================================================================
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

// ============================================================================
// [C++ STL 유틸리티 모음]
// ============================================================================
#include <optional>
#include <algorithm>
#include <chrono>
#include <regex>
#include <type_traits>
#include <functional>
#include <charconv>
#include <initializer_list>
#include <system_error>
#include <utility>
#include <tuple>
#include <random>

// ============================================================================
// [멀티스레딩 지원 (C++11 이상)]
// ============================================================================
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <shared_mutex>
#include <thread>
