/**
 * @file nvfbc_win_defs.h
 * @brief Windows NvFBC API type definitions.
 *
 * Based on publicly available information:
 *   - keylase/nvidia-patch nvfbcdefs.h (MIT license) for NvFBCCreateParams
 *   - NVIDIA public documentation for error codes and buffer formats
 *
 * IMPORTANT:
 *   This file does NOT contain proprietary NVIDIA Capture SDK headers.
 *   The INvFBCToSys interface is NOT defined here — users must provide
 *   their own interface definitions from an authorized source (NVIDIA
 *   Capture SDK, Grid SDK, etc.).
 *
 *   No authentication credentials, private keys, or bypass mechanisms
 *   are included. Users are responsible for obtaining appropriate access
 *   to NvFBC functionality through legitimate channels.
 */
#pragma once

#include <cstdint>
#include <Windows.h>

// ============================================================================
// NvFBC version and macros (from keylase/nvidia-patch nvfbcdefs.h, MIT license)
// ============================================================================

typedef unsigned long NvU32;

#define NVFBC_DLL_VERSION 0x70

#define NVFBC_STRUCT_VERSION(typeName, ver) \
  (NvU32)(sizeof(typeName) | ((ver) << 16) | (NVFBC_DLL_VERSION << 24))

#define NVFBCAPI __stdcall

// ============================================================================
// Error codes (NVFBCRESULT) — documented in NVIDIA public references
// ============================================================================

typedef enum _NVFBCRESULT {
  NVFBC_SUCCESS = 0,
  NVFBC_ERROR_GENERIC = -1,
  NVFBC_ERROR_INVALID_PARAM = -2,
  NVFBC_ERROR_INVALIDATED_SESSION = -3,
  NVFBC_ERROR_PROTECTED_CONTENT = -4,
  NVFBC_ERROR_DRIVER_FAILURE = -5,
  NVFBC_ERROR_CUDA_FAILURE = -6,
  NVFBC_ERROR_UNSUPPORTED = -7,
  NVFBC_ERROR_HW_ENC_FAILURE = -8,
  NVFBC_ERROR_INCOMPATIBLE_DRIVER = -9,
  NVFBC_ERROR_UNSUPPORTED_PLATFORM = -10,
  NVFBC_ERROR_OUT_OF_MEMORY = -11,
  NVFBC_ERROR_INVALID_PTR = -12,
  NVFBC_ERROR_INCOMPATIBLE_VERSION = -13,
  NVFBC_ERROR_OPT_CAPTURE_FAILURE = -14,
  NVFBC_ERROR_INSUFFICIENT_PRIVILEGES = -15,
  NVFBC_ERROR_INVALID_CALL = -16,
  NVFBC_ERROR_SYSTEM_ERROR = -17,
  NVFBC_ERROR_INVALID_TARGET = -18,
  NVFBC_ERROR_NVAPI_FAILURE = -19,
  NVFBC_ERROR_DYNAMIC_DISABLE = -20,
  NVFBC_ERROR_IPC_FAILURE = -21,
  NVFBC_ERROR_CURSOR_CAPTURE_FAILURE = -22,
} NVFBCRESULT;

// ============================================================================
// Buffer formats — common across NvFBC platforms
// ============================================================================

typedef enum _NVFBC_BUFFER_FORMAT {
  NVFBC_BUFFER_FORMAT_ARGB = 0,
  NVFBC_BUFFER_FORMAT_RGB = 1,
  NVFBC_BUFFER_FORMAT_NV12 = 2,
  NVFBC_BUFFER_FORMAT_YUV444P = 3,
  NVFBC_BUFFER_FORMAT_RGBA = 4,
  NVFBC_BUFFER_FORMAT_BGRA = 5,
} NVFBC_BUFFER_FORMAT;

// ============================================================================
// Interface type IDs for NvFBC_CreateEx
// ============================================================================

typedef enum _NVFBC_INTERFACE_TYPE {
  NVFBC_TO_SYS = 0,
  NVFBC_TO_CUDA = 1,
  NVFBC_TO_DX9VID = 2,
  NVFBC_TO_HW_ENC = 3,
} NVFBC_INTERFACE_TYPE;

// ============================================================================
// NvFBC_CreateEx parameters (from keylase/nvidia-patch, MIT license)
// ============================================================================

typedef struct _NvFBCCreateParams {
  NvU32 dwVersion;
  NvU32 dwInterfaceType;
  NvU32 dwMaxDisplayWidth;
  NvU32 dwMaxDisplayHeight;
  void *pDevice;
  void *pPrivateData;
  NvU32 dwPrivateDataSize;
  NvU32 dwInterfaceVersion;
  void *pNvFBC;
  NvU32 dwAdapterIdx;
  NvU32 dwNvFBCVersion;
  void *cudaCtx;
  void *pPrivateData2;
  NvU32 dwPrivateData2Size;
  NvU32 dwReserved[55];
  void *pReserved[27];
} NvFBCCreateParams;

#define NVFBC_CREATE_PARAMS_VER NVFBC_STRUCT_VERSION(NvFBCCreateParams, 2)

// ============================================================================
// Function pointer types for NvFBC64.dll exports
// ============================================================================

typedef NVFBCRESULT(NVFBCAPI *NvFBC_CreateFunctionExType)(void *pCreateParams);
typedef NVFBCRESULT(NVFBCAPI *NvFBC_GetStatusExFunctionType)(void *pStatusParams);

// ============================================================================
// Frame grab info (populated after successful capture)
// ============================================================================

typedef struct _NvFBCFrameGrabInfo {
  NvU32 dwWidth;
  NvU32 dwHeight;
  NvU32 dwBufferWidth;
  NvU32 dwReserved;
  NvU32 bIsNewFrame;
  NvU32 dwReservedFields[27];
} NvFBCFrameGrabInfo;
