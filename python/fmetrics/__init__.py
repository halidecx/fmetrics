from __future__ import annotations

from collections.abc import Callable
from enum import IntEnum
from typing import Any

from . import _native


__version__: str = _native.version()
Error = _native.Error


class DisplayModel(IntEnum):
    STANDARD_FHD = 0
    STANDARD_4K = 1
    STANDARD_HDR_PQ = 2
    STANDARD_HDR_HLG = 3
    STANDARD_HDR_LINEAR = 4
    STANDARD_HDR_DARK = 5
    STANDARD_HDR_LINEAR_ZOOM = 6
    STANDARD_HMD = 7
    STANDARD_PHONE = 8
    SDR_4K_30 = 9
    SDR_FHD_24 = 10
    HTC_VIVE_PRO = 11
    IPHONE_12_PRO = 12
    IPHONE_14_PRO = 13
    IPHONE_14_PRO_VERT = 14
    IPHONE_14_PRO_HDR = 15
    IPHONE_14_PRO_HDR_VERT = 16
    IPAD_PRO_12_9 = 17
    MACBOOK_PRO_16 = 18
    LG_OLED_2017_SDR = 19
    LG_OLED_2017_HDR = 20
    EIZO_CG3146 = 21
    HDR_PQ_65_INCH_4KNIT = 22
    HDR_PQ_65_INCH_2KNIT = 23
    HDR_PQ_65_INCH_1KNIT = 24
    LG_OLED_2026_HDR_PQ = 25
    CID22_MCOS = 26


def _display_model(value: int | str | DisplayModel) -> DisplayModel:
    if isinstance(value, str):
        try:
            return DisplayModel[value.upper()]
        except KeyError:
            raise ValueError(
                f"unknown CVVDP display model: {value}"
            ) from None
    return DisplayModel(value)


class Workspace:
    def __init__(self) -> None:
        self._handle = _native.workspace_create()

    def iwssim(self, reference: Any, distorted: Any) -> float:
        return _native.iwssim(self._handle, reference, distorted)

    def msssim(self, reference: Any, distorted: Any) -> float:
        return _native.msssim(self._handle, reference, distorted)

    def ssimu2(self, reference: Any, distorted: Any) -> float:
        return _native.ssimu2(self._handle, reference, distorted)

    def ssimu2_map(
        self,
        reference: Any,
        distorted: Any,
    ) -> tuple[float, memoryview]:
        return _map_result(
            _native.ssimu2_map(self._handle, reference, distorted)
        )

    def butteraugli(
        self,
        reference: Any,
        distorted: Any,
        intensity_target: float = 203.0,
        pnorm: int = 3,
    ) -> float:
        return _native.butteraugli(
            self._handle,
            reference,
            distorted,
            intensity_target,
            pnorm,
        )

    def butteraugli_map(
        self,
        reference: Any,
        distorted: Any,
        intensity_target: float = 203.0,
        pnorm: int = 3,
    ) -> tuple[float, memoryview]:
        return _map_result(
            _native.butteraugli_map(
                self._handle,
                reference,
                distorted,
                intensity_target,
                pnorm,
            )
        )


class Cvvdp:
    def __init__(
        self,
        width: int,
        height: int,
        fps: float,
        display_model: int | str | DisplayModel = DisplayModel.STANDARD_FHD,
        threads: int = 0,
    ) -> None:
        self.width = width
        self.height = height
        self.fps = fps
        self.display_model = _display_model(display_model)
        self.threads = threads
        self._handle = _native.cvvdp_create(
            width,
            height,
            fps,
            int(self.display_model),
            threads,
        )

    def process_frame(
        self,
        reference: Any,
        distorted: Any,
    ) -> tuple[float, float]:
        return _native.cvvdp_process_frame(
            self._handle,
            reference,
            distorted,
        )

    def reset(self) -> None:
        _native.cvvdp_reset(self._handle)

    def close(self) -> None:
        _native.cvvdp_close(self._handle)

    def __enter__(self) -> Cvvdp:
        return self

    def __exit__(self, *args: Any) -> None:
        self.close()


def _map_result(result: tuple[float, bytearray, int, int]) -> tuple[
    float,
    memoryview,
]:
    score, data, height, width = result
    return score, memoryview(data).cast("I", shape=(height, width))


def _run(method: Callable[..., Any], *args: Any) -> Any:
    return method(Workspace()._handle, *args)


def iwssim(reference: Any, distorted: Any) -> float:
    return _run(_native.iwssim, reference, distorted)


def msssim(reference: Any, distorted: Any) -> float:
    return _run(_native.msssim, reference, distorted)


def ssimu2(reference: Any, distorted: Any) -> float:
    return _run(_native.ssimu2, reference, distorted)


def ssimu2_map(
    reference: Any,
    distorted: Any,
) -> tuple[float, memoryview]:
    return _map_result(_run(_native.ssimu2_map, reference, distorted))


def butteraugli(
    reference: Any,
    distorted: Any,
    intensity_target: float = 203.0,
    pnorm: int = 3,
) -> float:
    return _run(
        _native.butteraugli,
        reference,
        distorted,
        intensity_target,
        pnorm,
    )


def butteraugli_map(
    reference: Any,
    distorted: Any,
    intensity_target: float = 203.0,
    pnorm: int = 3,
) -> tuple[float, memoryview]:
    result = _run(
        _native.butteraugli_map,
        reference,
        distorted,
        intensity_target,
        pnorm,
    )
    return _map_result(result)


def cvvdp(
    reference: Any,
    distorted: Any,
    display_model: int | str | DisplayModel = DisplayModel.STANDARD_FHD,
    threads: int = 0,
) -> tuple[float, float]:
    model = _display_model(display_model)
    return _native.cvvdp(
        reference,
        distorted,
        int(model),
        threads,
    )


def cvvdp_version() -> str:
    return _native.cvvdp_version()


__all__ = [
    "Error",
    "Cvvdp",
    "DisplayModel",
    "Workspace",
    "__version__",
    "butteraugli",
    "butteraugli_map",
    "cvvdp",
    "cvvdp_version",
    "iwssim",
    "msssim",
    "ssimu2",
    "ssimu2_map",
]
