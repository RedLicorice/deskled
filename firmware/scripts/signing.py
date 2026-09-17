# PlatformIO extra script: firmware signing.
# - before building: embeds keys/public.key as include/signing_pubkey.h (required, build fails without it)
# - after building: writes firmware.signed.bin, signed with keys/private.key (skipped with a warning if absent)
# The device only accepts signed images over Wi-Fi; serial flashing is not checked.

import os
import subprocess
import sys

Import("env")  # noqa: F821

project = env.subst("$PROJECT_DIR")
public_key = os.path.join(project, "keys", "public.key")
private_key = os.path.join(project, "keys", "private.key")
header = os.path.join(project, "include", "signing_pubkey.h")
signing_tool = os.path.join(env.PioPlatform().get_package_dir("framework-arduinoespressif8266"), "tools", "signing.py")

if not os.path.isfile(public_key):
    sys.stderr.write("ERROR: %s missing; run scripts/genkeys.sh to create a signing key pair\n" % public_key)
    env.Exit(1)

os.makedirs(os.path.dirname(header), exist_ok=True)
subprocess.check_call([sys.executable, signing_tool, "--mode", "header", "--publickey", public_key, "--out", header])


def sign_firmware(source, target, env):
    unsigned = str(target[0])
    signed = os.path.join(os.path.dirname(unsigned), "firmware.signed.bin")
    if not os.path.isfile(private_key):
        sys.stderr.write("WARNING: %s missing, firmware.signed.bin not created (Wi-Fi updates need it; "
                         "run scripts/genkeys.sh)\n" % private_key)
        return
    if os.path.exists(signed):
        os.remove(signed)
    subprocess.check_call([sys.executable, signing_tool, "--mode", "sign", "--privatekey", private_key,
                           "--bin", unsigned, "--out", signed])
    # signing.py reports errors without failing, so check the result ourselves
    if not os.path.isfile(signed) or os.path.getsize(signed) <= os.path.getsize(unsigned):
        sys.stderr.write("ERROR: signing failed\n")
        env.Exit(1)


env.AddPostAction("$BUILD_DIR/firmware.bin", sign_firmware)  # noqa: F821
