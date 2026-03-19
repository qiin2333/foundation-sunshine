/**
 * @file src/platform/windows/virtual_mouse.cpp
 * @brief Zako Virtual Mouse client implementation.
 *
 * Finds and opens the Zako Virtual Mouse HID device by matching
 * VID/PID, then sends mouse data via HID output reports.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// HID headers
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>

#include "virtual_mouse.h"
#include "src/logging.h"

// From vmouse_shared.h - duplicated here to avoid driver header dependency
static constexpr uint16_t VMOUSE_VID = 0x1ACE;
static constexpr uint16_t VMOUSE_PID = 0x0002;
static constexpr uint8_t VMOUSE_OUTPUT_REPORT_ID = 0x02;
static constexpr uint16_t VMOUSE_OUTPUT_REPORT_SIZE = 8;

namespace {
#ifdef SUNSHINE_VIRTUAL_MOUSE_STANDALONE_TEST
  struct null_log_t {
    template<typename T>
    null_log_t &
    operator<<(T &&) {
      return *this;
    }
  };

  null_log_t &
  null_log() {
    static null_log_t logger;
    return logger;
  }

  #define VMOUSE_LOG(severity) null_log()
#else
  #define VMOUSE_LOG(severity) BOOST_LOG(severity)
#endif

  /**
   * @brief Convert a wide string to a narrow (UTF-8) string for logging.
   */
  std::string
  wide_to_utf8(const wchar_t *wstr) {
    if (!wstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
  }

  bool
  query_caps(HANDLE device, HIDP_CAPS &caps) {
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (!HidD_GetPreparsedData(device, &preparsed)) {
      return false;
    }

    const auto status = HidP_GetCaps(preparsed, &caps);
    HidD_FreePreparsedData(preparsed);
    return status == HIDP_STATUS_SUCCESS;
  }

  bool
  is_vmouse_device(const HIDD_ATTRIBUTES &attrs, const HIDP_CAPS &caps) {
    return attrs.VendorID == VMOUSE_VID &&
           attrs.ProductID == VMOUSE_PID &&
           caps.FeatureReportByteLength >= VMOUSE_OUTPUT_REPORT_SIZE;
  }
}  // namespace

using namespace std::literals;

namespace platf {
  namespace vmouse {
    namespace detail {
      output_report_t
      build_output_report(uint8_t buttons, int16_t delta_x, int16_t delta_y,
                          int8_t scroll_v, int8_t scroll_h) {
        return output_report_t {
          VMOUSE_OUTPUT_REPORT_ID,
          buttons,
          static_cast<uint8_t>(delta_x & 0xFF),
          static_cast<uint8_t>((delta_x >> 8) & 0xFF),
          static_cast<uint8_t>(delta_y & 0xFF),
          static_cast<uint8_t>((delta_y >> 8) & 0xFF),
          static_cast<uint8_t>(scroll_v),
          static_cast<uint8_t>(scroll_h),
        };
      }

      uint8_t
      apply_button_transition(uint8_t current_buttons, uint8_t button_mask,
                              bool release) {
        return release ? static_cast<uint8_t>(current_buttons & ~button_mask) :
                         static_cast<uint8_t>(current_buttons | button_mask);
      }

      bool
      should_close_on_write_error(unsigned long err) {
        return err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_GEN_FAILURE;
      }
    }  // namespace detail

    // ========================================================================
    // Implementation Detail
    // ========================================================================

    struct device_t::impl_t {
      HANDLE hDevice = INVALID_HANDLE_VALUE;
      uint8_t buttonState = 0;  // Current button state (accumulated)

      ~impl_t() {
        close();
      }

      void
      close() {
        if (hDevice != INVALID_HANDLE_VALUE) {
          CloseHandle(hDevice);
          hDevice = INVALID_HANDLE_VALUE;
        }
      }

      /**
       * @brief Find and open the virtual mouse HID device.
       *
       * Enumerates all HID devices and finds the one matching our VID/PID.
       * Opens it for writing (to send output reports).
       */
      bool
      open() {
        GUID hidGuid;
        HidD_GetHidGuid(&hidGuid);

        HDEVINFO devInfoSet = SetupDiGetClassDevsW(
            &hidGuid, NULL, NULL,
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

        if (devInfoSet == INVALID_HANDLE_VALUE) {
          VMOUSE_LOG(debug) << "vmouse: SetupDiGetClassDevs failed"sv;
          return false;
        }

        bool found = false;
        SP_DEVICE_INTERFACE_DATA devInterfaceData;
        devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD i = 0;
             SetupDiEnumDeviceInterfaces(devInfoSet, NULL, &hidGuid, i, &devInterfaceData);
             i++) {
          // Get required buffer size
          DWORD requiredSize = 0;
          SetupDiGetDeviceInterfaceDetailW(devInfoSet, &devInterfaceData, NULL, 0, &requiredSize, NULL);
          if (requiredSize == 0) continue;

          // Allocate and get detail
          auto detailBuf = std::make_unique<BYTE[]>(requiredSize);
          auto *detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuf.get());
          detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

          if (!SetupDiGetDeviceInterfaceDetailW(
                  devInfoSet, &devInterfaceData, detail, requiredSize, NULL, NULL)) {
            continue;
          }

          // Open with no access first so we can inspect attributes even if
          // the device only allows write access for output reports.
          HANDLE h = CreateFileW(
              detail->DevicePath,
              0,
              FILE_SHARE_READ | FILE_SHARE_WRITE,
              NULL,
              OPEN_EXISTING,
              0,
              NULL);

          if (h == INVALID_HANDLE_VALUE) continue;

          // Check if this is our virtual mouse device.
          HIDD_ATTRIBUTES attrs;
          attrs.Size = sizeof(HIDD_ATTRIBUTES);
          if (HidD_GetAttributes(h, &attrs)) {
            HIDP_CAPS caps;
            if (query_caps(h, caps) && is_vmouse_device(attrs, caps)) {
              hDevice = h;
              found = true;

              VMOUSE_LOG(info) << "vmouse: Found virtual mouse device at "sv
                               << wide_to_utf8(detail->DevicePath);
              break;
            }
          }

          CloseHandle(h);
        }

