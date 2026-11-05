// Forwards the Windows ML include form used by src/core/inference and src/main.cpp to the
// upstream Microsoft.ML.OnnxRuntime.DirectML headers. Only on the include path of the
// ARX_ORT_FLAVOR=upstream build; this directory itself must not contain the real header.
#pragma once
#include <dml_provider_factory.h>
