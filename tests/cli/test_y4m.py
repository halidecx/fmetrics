import array
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


BINARY = pathlib.Path(os.environ.get("FMETRICS_BINARY", "zig-out/bin/fmetrics")).resolve()


def write_video(path, distorted=False, bits=8, full=False, height=720):
    width = 128
    chroma = " C420jpeg" if bits == 8 else f" C420p{bits}"
    range_tag = " XCOLORRANGE=FULL" if full else " XCOLORRANGE=LIMITED"
    with path.open("wb") as stream:
        stream.write(f"YUV4MPEG2 W{width} H{height} F50:1 Ip{chroma}{range_tag}\n".encode())
        for frame in range(24):
            values = []
            for plane in range(3):
                w, h = (width, height) if plane == 0 else (width // 2, height // 2)
                for y in range(h):
                    for x in range(w):
                        value = (x + y + frame * 3) % 180 + 32 if plane == 0 else 112 + (x + y + frame + plane * 3) % 32
                        value = value * (1 << (bits - 8))
                        if distorted:
                            value += (3 if x % 7 == 0 else -1) * (1 << (bits - 8))
                            if bits > 8:
                                value += x % 3
                        values.append(value)
            payload = array.array("B" if bits == 8 else "H", values)
            if bits > 8 and sys.byteorder != "little":
                payload.byteswap()
            stream.write(b"FRAME\n")
            stream.write(payload.tobytes())


class Y4mTests(unittest.TestCase):
    def compare(self, reference, distorted):
        result = subprocess.run([str(BINARY), "cvvdp", str(reference), str(distorted), "--json"], capture_output=True, text=True, check=True)
        return json.loads(result.stdout or result.stderr[result.stderr.index("{"):])

    def test_vship_y4m_golden(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for height, bits, full, expected in [
                (720, 8, False, 8.85979),
                (720, 10, False, 8.86006),
                (720, 8, True, 9.04493),
                (72, 8, False, 9.32735),
                (72, 10, False, 9.38274),
                (72, 8, True, 9.46047),
            ]:
                with self.subTest(height=height, bits=bits, full=full):
                    ref, dis = root / "ref.y4m", root / "dis.y4m"
                    write_video(ref, bits=bits, full=full, height=height)
                    write_video(dis, distorted=True, bits=bits, full=full, height=height)
                    result = self.compare(ref, dis)
                    self.assertEqual(result["frames"], 24)
                    self.assertAlmostEqual(result["jod"], expected, delta=0.0001)


if __name__ == "__main__":
    unittest.main()
