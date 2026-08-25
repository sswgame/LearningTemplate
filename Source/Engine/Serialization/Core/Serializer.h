/**
 * @file Serializer.h
 * @brief 우산 헤더: Binary / JSON / XML / ObjectDiff (+ SerializeContext)
 * @note 구현은 SerializeContext.cpp, BinarySerializer.cpp, JsonSerializer.cpp,
 *       XmlSerializer.cpp, ObjectDiffSerializer.cpp, SerializerShared.cpp.
 */
#pragma once
#include "Engine/Serialization/Core/SchemaMigrate.h"
#include "Engine/Serialization/Core/SerializeContext.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/XmlSerializer.h"
#include "Engine/Serialization/Object/ObjectDiffSerializer.h"
