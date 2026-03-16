# NvFBC Capture Plugin — Legal Notice

## Disclaimer

This plugin interfaces with NVIDIA Frame Buffer Capture (NvFBC), a
proprietary NVIDIA technology. By using this plugin, you acknowledge
and agree to the following:

1. **Authorization Required**: NvFBC access may be restricted to specific
   GPU product lines (e.g., NVIDIA Quadro, Tesla, Grid). Using NvFBC on
   unsupported hardware may violate NVIDIA's End User License Agreement
   (EULA) for GeForce drivers.

2. **No Bypass Mechanisms Included**: This plugin does NOT include any
   authentication keys, private data, or technical measures to circumvent
   NvFBC access restrictions. Users must independently obtain appropriate
   NvFBC authorization.

3. **External Private Data**: If your NvFBC configuration requires private
   authentication data, it must be supplied externally via:
   - A file at `plugins/nvfbc_auth.bin` (next to the plugin DLL)
   - The `NVFBC_PRIVDATA_FILE` environment variable pointing to the file
   
   This plugin does not generate, distribute, or embed such data.

4. **User Responsibility**: Users are solely responsible for ensuring their
   use of NvFBC complies with all applicable laws, regulations, and license
   agreements, including but not limited to the NVIDIA EULA, DMCA (US),
   EU Copyright Directive, and local intellectual property laws.

5. **No Warranty**: This plugin is provided "AS IS" without warranty of any
   kind. The authors disclaim all liability for any damages arising from
   its use.

## Third-Party Attributions

- **NvFBCCreateParams structure**: Based on definitions from
  [keylase/nvidia-patch](https://github.com/keylase/nvidia-patch)
  (MIT License)
- **NVFBCRESULT error codes**: Based on publicly documented NVIDIA
  NvFBC error codes
- **Sunshine Capture Plugin API**: Part of the Sunshine project
  (GPL-3.0 License)

## NVIDIA Capture SDK

For official NvFBC API headers and documentation, obtain the NVIDIA
Capture SDK from: https://developer.nvidia.com/capture-sdk

The Capture SDK provides the complete INvFBCToSys interface definition
needed to fully implement the capture functionality in this plugin.
