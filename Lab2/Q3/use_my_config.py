# Pre-build script: FreeRTOS.h in FreeRTOS_SAMD21 does #include "FreeRTOSConfig.h",
# which always finds the library's own copy first (same directory). Overwrite that
# copy with the project's include/FreeRTOSConfig.h so the kernel and the app
# are built with ONE customisable configuration.
import os, shutil
Import("env")
src = os.path.join(env["PROJECT_INCLUDE_DIR"], "FreeRTOSConfig.h")
dst = os.path.join(env["PROJECT_LIBDEPS_DIR"], env["PIOENV"],
                   "FreeRTOS_SAMD21", "src", "FreeRTOSConfig.h")
if os.path.isdir(os.path.dirname(dst)):
    shutil.copyfile(src, dst)
    print("use_my_config.py: using", src)
