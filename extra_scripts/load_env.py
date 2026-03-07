# Load .env and inject WIFI_SSID / WIFI_PASS into build (PlatformIO pre script).
# If .env is missing or vars are absent, firmware falls back to main.cpp defaults.

Import("env")
import os

def escape_c(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')

env_vars = {}
env_file = os.path.join(env["PROJECT_DIR"], ".env")
if os.path.exists(env_file):
    with open(env_file) as f:
        for line in f:
            line = line.strip()
            if "=" in line and not line.startswith("#"):
                k, v = line.split("=", 1)
                k = k.strip()
                v = v.strip().strip('"').strip("'")
                env_vars[k] = v

if "WIFI_SSID" in env_vars and "WIFI_PASS" in env_vars:
    env.Append(
        BUILD_FLAGS=[
            '-DWIFI_SSID=\\"' + escape_c(env_vars["WIFI_SSID"]) + '\\"',
            '-DWIFI_PASS=\\"' + escape_c(env_vars["WIFI_PASS"]) + '\\"',
        ]
    )
