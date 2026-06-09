#!/bin/bash
# Enable Monado's simulated HMD and controller (Qwerty) driver
export QWERTY_ENABLE=1

# Enable Monado's debug GUI (required to view the compositor window and control the HMD/controllers)
export XRT_DEBUG_GUI=1

# If Monado is not registered as the default system OpenXR runtime, point to it directly:
# export XR_RUNTIME_JSON="/usr/share/openxr/1/openxr_monado.json"

# Run the compiled application
./build/Main
