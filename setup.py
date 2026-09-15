import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


root = pathlib.Path(__file__).parent.resolve()
zon = (root / "build.zig.zon").read_text()
version = re.search(r'\.version\s*=\s*"([^"]+)"', zon).group(1)


class ZigBuildExt(build_ext):
    def build_extensions(self):
        prefix = pathlib.Path(self.build_temp).resolve() / "zig-out"
        zig = [shutil.which("zig")] if shutil.which("zig") else [
            sys.executable,
            "-m",
            "ziglang",
        ]
        target = []
        if sys.platform == "darwin":
            deployment = os.environ.get("MACOSX_DEPLOYMENT_TARGET", "11.0")
            arch = {"arm64": "aarch64"}.get(
                platform.machine(), platform.machine()
            )
            target = [f"-Dtarget={arch}-macos.{deployment}"]
        elif sys.platform.startswith("linux"):
            arch = platform.machine()
            target = [f"-Dtarget={arch}-linux-gnu"]
        subprocess.run(
            zig + [
                "build",
                "--release=fast",
                "-Dstrip=true",
                "--prefix",
                str(prefix),
            ] + target,
            cwd=root,
            check=True,
        )
        archive = prefix / "lib" / "libfmetrics.a"
        if sys.platform == "darwin":
            subprocess.run(["ranlib", archive], check=True)
        for extension in self.extensions:
            extension.extra_objects = [str(archive)]
        super().build_extensions()


libraries = ["m", "pthread"] if sys.platform.startswith("linux") else []
extension = Extension(
    "fmetrics._native",
    sources=["python/fmetrics/_native.c"],
    include_dirs=["src"],
    libraries=libraries,
    define_macros=[("Py_LIMITED_API", "0x030B0000")],
    py_limited_api=True,
)

setup(
    version=version,
    ext_modules=[extension],
    cmdclass={"build_ext": ZigBuildExt},
    options={"bdist_wheel": {"py_limited_api": "cp311"}},
)
