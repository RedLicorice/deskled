#!/usr/bin/env python3
"""Signs a built firmware image with keys/private.key.

    python3 scripts/sign.py <build dir>

Writes <build dir>/firmware.signed.bin. The Wi-Fi upload runs this first, so a signed image can
never lag behind the build it is supposed to match.
"""

import os
import subprocess
import sys

project = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
private_key = os.path.join(project, "keys", "private.key")
tool = os.path.expanduser("~/.platformio/packages/framework-arduinoespressif8266/tools/signing.py")


def main(build_dir: str) -> int:
    unsigned = os.path.join(build_dir, "firmware.bin")
    signed = os.path.join(build_dir, "firmware.signed.bin")
    for path in (unsigned, private_key, tool):
        if not os.path.isfile(path):
            sys.stderr.write("ERROR: %s missing\n" % path)
            return 1
    if os.path.exists(signed):
        os.remove(signed)
    subprocess.check_call([sys.executable, tool, "--mode", "sign", "--privatekey", private_key,
                           "--bin", unsigned, "--out", signed])
    if not os.path.isfile(signed) or os.path.getsize(signed) <= os.path.getsize(unsigned):
        sys.stderr.write("ERROR: signing produced no output\n")
        return 1
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