        SetupDiDestroyDeviceInfoList(devInfoSet);

        if (!found) {
          VMOUSE_LOG(debug) << "vmouse: Virtual mouse device not found (VID="sv
                            << std::hex << VMOUSE_VID
                            << " PID="sv << VMOUSE_PID << ")"sv;
        }

        return found;
      }

      /**
       * @brief Send an output report to the virtual mouse driver.
       */
      bool
      sendReport(uint8_t buttons, int16_t dx, int16_t dy, int8_t sv, int8_t sh) {
        if (hDevice == INVALID_HANDLE_VALUE) return false;

        auto report = detail::build_output_report(buttons, dx, dy, sv, sh);

        BOOL result = HidD_SetFeature(hDevice, (PVOID) report.data(), static_cast<ULONG>(report.size()));
        if (!result) {
          DWORD err = GetLastError();
          if (detail::should_close_on_write_error(err)) {
            VMOUSE_LOG(warning) << "vmouse: Device disconnected, closing handle"sv;
            close();
          }
          return false;
        }

        return true;
      }
    };

    // ========================================================================
    // device_t Methods
    // ========================================================================

    device_t::device_t(): impl(std::make_unique<impl_t>()) {}
    device_t::~device_t() = default;
    device_t::device_t(device_t &&other) noexcept = default;
    device_t &device_t::operator=(device_t &&other) noexcept = default;

    bool
    device_t::is_available() const {
      return impl && impl->hDevice != INVALID_HANDLE_VALUE;
    }

    bool
    device_t::move(int16_t delta_x, int16_t delta_y) {
      return impl->sendReport(impl->buttonState, delta_x, delta_y, 0, 0);
    }

    bool
    device_t::button(uint8_t button_mask, bool release) {
      impl->buttonState = detail::apply_button_transition(impl->buttonState, button_mask, release);
      // Send report with current button state and zero movement
      return impl->sendReport(impl->buttonState, 0, 0, 0, 0);
    }

    bool
    device_t::scroll(int8_t distance) {
      return impl->sendReport(impl->buttonState, 0, 0, distance, 0);
    }

    bool
    device_t::hscroll(int8_t distance) {
      return impl->sendReport(impl->buttonState, 0, 0, 0, distance);
    }

    bool
    device_t::send_report(uint8_t buttons, int16_t delta_x, int16_t delta_y,
                          int8_t scroll_v, int8_t scroll_h) {
      impl->buttonState = buttons;
      return impl->sendReport(buttons, delta_x, delta_y, scroll_v, scroll_h);
    }

    // ========================================================================
    // Factory Functions
    // ========================================================================

    device_t
    create() {
      device_t dev;
      if (!dev.impl->open()) {
        VMOUSE_LOG(info) << "vmouse: Virtual mouse driver not available, "
                            "falling back to SendInput"sv;
      }
      return dev;
    }

    bool
    is_driver_installed() {
      GUID hidGuid;
      HidD_GetHidGuid(&hidGuid);

      HDEVINFO devInfoSet = SetupDiGetClassDevsW(
          &hidGuid, NULL, NULL,
          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

      if (devInfoSet == INVALID_HANDLE_VALUE) return false;

      bool found = false;
      SP_DEVICE_INTERFACE_DATA devInterfaceData;
      devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

      for (DWORD i = 0;
           SetupDiEnumDeviceInterfaces(devInfoSet, NULL, &hidGuid, i, &devInterfaceData);
           i++) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfoSet, &devInterfaceData, NULL, 0, &requiredSize, NULL);
        if (requiredSize == 0) continue;

        auto detailBuf = std::make_unique<BYTE[]>(requiredSize);
        auto *detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuf.get());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(
                devInfoSet, &devInterfaceData, detail, requiredSize, NULL, NULL)) {
          continue;
        }

        HANDLE h = CreateFileW(
            detail->DevicePath,
            0,  // No access needed, just check attributes
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);

        if (h == INVALID_HANDLE_VALUE) continue;

        HIDD_ATTRIBUTES attrs;
        attrs.Size = sizeof(HIDD_ATTRIBUTES);
        if (HidD_GetAttributes(h, &attrs)) {
          HIDP_CAPS caps;
          if (query_caps(h, caps) && is_vmouse_device(attrs, caps)) {
            found = true;
            CloseHandle(h);
            break;
          }
        }

        CloseHandle(h);
      }

      SetupDiDestroyDeviceInfoList(devInfoSet);
      return found;
    }

  }  // namespace vmouse
}  // namespace platf

#undef VMOUSE_LOG
